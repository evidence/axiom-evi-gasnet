/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/ssh-spawner/gasnet_bootstrap_ssh.c,v $
 *     $Date: 2005/01/09 23:23:13 $
 * $Revision: 1.6 $
 * Description: GASNet ssh-based bootstrapper for vapi-conduit
 * Copyright 2004, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */
#include <gasnet.h>
#include <gasnet_internal.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#if HAVE_NETINET_TCP_H
  #include <netinet/tcp.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

/* NOTES

   This is a ssh-based (or rsh is you want) spawner for GASNet.  It is
   intended to be conduit-neutral.
  
   In the interest of scalability the ssh processes are started up in
   a balanced N-ary tree, where N is determined at compile-time (the
   value of "ARY", set below.  Typically we want this value to be
   resonably large, since deep trees would result in multiple steps
   of forwarding for standard I/O (which is performed entirely by the
   ssh processes at this point).

   The process corresponding to gasnet node zero is the root of this
   tree, but it also has a parent: the original process started by the
   user (or upcrun, etc.), the "master" process.  This allows for the
   possibility that the spawner is launched that is not one of the
   compute nodes.

   In addition to the tree of ssh connections, there is a TCP socket
   created between each process and its parent (including between the
   root and the master).  This socket is used for control information,
   during startup.  For instance, the the environment and arguments
   are transferred over this socket.

   The control sockets are used to send each process only a portion of
   the list of host names.  Rather than send the entire list, each processs
   receives only its own host name and that of any children it may have in
   the tree.

   The spawner is able to (in most cases) avoid orphaned processes
   by using TCP out-of-band data to generate a SIGURG.  The handler for
   this signal will send the out-of-band data both up and down the tree
   before exiting.

   If a child has the same hostname as its parent, it will be started
   directly, rather than via ssh.

   The tree structure is used to provide scalable implementations of
   the following "service" routines for use during the bootstrap, as
   required in the template-conduit:
      extern void gasnetc_bootstrapBarrier(void);
      extern void gasnetc_bootstrapExchange(void *src, size_t len, void *dest);
      extern void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode);
   
   Additionally, the following is useful (at least in vapi-conduit)
   for exchanging endpoint identifiers in a scalable manner:
      extern void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest);

   If demand exists, scalable Scatter and Gather are possible.

   The following are needed to handle startup and termination:
      extern void gasnetc_bootstrapInit(int *argc_p, char ***argv_p,
                                        gasnet_node_t *nodes_p,
                                        gasnet_node_t *mynode_p);
      extern void gasnetc_bootstrapFini(void);
      extern void gasnetc_bootstrapAbort(int exitcode);
   In the case of normal termination, all nodes should call
   gasnetc_bootstrapFini() before they call exit().  In the event that
   gasnet is unable to arrange for an orderly shutdown, a call to
   gasnetc_bootstrapAbort() will try to force all processes to exit
   with the given exit code.

   To deal with global environment propagation, one should
      #define GASNETI_CONDUIT_GETENV gasnetc_bootstrapGetenv
   A call to gasneti_setupGlobalEnvironment() is not required.
   A call to gasnetc_bootstrapGetenv() can safely be made at anytime,
   but will return NULL until after the call to gasnetc_bootstrapInit().

   To control the spawner, there are a few environment variables, all
   of which are processed only by the master process (which send the
   relavent information on to the others via the control sockets).
   Assuming ENV_PREFIX is #defined to "GASNET_" (the default):
     GASNET_SSH_CMD:
       The ssh (or rsh) command.  This should just be something like
       "ssh" or "/usr/local/bin/ssh2", without arguments.
     GASNET_SSH_OPTIONS:
       This is a string containing the "fixed" arguments to the ssh
       command.  This list will (currently) split on whitespace without
       regard to any quotes.  Keep in mind that for OpenSSH
         -o Option=Value
       works fine, even though the docs lead one to want to use
         -o 'Option Value'
     GASNET_SSH_NODEFILE:
       If this variable is set, it is taken to be a file containing
       hostnames (or IPs), one per line.  Leading whitespace is OK.
       The hostname ends at the first whitespace character (or end
       of line), and anything remaining on the line (such as a CPU
       count) will be ignored.  Comment lines beginning with '#' are
       ignored.
     GASNET_SSH_HOSTS:
       Only if GASNET_SSH_NODEFILE is NOT set, this variable will be taken
       as a list of hosts.  Legal delimiters for the list include commas
       and whitespace (among others).

   XXX: still to do
   + Implement "-N" (as with prun) so NODES and PROCS are independent.
   + Should consider allowing quoted whitespace in SSH_OPTIONS.  We really
     don't want to invoke system() because that leads to all sorts of
     possible questions about proper amounts of quoting.
   + Should look carefully at udp-conduit for hints on arguments to ssh,
     especially for OpenSSH.
   + Implement "custom" spawner in the spirit of udp-conduit.
   + Look at udp-conduit for things missing from this list. :-)
   + We probably leak small strings in a few places.

 */

