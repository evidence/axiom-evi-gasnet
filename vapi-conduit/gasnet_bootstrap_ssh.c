#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

#include <gasnet.h>
#include <gasnet_internal.h>

#define ARY 8

#define USE_LOCAL_SPAWN 1

#ifndef USE_LOCAL_SPAWN
  #define USE_LOCAL_SPAWN 0
#endif

#define SSH_SERVERS_DELIM_CHARS  " ,/;:"
#define SSH_NODEFILE_DELIM_CHARS " \t\n\r"
#define SSH_OPTIONS_DELIM_CHARS  " \t\n\r"

extern char **environ;
#define ENV_PREFIX "GASNET_"

enum {
  CMD_BARRIER_UP,
  CMD_BARRIER_DOWN
};

typedef struct hostlist_t_ {
  size_t	len;
  char		*name;
} hostlist_t;

static gasnet_node_t nproc = (gasnet_node_t)(-1L);
static gasnet_node_t myproc = (gasnet_node_t)(-1L);
static gasnet_node_t tree_size = (gasnet_node_t)(-1L);
static char cwd[1024];
static int mypid;
static int devnull = -1;
static int listener = -1;
static int listen_port = -1;
static int children = 0;
static hostlist_t *hostlist;
static char **env_vars;

static char **ssh_argv = NULL;
static int ssh_argc;

static char *sappendf(char *s, const char *fmt, ...) __attribute__((__format__ (__printf__, 2, 3)));
static char *sappendf(char *s, const char *fmt, ...)
{
  va_list args;
  int old_len, add_len;

  va_start(args, fmt);

  /* compute length of thing to append */
  add_len = vsnprintf(NULL, 0, fmt, args);

  /* grow the string, including space for '\0': */
  if (s) {
    old_len = strlen(s);
#if 0 /* No gasneti_realloc */
    s = gasneti_realloc(s, old_len + add_len + 1);
#else
    { char *tmp = gasneti_malloc(old_len + add_len + 1);
      memcpy(tmp, s, old_len + 1);
      gasneti_free(s);
      s = tmp;
    }
#endif
  } else {
    old_len = 0;
    s = gasneti_malloc(add_len + 1);
  }

  /* append */
  vsprintf((s+old_len), fmt, args);

  va_end(args);

  return s;
}

static struct {
  volatile pid_t	pid;	/* reset to zero by reaper */
  gasnet_node_t		rank;
  gasnet_node_t		size;	/* size of subtree rooted at this child */
  int			sock;
} child[ARY + 1]; /* +1 is for a "sentinal" element, always unused */

static struct {
  const char *		name;	/* XXX: no need to be global and/or here? */
  int			port;	/* XXX: no need to be global and/or here? */
  int			sock;
} parent;

static void reaper(int sig)
{
  pid_t pid;

  gasneti_reghandler(SIGCHLD, &reaper);

  while((pid = waitpid(-1,NULL,WNOHANG)) > 0) {
    int i;
    for (i = 0; i < ARY; ++i) {
      if (pid == child[i].pid) {
	child[i].pid = 0;
	break;
      }
    }
  }
}

static void do_abort(void)
{
  static const char c = 0xff;
  int j;

  gasneti_reghandler(SIGURG, SIG_IGN);
  gasneti_reghandler(SIGABRT, SIG_DFL);

  send(parent.sock, &c, 1, MSG_OOB);
  for (j = 0; j < children; ++j) {
    send(child[j].sock, &c, 1, MSG_OOB);
  }

  abort();
  /* NOT REACHED */
}

static void sigurg_handler(int sig)
{
  do_abort();
  /* NOT REACHED */
}

static ssize_t do_write(int fd, const void *buf, size_t len)
{
  const char *p = (const char *)buf;
  while (len) {
    ssize_t rc = write(fd, p, len);
    if (rc < 0) {
      break;
    }
    p += rc;
    len -= rc;
  }
  return len ? -1 : 0;
}

static void do_write_string(int fd, const char *string) {
  size_t len = string ? strlen(string) : 0;
  do_write(fd, &len, sizeof(len));
  do_write(fd, string, len);
}