#define ARY 24
#define USE_LOCAL_SPAWN 1

#ifndef USE_LOCAL_SPAWN
  #define USE_LOCAL_SPAWN 0
#endif

#define WHITESPACE " \t\n\r"
#define SSH_SERVERS_DELIM_CHARS  ",/;:" WHITESPACE

extern char **environ;
#ifndef ENV_PREFIX
  #define ENV_PREFIX "GASNET_"
#endif

static gasnet_node_t nproc = (gasnet_node_t)(-1L);
static gasnet_node_t myproc = (gasnet_node_t)(-1L);
static gasnet_node_t tree_size = (gasnet_node_t)(-1L);
static char cwd[1024];
static int mypid;
static int rootpid = 0;		/* only on master */
static int children = 0;	/* only on slaves */
static int devnull = -1;
static int listener = -1;
static int listen_port = -1;
static char **hostlist;
static char **ssh_argv = NULL;
static char *master_env = NULL;
static size_t master_env_len = 0;
static int ssh_argc;
static int parent = -1; /* socket */
static struct {
  volatile pid_t	pid;	/* pid of ssh (reset to zero by reaper) */
  gasnet_node_t		rank;
  gasnet_node_t		size;	/* size of subtree rooted at this child */
  int			sock;
} child[ARY];

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

/* master forwards signals to root */
static void sigforward(int sig)
{
  gasneti_reghandler(sig, &sigforward);
  if (rootpid) {
    kill(rootpid, sig);
  }
}