static ssize_t do_read(int fd, void *buf, size_t len)
{
  char *p = (char *)buf;
  while (len) {
    ssize_t rc = read(fd, p, len);
    if (rc <= 0) {
      break;
    }
    p += rc;
    len -= rc;
  }
  return len ? -1 : 0;
}

static ssize_t do_read_string(int fd, char **string_p) {
  size_t len;

  do_read(fd, &len, sizeof(size_t));
  if (len) {
    char *tmp = gasneti_malloc(len + 1);
    do_read(fd, tmp, len);
    tmp[len] = '\0';
    *string_p = tmp;
  }

  return len;
}

static char *quote_arg(const char *arg) {
  char *result = gasneti_strdup("'");
  char *p, *q, *tmp;

  p = tmp = gasneti_strdup(arg);
  while ((q = strchr(p, '\'')) != NULL) {
    *q = '\0';
    result = sappendf(result, "%s'\\''", p);
    p = q + 1;
  }
  result = sappendf(result, "%s'", p);
  gasneti_free(tmp);
  return result;
}

static char *stringify_args(int argc, char **argv) {
  char *args = gasneti_calloc(1,1); /* == '\0' */
  int i;

  for (i = 0; i < argc; ++i) {
    char *q = quote_arg(argv[i]);
    args = sappendf(args, " %s", q);
    gasneti_free(q);
  }

  return args;
}

/* Build an array of strings from a comma separated list */
static char ** list_to_array(const char *list, const char *delims) {
  char **result = NULL;
  char **q;
  const char *p;
  int count=0;

  /* Count number of tokens in list.  */
  p = list;
  while (*p) {
    while (*p && strchr(delims,*p)) ++p; /* eat delimiters */
    if (!*p) break; /* reached end of string */
    ++count;
    while (*p && !strchr(delims,*p)) ++p; /* eat non-delimiters */
  }

  /* Allocate the array */
  result = gasneti_malloc(sizeof(void *) * (count + 1));
  q = result;

  /* Populate the array */
  if (count) {
    char *r = gasneti_strdup(list);
    while (*r) {
      while (*r && strchr(delims,*r)) ++r; /* eat leading delimiters */
      if (!*r) break; /* reached end of string w/o adding a token */
      *(q++) = r;
      r += strcspn(r, delims);
      if (*r) *(r++) = '\0'; /* add trailing null if needed */
    }
  }
  *q = NULL;

  return result;
}

static void build_hostlist(void)
{
  gasnet_node_t i;
  const char *env_string;

  hostlist = gasneti_malloc(nproc * sizeof(hostlist_t));

  if ((env_string = getenv(ENV_PREFIX "SSH_NODEFILE")) != NULL && strlen(env_string)) {
    FILE *fp;

    fp = fopen(env_string, "r");
    if (!fp) {
      gasneti_fatalerror("fopen(%s)\n", env_string);
    }

    for (i = 0; i < nproc; ++i) {
      char buf[256];

      if (!fgets(buf, 256, fp)) {
	/* ran out of lines */
        gasneti_fatalerror("Out of hosts in file %s\n", env_string);
      }

      buf[strcspn(buf, SSH_NODEFILE_DELIM_CHARS)] = '\0';
      hostlist[i].len  = strlen(buf);
      hostlist[i].name = gasneti_strdup(buf);
    }

    fclose(fp);
  } else if ((env_string = getenv(ENV_PREFIX "SSH_SERVERS")) != NULL && strlen(env_string)) {
    char **names = list_to_array(env_string, SSH_SERVERS_DELIM_CHARS);
    for (i = 0; i < nproc; ++i) {
      char *p = names[i];
      if (!p) {
        gasneti_fatalerror("Out of hosts in SSH_SERVERS\n");
      }
      hostlist[i].len  = strlen(p);
      hostlist[i].name = p;
    }

    gasneti_free(names);
  } else {
    gasneti_fatalerror("No " _STRINGIFY(ENV_PREFIX) "SSH_NODEFILE or " _STRINGIFY(ENV_PREFIX) "SSH_SERVERS in environment\n");
  }
}