static void reaper(int sig)
{
  pid_t pid;

  gasneti_reghandler(sig, &reaper);

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

static void do_abort(unsigned char exitcode) GASNET_NORETURN;
static void do_abort(unsigned char exitcode)
{
  int j;

  gasneti_reghandler(SIGURG, SIG_IGN);

  send(parent, &exitcode, 1, MSG_OOB);
  for (j = 0; j < children; ++j) {
    send(child[j].sock, &exitcode, 1, MSG_OOB);
  }

  _exit(exitcode);

  gasneti_reghandler(SIGABRT, SIG_DFL);
  abort();
  /* NOT REACHED */
}

static void sigurg_handler(int sig)
{
  unsigned char exitcode = 255;
  int j;

  /* We need to read our single byte of urgent data here.
   * Since we don't know which socket sent it, we just
   * try them all.  MSG_OOB is supposed to always be
   * non-blocking.  If multiple sockets have OOB data
   * pending, we don't care which one we read.
   */
  (void)recv(parent, &exitcode, 1, MSG_OOB);
  for (j = 0; j < children; ++j) {
    (void)recv(child[j].sock, &exitcode, 1, MSG_OOB);
  }
  do_abort(exitcode);
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

static char *do_read_string(int fd) {
  char *result = NULL;
  size_t len;

  do_read(fd, &len, sizeof(size_t));
  if (len) {
    result = gasneti_malloc(len + 1);
    do_read(fd, result, len);
    result[len] = '\0';
  }

  return result;
}

/* Add single quotes around a string, taking care of any existing quotes */
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

/* Build an array of strings from a list */
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

  hostlist = gasneti_malloc(nproc * sizeof(char *));

  if ((env_string = getenv(ENV_PREFIX "SSH_NODEFILE")) != NULL && strlen(env_string)) {
    FILE *fp;

    fp = fopen(env_string, "r");
    if (!fp) {
      gasneti_fatalerror("fopen(%s)\n", env_string);
    }

    for (i = 0; i < nproc;) {
      static char buf[1024];
      char *p;

      if (!fgets(buf, sizeof(buf), fp)) {
	/* ran out of lines */
        gasneti_fatalerror("Out of hosts in file %s\n", env_string);
      }
 
      p = buf;
      while (*p && strchr(WHITESPACE, *p)) ++p; /* eat leading whitespace */
      if (*p != '#') {
        p[strcspn(p, WHITESPACE)] = '\0';
        hostlist[i] = gasneti_strdup(p);
	++i;
      }
    }

    fclose(fp);
  } else if ((env_string = getenv(ENV_PREFIX "SSH_SERVERS")) != NULL && strlen(env_string)) {
    hostlist = list_to_array(env_string, SSH_SERVERS_DELIM_CHARS);
    for (i = 0; i < nproc; ++i) {
      if (hostlist[i] == NULL) {
        gasneti_fatalerror("Out of hosts in SSH_SERVERS\n");
      }
    }
  } else {
    gasneti_fatalerror("No " ENV_PREFIX "SSH_NODEFILE or " ENV_PREFIX "SSH_SERVERS in environment\n");
  }
}

static void send_hostlist(int s, int count, char * const * list) {
  gasnet_node_t i;

  /* length of list is already known to the recipient */
  for (i = 0; i < count; ++i) {
    do_write_string(s, list[i]);
  }
}

static void recv_hostlist(int s, int count) {
  gasnet_node_t i;

  hostlist = gasneti_malloc(count * sizeof(char *));
  for (i = 0; i < count; ++i) {
    hostlist[i] = do_read_string(s);
  }
  hostlist -= myproc; /* offset for ease of indexing */
}

/*
 * Send environment as a big char[] with \0 between each 'VAR=VAL'
 * and a double \0 to terminate. (inspired by amudp)
 */
static void send_env(int s) {
  if (!master_env) {
    int i;
    const char *p;
    char *q;
    size_t rlen = strlen(ENV_PREFIX "SSH_");
    size_t count;

    gasneti_assert(rootpid); /* == we are master */

    /* First pass over environment to get its size */
    master_env_len = 1; /* for the doubled \0 at the end */
    for (i = 0, p = environ[0]; p != NULL; p = environ[++i]) {
      if (!strncmp(ENV_PREFIX "SSH_", p, rlen)) {
        /* We parse these ourselves, don't forward */
      } else {
        master_env_len += strlen(p) + 1;
      }
    }

    /* Append all the strings together */
    q = master_env = gasneti_malloc(master_env_len);
    for (i = 0, p = environ[0]; p != NULL; p = environ[++i]) {
      if (!strncmp(ENV_PREFIX "SSH_", p, rlen)) {
        /* We parse these ourselves, don't forward */
      } else {
        size_t tmp = strlen(p) + 1;
        memcpy(q, p, tmp);
        q += tmp;
      }
    }
    *q = '\0';
  }

  /* send it */
  do_write(s, &master_env_len, sizeof(master_env_len));
  do_write(s, master_env, master_env_len);
}

static void recv_env(int s) {
  do_read(s, &master_env_len, sizeof(master_env_len));
  master_env = gasneti_malloc(master_env_len);
  do_read(s, master_env, master_env_len);
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
    ssh_argv[i] = do_read_string(s);
  }
}

static void send_argv(int s, int argc, char * const *argv) {
  int i;

  do_write(s, &argc, sizeof(int));
  for (i = 0; i < argc; ++i) {
    do_write_string(s, argv[i]);
  }
}