static void send_hostlist(int s, int count, const hostlist_t *list) {
  gasnet_node_t i;
  size_t len;

  for (i = 0; i < count; ++i) {
    len = list[i].len;
    do_write(s, &len, sizeof(size_t));
    do_write(s, list[i].name, len);
  }
}

static void recv_hostlist(int s, int count) {
  gasnet_node_t i;

  hostlist = gasneti_malloc(count * sizeof(hostlist_t));
  for (i = 0; i < count; ++i) {
    hostlist[i].len = do_read_string(s, &(hostlist[i].name));
  }
  hostlist -= myproc; /* offset */
}

static void send_env(int s, char **list) {
  int i;
  const char *p;
  size_t plen = strlen(ENV_PREFIX);
  size_t rlen = strlen(ENV_PREFIX "SSH_");
  size_t len;

  for (i = 0, p = environ[0]; p != NULL; p = environ[++i]) {
    if (!strncmp(ENV_PREFIX, p, plen)) {
      if (!strncmp(ENV_PREFIX "SSH_", p, rlen)) {
	/* We parse these ourselves, don't forward */
	continue;
      }
      do_write_string(s, p);
    } else if (list) {
      int j;
      const char *q;
      for (j = 0, q = list[0]; q != NULL; q = list[++j]) {
        len = strlen(q);
        if (!strncmp(q, p, len) && (p[len] == '=')) {
          do_write_string(s, p);
	  break;
        }
      }
    }
  }

  /* end marker */
  len = 0;
  do_write(s, &len, sizeof(len));
}

static void recv_env(int s) {
  char *p;

  while (do_read_string(s, &p)) {
    if (putenv(p) < 0) {
      gasneti_fatalerror("putenv(%s) failed\n", p);
    }
  }
}

static void send_ssh_argv(int s) {
  int i;

  do_write(s, &ssh_argc, sizeof(int));
  for (i = 0; i < ssh_argc; ++i) {
    do_write_string(s, ssh_argv[i]);
  }
}

static void recv_ssh_argv(int s) {
  int i;

  do_read(s, &ssh_argc, sizeof(int));
  ssh_argv = gasneti_calloc(ssh_argc+3, sizeof(char *));
  for (i = 0; i < ssh_argc; ++i) {
    do_read_string(s, &ssh_argv[i]);
  }
}

static void pre_spawn(void) {
  struct sockaddr_in sock_addr;
  socklen_t addr_len;

  /* Get the cwd */
  if (!getcwd(cwd, sizeof(cwd))) {
    gasneti_fatalerror("getcwd() failed\n");
  }

  /* Open /dev/null */
  devnull = open("/dev/null", O_RDONLY);
  if (devnull < 0) {
    gasneti_fatalerror("open(/dev/null) failed\n");
  }

  /* Create listening socket */
  if ((listener = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    gasneti_fatalerror("listener = socket() failed\n");
  }
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = 0;
  sock_addr.sin_addr.s_addr = INADDR_ANY;
  addr_len = sizeof(sock_addr);
  if (bind(listener, (struct sockaddr *)&sock_addr, addr_len) < 0) {
    gasneti_fatalerror("bind() failed\n");
  }
  if (listen(listener, ARY) < 0) {
    gasneti_fatalerror("listen() failed\n");
  }
  if (getsockname(listener, (struct sockaddr *)&sock_addr, &addr_len) < 0) {
    gasneti_fatalerror("getsockname() failed\n");
  }
  listen_port = ntohs(sock_addr.sin_port);
}

static void post_spawn(int count) {
  /* Accept count connections */
  while (count--) {
    gasnet_node_t size, rank;
    struct sockaddr_in sock_addr;
    socklen_t addr_len;
    int id;
    int s;

    if ((s = accept(listener, (struct sockaddr *)&sock_addr, &addr_len)) < 0) {
      gasneti_fatalerror("accept() failed\n");
    }
    (void)ioctl(s, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
    if (do_read(s, &id, sizeof(int)) < 0) {
      gasneti_fatalerror("do_read(id) failed\n");
    }
    if (id < 0) {
      rank = 0;
      size = nproc;
    } else {
      child[id].sock = s;
      rank = child[id].rank;
      size = child[id].size;
    }
    do_write(s, &rank, sizeof(gasnet_node_t));
    do_write(s, &nproc, sizeof(gasnet_node_t));
    do_write(s, &size, sizeof(gasnet_node_t));
    if (rank == 0) {
      send_env(s, env_vars);
    }
    send_hostlist(s, size, hostlist + rank);
    send_ssh_argv(s);
  }

  /* Close listener and /dev/null */
  close(listener);
  close(devnull);
}

static void do_connect(int child_id) {
  struct sockaddr_in sock_addr;
  socklen_t addr_len;
  struct hostent *h = gethostbyname(parent.name);
  if (h == NULL) {
    gasneti_fatalerror("gethostbyname(%s) failed\n", parent.name);
  }
  if ((parent.sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    gasneti_fatalerror("parent.sock = socket() failed\n");
  }
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(parent.port);
  sock_addr.sin_addr = *(struct in_addr *)(h->h_addr_list[0]);
  addr_len = sizeof(sock_addr);
  if (connect(parent.sock, (struct sockaddr *)&sock_addr, addr_len) < 0) {
    gasneti_fatalerror("connect() failed\n");
  }
  (void)ioctl(parent.sock, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
  do_write(parent.sock, &child_id, sizeof(int));
  do_read(parent.sock, &myproc, sizeof(gasnet_node_t));
  do_read(parent.sock, &nproc, sizeof(gasnet_node_t));
  do_read(parent.sock, &tree_size, sizeof(gasnet_node_t));
  if (myproc == 0) {
    recv_env(parent.sock);
  }
  recv_hostlist(parent.sock, tree_size);
  recv_ssh_argv(parent.sock);
}

static pid_t spawn_one(const char *host, const char *argv0, int child_id, const char *myhost, const char *args)
{
  char *shell_cmd;
  pid_t pid;

  shell_cmd = sappendf(NULL, "cd %s && exec %s -slave %s %d %d %s",
			cwd, argv0, myhost, listen_port, child_id, args);
  pid = fork();
  if (pid < 0) {
    gasneti_fatalerror("fork() failed\n");
  } else if (pid == 0) {
    if (child_id >= 0) {
      if (dup2(STDIN_FILENO, devnull) < 0) {
        gasneti_fatalerror("dup2(STDIN_FILENO, /dev/null) failed\n");
      }
    }
    if (USE_LOCAL_SPAWN && !strcmp(host, myhost)) {
      execlp("sh", "sh", "-c", shell_cmd, NULL);
      gasneti_fatalerror("execlp(sh) failed\n");
    } else {
      ssh_argv[ssh_argc] = (/* noconst */ char *)host;
      ssh_argv[ssh_argc+1] = shell_cmd;
      execvp(ssh_argv[0], ssh_argv);
      gasneti_fatalerror("execvp(ssh) failed\n");
    }
  }
  gasneti_free(shell_cmd);

  return pid;
}

static void do_master(int argc, char **argv) {
  char myhost[1024];
  pid_t pid;
  int status;
  char *env_string;
  char *ssh_argv0;
  char **ssh_options = NULL;
  int i;

  if (gethostname(myhost, sizeof(myhost)) < 0) {
    gasneti_fatalerror("gethostname() failed\n");
  }

  if ((env_string = getenv(ENV_PREFIX "SSH_CMD")) != NULL && strlen(env_string)) {
    ssh_argv0 = env_string;
  } else {
    ssh_argv0 = gasneti_strdup("ssh");
  }
  if ((env_string = getenv(ENV_PREFIX "SSH_OPTIONS")) != NULL && strlen(env_string)) {
    /* XXX: no whitespace in SSH_OPTIONS */
    if ((ssh_options = list_to_array(env_string, SSH_OPTIONS_DELIM_CHARS)) != NULL) {
      while (ssh_options[ssh_argc]) ++ssh_argc;
    }
  }
  ++ssh_argc;

  /* Merge ssh config */
  ssh_argv = gasneti_calloc((ssh_argc + 3), sizeof(char *));
  ssh_argv[0] = ssh_argv0;
  if (ssh_argc > 1) {
    for (i=1; i<ssh_argc; ++i) {
      ssh_argv[i] = ssh_options[i-1];
    }
    gasneti_free(ssh_options);
  }

  build_hostlist();
  pre_spawn();

  pid = spawn_one(hostlist[0].name, argv[0], -1, myhost, stringify_args(argc-1, argv+1));
  if (pid <= 0) {
    gasneti_fatalerror("spawn_one(0) failed\n");
  }

  post_spawn(1);

  waitpid(pid, &status, 0);
  exit (WIFEXITED(status) ?  WEXITSTATUS(status) : -1);
}

static void do_slave(int child_id, int argc, char **argv)
{
  gasnet_node_t i, quot, rem;
  const char *args;
  int j;

  mypid = getpid();
  gasneti_reghandler(SIGURG, &sigurg_handler);

  /* Connect w/ parent to find out who we are */
  do_connect(child_id);

  if (tree_size < 2) {
    /* I have no children */
    return;
  }

  /* Map out children */
  quot = (tree_size - 1) / ARY;
  rem = (tree_size - 1) % ARY;
  for (i = myproc + 1, j = 0; i < (myproc + tree_size); j++) {
    gasnet_node_t tmp = quot;
    if (rem) {
      --rem;
      ++tmp;
    }
    child[j].rank = i;
    child[j].size = tmp;
    i += tmp;
    children++;
  }

  pre_spawn();
  args = stringify_args(argc-1, argv+1);
  gasneti_reghandler(SIGCHLD, &reaper);

  for (j = 0; j < children; ++j) {
    i = child[j].rank;
    child[j].pid = spawn_one(hostlist[i].name, argv[0], j, hostlist[myproc].name, args);
  }
  post_spawn(children);
}

static void usage(const char *argv0) {
  gasneti_fatalerror("usage: %s -master [-env list,of,env,vars,to,propagate] nproc [--] [ARGS...]\n", argv0);
}

/*----------------------------------------------------------------------------------------------*/

void gasnetc_bootstrapInit(int *argc_p, char ***argv_p, gasnet_node_t *nodes_p, gasnet_node_t *mynode_p) {
  int argc = *argc_p;
  char **argv = *argv_p;

  if (argc < 2) {
    usage(argv[0]);
  }

  if (strcmp(argv[1], "-slave") == 0) {
    /* Explicit slave process */
    int child_id;
    parent.name = argv[2];
    parent.port = atoi(argv[3]);
    child_id = atoi(argv[4]);
    argv[4] = argv[0];
    argc -= 4;
    argv += 4;
    do_slave(child_id, argc, argv);
  } else {
    int argi=1;
    if ((argi < argc) && (strcmp(argv[argi], "-master") == 0)) {
      /* Explicit master process */
      argi++;
    } else {
      /* Implicitly the master process */
    }
    if ((argi < argc) && (strcmp(argv[argi], "-env") == 0)) {
      env_vars = list_to_array(argv[argi+1], ",");
      argi += 2;
    }
    if (argi >= argc) {
      usage(argv[0]);
    }
    nproc = atoi(argv[argi++]);
    if (nproc < 1) {
      usage(argv[0]);
    }
    if ((argi < argc) && (strcmp(argv[argi], "--") == 0)) {
      argi++;
    }
    argv[argi-1] = argv[0];
    argc -= argi-1;
    argv += argi-1;
    do_master(argc, argv);
    /* Does not return */
  }

  *argc_p = argc;
  *argv_p = argv;
  *nodes_p = nproc;
  *mynode_p = myproc;
}

void gasnetc_bootstrapFini(void) {
  int j;

  for (j = 0; j < children; ++j) {
    while (child[j].pid) pause();
  }
}

void gasnetc_bootstrapAbort(int exitcode) {
  do_abort();
  /* NOT REACHED */
}

void gasnetc_bootstrapBarrier(void) {
  int j;
  char cmd;

  /* UP */
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, &cmd, sizeof(cmd));
    gasneti_assert(cmd == CMD_BARRIER_UP);
  }
  if (myproc) {
    cmd = CMD_BARRIER_UP;
    do_write(parent.sock, &cmd, sizeof(cmd));
  }

  /* DOWN */
  cmd = CMD_BARRIER_DOWN;
  if (myproc) {
    do_read(parent.sock, &cmd, sizeof(cmd));
    gasneti_assert(cmd == CMD_BARRIER_DOWN);
  }
  for (j = 0; j < children; ++j) {
    gasneti_assert(cmd == CMD_BARRIER_DOWN);
    do_write(child[j].sock, &cmd, sizeof(cmd));
  }
}

void gasnetc_bootstrapAllgather(void *src, size_t len, void *dest) {
  int j;

  /* Forward data up the tree */
  memcpy((char *)dest + len*myproc, src, len);
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, (char *)dest + len*child[j].rank, len*child[j].size);
  }
  if (myproc) {
    do_write(parent.sock, (char *)dest + len*myproc, tree_size*len);
  }

  /* Move data down, reducing traffic by sending
     only parts that a given node did not send to us */
  if (myproc) {
    gasnet_node_t next = myproc + tree_size;
    do_read(parent.sock, dest, len*myproc);
    do_read(parent.sock, (char *)dest + len*next, len*(nproc - next));
  }
  for (j = 0; j < children; ++j) {
    gasnet_node_t next = child[j].rank + child[j].size;
    do_write(child[j].sock, dest, len*child[j].rank);
    do_write(child[j].sock, (char *)dest + len*next, len*(nproc - next));
  }
}