static void recv_argv(int s, int *argc_p, char ***argv_p) {
  int argc, i;
  char **argv;

  do_read(s, &argc, sizeof(int));
  argv = gasneti_calloc(argc+1, sizeof(char **));
  for (i = 0; i < argc; ++i) {
    argv[i] = do_read_string(s);
  }
  argv[argc] = NULL;

  *argc_p = argc;
  *argv_p = argv;
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

static void post_spawn(int count, int argc, char * const *argv) {
  /* Accept count connections */
  while (count--) {
    gasnet_node_t size, rank;
    struct sockaddr_in sock_addr;
    socklen_t addr_len;
    static const int one = 1;
    int id;
    int s;

    if ((s = accept(listener, (struct sockaddr *)&sock_addr, &addr_len)) < 0) {
      gasneti_fatalerror("accept() failed\n");
    }
    (void)ioctl(s, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
    (void)setsockopt(parent, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one));
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
    send_env(s);
    send_hostlist(s, size, hostlist + rank);
    send_ssh_argv(s);
    send_argv(s, argc, argv);
  }

  /* Close listener and /dev/null */
  close(listener);
  close(devnull);

  /* Free the hostlist and ssh_argv */
  if (myproc != (gasnet_node_t)(-1L)) {
    gasnet_node_t i;
    int j;

    hostlist += myproc; /* undo the offset */
    for (i = 0; i < tree_size; ++i) {
      gasneti_free(hostlist[i]);
    }
    gasneti_free(hostlist);

    for (j = 0; j < ssh_argc; ++j) {
      gasneti_free(ssh_argv[j]);
    }
    gasneti_free(ssh_argv);
  }
}

static void do_connect(int child_id, const char *parent_name, int parent_port, int *argc_p, char ***argv_p) {
  struct sockaddr_in sock_addr;
  socklen_t addr_len;
  static const int one = 1;
  struct hostent *h = gethostbyname(parent_name);

  if (h == NULL) {
    gasneti_fatalerror("gethostbyname(%s) failed\n", parent_name);
  }
  if ((parent = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    gasneti_fatalerror("parent = socket() failed\n");
  }
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(parent_port);
  sock_addr.sin_addr = *(struct in_addr *)(h->h_addr_list[0]);
  addr_len = sizeof(sock_addr);
  if (connect(parent, (struct sockaddr *)&sock_addr, addr_len) < 0) {
    gasneti_fatalerror("connect() failed\n");
  }
  (void)ioctl(parent, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
  (void)setsockopt(parent, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one));
  do_write(parent, &child_id, sizeof(int));
  do_read(parent, &myproc, sizeof(gasnet_node_t));
  do_read(parent, &nproc, sizeof(gasnet_node_t));
  do_read(parent, &tree_size, sizeof(gasnet_node_t));
  recv_env(parent);
  recv_hostlist(parent, tree_size);
  recv_ssh_argv(parent);
  recv_argv(parent, argc_p, argv_p);
}

static pid_t spawn_one(const char *host, const char *argv0, int child_id, const char *myhost)
{
  pid_t pid;

  pid = fork();
  if (pid < 0) {
    gasneti_fatalerror("fork() failed\n");
  } else if (pid == 0) {
    /* For all children except the root do </dev/null */
    if (child_id >= 0) {
      if (dup2(STDIN_FILENO, devnull) < 0) {
        gasneti_fatalerror("dup2(STDIN_FILENO, /dev/null) failed\n");
      }
    }
    if (USE_LOCAL_SPAWN && !strcmp(host, myhost)) {
      execlp(argv0, argv0, "-slave", myhost,
	     sappendf(NULL, "%d", listen_port),
	     sappendf(NULL, "%d", child_id), NULL);
      gasneti_fatalerror("execlp(sh) failed\n");
    } else {
      ssh_argv[ssh_argc] = (/* noconst */ char *)host;
      ssh_argv[ssh_argc+1] = sappendf(NULL, "cd %s; exec %s -slave %s %d %d",
				      quote_arg(cwd), quote_arg(argv0),
				      myhost, listen_port, child_id);
      execvp(ssh_argv[0], ssh_argv);
      gasneti_fatalerror("execvp(ssh) failed\n");
    }
  }

  return pid;
}

static void do_master(int argc, char **argv) GASNET_NORETURN;
static void do_master(int argc, char **argv) {
  char myhost[1024];
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
    if ((ssh_options = list_to_array(env_string, WHITESPACE)) != NULL) {
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

  /* Arrange to forward termination signals */
  gasneti_reghandler(SIGQUIT, &sigforward);
  gasneti_reghandler(SIGINT,  &sigforward);
  gasneti_reghandler(SIGTERM, &sigforward);
  gasneti_reghandler(SIGHUP,  &sigforward);
  gasneti_reghandler(SIGPIPE, &sigforward);

  rootpid = spawn_one(hostlist[0], argv[0], -1, myhost);
  if (rootpid <= 0) {
    gasneti_fatalerror("spawn_one(root) failed\n");
  }

  post_spawn(1, argc, argv);

  waitpid(rootpid, &status, 0);
  exit (WIFEXITED(status) ?  WEXITSTATUS(status) : -1);
}

static void do_slave(int child_id, const char *parent_name, int parent_port, int *argc_p, char ***argv_p)
{
  gasnet_node_t i, quot, rem;
  const char *args;
  int j;

  mypid = getpid();
  gasneti_reghandler(SIGURG, &sigurg_handler);

  /* Connect w/ parent to find out who we are */
  do_connect(child_id, parent_name, parent_port, argc_p, argv_p);

  /* Start any children */
  if (tree_size > 1) {
    /* Map out the subtrees */
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
  
    /* Spawn them */
    pre_spawn();
    gasneti_reghandler(SIGCHLD, &reaper);
    for (j = 0; j < children; ++j) {
      i = child[j].rank;
      child[j].pid = spawn_one(hostlist[i], (*argv_p)[0], j, hostlist[myproc]);
    }
    post_spawn(children, *argc_p, *argv_p);
  }
}

static void usage(const char *argv0) {
  gasneti_fatalerror("usage: %s [-master] NPROC [--] [ARGS...]\n", argv0);
}

/*----------------------------------------------------------------------------------------------*/

/* gasnetc_bootstrapInit
 *
 * Upon return:
 *   + All processes have been spawned
 *   + argc and argv are those the user specified
 *   + *nodes_p and *mynode_p are set
 *   + the global environment is available via gasnetc_bootstrapGetenv()
 */
void gasnetc_bootstrapInit(int *argc_p, char ***argv_p, gasnet_node_t *nodes_p, gasnet_node_t *mynode_p) {
  int argc = *argc_p;
  char **argv = *argv_p;

  if (argc < 2) {
    usage(argv[0]);
  }

  if (strcmp(argv[1], "-slave") == 0) {
    /* Explicit slave process */
    int child_id;
    const char *parent_name = argv[2];
    int parent_port = atoi(argv[3]);
    child_id = atoi(argv[4]);
    do_slave(child_id, parent_name, parent_port, argc_p, argv_p);
  } else {
    int argi=1;
    if ((argi < argc) && (strcmp(argv[argi], "-master") == 0)) {
      /* Explicit master process */
      argi++;
    } else {
      /* Implicitly the master process */
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

  *nodes_p = nproc;
  *mynode_p = myproc;
}

/* gasnetc_bootstrapFini
 *
 * Waits for children to exit.
 */
void gasnetc_bootstrapFini(void) {
  int j;

  for (j = 0; j < children; ++j) {
    while (child[j].pid) pause();
  }
}

/* gasnetc_bootstrapAbort
 *
 * Force immediate (abnormal) termination.
 */
void gasnetc_bootstrapAbort(int exitcode) {
  do_abort((unsigned char)exitcode);
  /* NOT REACHED */
}

void gasnetc_bootstrapBarrier(void) {
  static const int zero = 0;
  static const int one = 1;
  int cmd, j;

  /* UP */
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, &cmd, sizeof(cmd));
    gasneti_assert(cmd == zero);
  }
  if (myproc) {
    do_write(parent, &zero, sizeof(zero));
  }

  /* DOWN */
  if (myproc) {
    do_read(parent, &cmd, sizeof(cmd));
    gasneti_assert(cmd == one);
  }
  for (j = 0; j < children; ++j) {
    do_write(child[j].sock, &one, sizeof(one));
  }
}

void gasnetc_bootstrapExchange(void *src, size_t len, void *dest) {
  int j;

  /* Forward data up the tree */
  memcpy((char *)dest + len*myproc, src, len);
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, (char *)dest + len*child[j].rank, len*child[j].size);
  }
  if (myproc) {
    do_write(parent, (char *)dest + len*myproc, tree_size*len);
  }

  /* Move data down, reducing traffic by sending
     only parts that a given node did not send to us */
  if (myproc) {
    gasnet_node_t next = myproc + tree_size;
    do_read(parent, dest, len*myproc);
    do_read(parent, (char *)dest + len*next, len*(nproc - next));
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
    do_write(parent, tmp, row_len * tree_size);
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
    do_read(parent, tmp, row_len * tree_size);
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
      do_write(parent, src, len);
    } else if ((rootnode > myproc) && (rootnode < (myproc + tree_size))) {
      /* Forward from child to parent */
      for (j = 0; (rootnode >= child[j].rank + child[j].size); ++j) {
	/* searching for proper child */
      }
      do_read(child[j].sock, dest, len);
      if (myproc) {
        do_write(parent, dest, len);
      }
    }
  } else if (!myproc) {
    memcpy(dest, src, len);
  }

  /* Now move it down */
  if (myproc) {
    do_read(parent, dest, len);
  }
  for (j = 0; j < children; ++j) {
    do_write(child[j].sock, dest, len);
  }
}

/* gasnetc_bootstrapGetenv
 *
 * Fetch a variable from the environment on the master node.
 * (more or less copied from amudp_spmd.cpp)
 */
const char *gasnetc_bootstrapGetenv(const char *var) {
  if (master_env && var && (*var != '\0')) {
    char *p = master_env;
    size_t len = strlen(var);

    while (*p) {
      if (!strncmp(var, p, len) && (p[len] == '=')) {
        return p + len + 1;
      } else {
        p += strlen(p) + 1;
      }
    }
  }
  return NULL;
}