void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest) {
  size_t row_len = len * nproc;
  char *tmp = gasneti_malloc(row_len * tree_size);
  int j;
                                                                                                              
  /* Collect rows from our subtree (gather) */
  memcpy(tmp, src, row_len);
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, tmp + row_len*(child[j].rank - myproc), row_len*child[j].size);
  }
  if (myproc) {
    do_write(parent.sock, tmp, row_len * tree_size);
  }

  /* Transpose at root, using dest for free temporary space */
  if (!myproc) {
    gasnet_node_t i;
    for (i = 0; i < nproc; ++i) {
      gasnet_node_t k;
      for (k = 0; k < i; ++k) {
	void *p = tmp + (i*row_len + k*len);
	void *q = tmp + (k*row_len + i*len);
        memcpy(dest, p, len);
        memcpy(p, q, len);
        memcpy(q, dest, len);
      }
    }
  }

  /* Move data back down (scatter) */
  if (myproc) {
    do_read(parent.sock, tmp, row_len * tree_size);
  }
  for (j = 0; j < children; ++j) {
    do_write(child[j].sock, tmp + row_len*(child[j].rank - myproc), row_len*child[j].size);
  }
  memcpy(dest, tmp, row_len);

  gasneti_free(tmp);
}

void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  int j;

  if (rootnode != 0) {
    /* Move up the tree to proc 0 */
    if (rootnode == myproc) {
      do_write(parent.sock, src, len);
    } else if ((rootnode > myproc) && (rootnode < (myproc + tree_size))) {
      /* Forward from child to parent */
      for (j = 0; (rootnode >= child[j].rank + child[j].size); ++j) {
	/* searching for proper child */
      }
      do_read(child[j].sock, dest, len);
      if (myproc) {
        do_write(parent.sock, dest, len);
      }
    }
  } else if (!myproc) {
    memcpy(dest, src, len);
  }

  /* Now move it down */
  if (myproc) {
    do_read(parent.sock, dest, len);
  }
  for (j = 0; j < children; ++j) {
    do_write(child[j].sock, dest, len);
  }
}

