/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/ssh-spawner/gasnet_bootstrap_ssh.c,v $
 *     $Date: 2005/01/14 22:09:20 $
 * $Revision: 1.13 $
 * Description: GASNet ssh-based bootstrapper for vapi-conduit
 * Copyright 2004, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */
#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

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
   value of "OUT_DEGREE", set below.  Typically we want this value to
   be resonably large, since deep trees would result in multiple steps
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
       command.  It is parsed while treating \, ' and " in the same
       way as a shell.
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
   + Consider making OUT_DEGREE a runtime variable.
   + Should look carefully at udp-conduit for hints on arguments to ssh,
     especially for OpenSSH.
   + Implement "custom" spawner in the spirit of udp-conduit.
   + Look at udp-conduit for things missing from this list. :-)
   + We probably leak small strings in a few places.

 */

#define OUT_DEGREE 32
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

/* Master & slaves */
  static int is_master = 0;
  static int is_verbose = 0;
  static gasnet_node_t nproc = 0;
  static char cwd[1024];
  static int devnull = -1;
  static int listener = -1;
  static int listen_port = -1;
  static char **nodelist;
  static char **ssh_argv = NULL;
  static int ssh_argc = 0;
  static char *master_env = NULL;
  static size_t master_env_len = 0;
  static struct child {
    int			sock;
    volatile pid_t	pid;	/* pid of ssh (reset to zero by reaper) */
    gasnet_node_t	rank;
    gasnet_node_t	procs;	/* size in procs of subtree rooted at this child */
    gasnet_node_t	nodes;	/* size in nodes of subtree rooted at this child */
    char **		nodelist;
  } *child = NULL;
  static int children = 0;
  static volatile int accepted = 0;
/* Slaves only */
  static gasnet_node_t myproc = (gasnet_node_t)(-1L);
  static gasnet_node_t tree_procs = (gasnet_node_t)(-1L);
  static gasnet_node_t tree_nodes = (gasnet_node_t)(-1L);
  static int parent = -1; /* socket */
  static int mypid;
/* Master only */
  static volatile int exit_status = -1;
  static gasnet_node_t nnodes = 0;	/* nodes, as distinct from procs */

static void do_verbose(const char *fmt, ...) __attribute__((__format__ (__printf__, 1, 2)));
static void do_verbose(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fflush(stderr);
  va_end(args);
}
#define BOOTSTRAP_VERBOSE(ARGS)		if_pf (is_verbose) do_verbose ARGS

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

/* master forwards signals to root if possible */
static void sigforward(int sig)
{
  int sent = 0;
  pid_t root_pid = child ? child[0].pid : 0;
  gasneti_assert(is_master);

  if (root_pid) {
    BOOTSTRAP_VERBOSE(("Master forwarding signal %d\n", sig));
    gasneti_reghandler(sig, &sigforward);
    sent = (kill(root_pid, sig) == 0);
  }
  if (!sent) {
    BOOTSTRAP_VERBOSE(("Master resending signal %d to self\n", sig));
    gasneti_reghandler(sig, SIG_DFL);
    raise(sig);
  }
}

static void reaper(int sig)
{
  pid_t pid;
  int status;

  gasneti_reghandler(sig, &reaper);
  while((pid = waitpid(-1,&status,WNOHANG)) > 0) {
    if (child) {
      int j;
      for (j = 0; j < children; ++j) {
        if (pid == child[j].pid) {
	  child[j].pid = 0;
	  if (child[j].rank == 0 && WIFEXITED(status)) {
	    exit_status = WEXITSTATUS(status);
	  }
	  break;
        }
      }
    }
  }

  if (accepted < children) {
    gasneti_fatalerror("One or more processes died before setup was completed");
  }
}

static void do_oob(unsigned char exitcode) {
  const int flags = MSG_OOB 
#ifdef MSG_DONTWAIT
	  		| MSG_DONTWAIT
#endif
#ifdef MSG_NOSIGNAL
			| MSG_NOSIGNAL
#endif
			;
  int j;

  send(parent, &exitcode, 1, flags);
  if (child) {
    for (j = 0; j < children; ++j) {
      send(child[j].sock, &exitcode, 1, flags);
    }
  }
}

static void do_abort(unsigned char exitcode) GASNET_NORETURN;
static void do_abort(unsigned char exitcode) {
  gasneti_reghandler(SIGURG, SIG_IGN);
  do_oob(exitcode);
  _exit(exitcode);

  /* paranoia... */
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
  if (child) {
    for (j = 0; j < children; ++j) {
      (void)recv(child[j].sock, &exitcode, 1, MSG_OOB);
    }
  }

  do_abort(exitcode);
  /* NOT REACHED */
}

static void do_write(int fd, const void *buf, size_t len)
{
  const char *p = (const char *)buf;
  while (len) {
    ssize_t rc = write(fd, p, len);
    if_pf (rc <= 0) {
      do_oob(255);
      if (rc == 0) {
        gasneti_fatalerror("unexpected zero return from write(ctrl_socket)");
      } else {
        gasneti_fatalerror("write(ctrl_socket) returned errno=%d", errno);
      }
    }
    p += rc;
    len -= rc;
  }
}

static void do_write_string(int fd, const char *string) {
  size_t len = string ? strlen(string) : 0;
  do_write(fd, &len, sizeof(len));
  do_write(fd, string, len);
}

static void do_read(int fd, void *buf, size_t len)
{
  char *p = (char *)buf;
  while (len) {
    ssize_t rc = read(fd, p, len);
    if_pf (rc <= 0) {
      do_oob(255);
      if (rc == 0) {
        gasneti_fatalerror("unexpected EOF from read(ctrl_socket)");
      } else {
        gasneti_fatalerror("read(ctrl_socket) returned errno=%d", errno);
      }
    }
    p += rc;
    len -= rc;
  }
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

static int options_helper(char **list, const char *string, const char *where)
{
  int count = 0;
  int in_quotes = 0;
  const char *special[] = {WHITESPACE "\\\"'",	/* special chars outside dbl quotes */
			   "\\\""};		/* special chars inside dbl quotes */
  
  if (!string) {
    return 0;
  }

  /* Outer loop adds a word to the list on each pass
     with the possible exception of the last
   */
  while (*string) {
    char tmp[1024];
    char *p = tmp;
    while (*string && strchr(WHITESPACE,*string)) ++string; /* eat leading whitespace */
    if (!*string) { break; /* reached end of string */ }
    /* This loop brings together pieces of a "word", possible w/ quotes */
    in_quotes = 0;
    do {
      int i = strcspn(string, special[in_quotes]);
      memcpy(p , string, i); p += i;
      gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
      string += i;
      switch (*string) {
	case '\0':
	  break;
	case '\\':
	  if (!string[1]) {
	    gasneti_fatalerror("string ends with \\ %s", where);
	  } else if (strchr(special[in_quotes],string[1])) {
	    /* Drop the backslash if it quotes a special character */
            *(p++) = string[1];
            gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
	  } else {
	    /* Keep the backslash */
	    memcpy(p , string, 2); p += 2;
            gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
	  }
	  string += 2;
	  break;
	case '\'':
	  ++string;
	  i = strcspn(string, "\'");
	  if (string[i] != '\'') {
	    gasneti_fatalerror("unbalanced ' %s", where);
	  }
          memcpy(p , string, i); p += i;
          gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
	  string += i + 1;
	  break;
	case '"':
	  ++string;
	  in_quotes = !in_quotes;
	  break;
	default: /* WHITESPACE */
	  break;
      }
    } while (*string && (in_quotes || !strchr(WHITESPACE,*string)));
    if (in_quotes) {
      gasneti_fatalerror("unbalanced \" %s", where);
    }
    if (list) {
      gasneti_assert((uintptr_t)(p-tmp) < sizeof(tmp));
      *p = '\0';
      list[count] = strdup(tmp);
    }
    ++count;
  }
  if (list) {
    list[count] = NULL;
  }
  return count;
}

/* Parse a string into an array of "words", following shell rules for '," and \ */
static char **parse_options(const char *string, int *count_p, const char *where)
{
  int count;
  char **list;

  /* First parse pass will just count the words */
  count = options_helper(NULL, string, where);
  list = gasneti_malloc(sizeof(char *) * (count+1));

  /* Second pass fills the list of words */
  (void)options_helper(list, string, where);

  if (count_p) *count_p = count;
  return list;
}

static void configure_ssh(void) {
  char *env_string;
  char *ssh_argv0;
  char **ssh_options = NULL;
  int i;

  BOOTSTRAP_VERBOSE(("Parsing environment for ssh command line\n"));
  if ((env_string = getenv(ENV_PREFIX "SSH_CMD")) != NULL && strlen(env_string)) {
    ssh_argv0 = env_string;
  } else {
    ssh_argv0 = gasneti_strdup("ssh");
  }
  BOOTSTRAP_VERBOSE(("\t|%s", ssh_argv0));
  if ((env_string = getenv(ENV_PREFIX "SSH_OPTIONS")) != NULL && strlen(env_string)) {
    ssh_options = parse_options(env_string, &ssh_argc, "while parsing " ENV_PREFIX "SSH_OPTIONS");
  }
  ++ssh_argc;
  ssh_argv = gasneti_calloc((ssh_argc + 3), sizeof(char *));
  ssh_argv[0] = ssh_argv0;
  if (ssh_argc > 1) {
    for (i=1; i<ssh_argc; ++i) {
      ssh_argv[i] = ssh_options[i-1];
      BOOTSTRAP_VERBOSE(("|%s", ssh_argv[i]));
    }
    gasneti_free(ssh_options);
  }
  BOOTSTRAP_VERBOSE(("|\n"));
}

/* Build an array of hostnames from a file */
static char ** parse_nodefile(const char *filename) {
  char **result = NULL;
  gasnet_node_t i;
  FILE *fp;

  BOOTSTRAP_VERBOSE(("Parsing nodefile '%s'\n", filename));
  fp = fopen(filename, "r");
  if (!fp) {
    gasneti_fatalerror("failed to open nodefile '%s'", filename);
  }

  result = gasneti_malloc(nnodes * sizeof(char *));
  for (i = 0; i < nnodes;) {
    static char buf[1024];
    char *p;

    if (!fgets(buf, sizeof(buf), fp)) {
      /* ran out of lines */
      gasneti_fatalerror("Out of lines in nodefile '%s'", filename);
    }
 
    p = buf;
    while (*p && strchr(WHITESPACE, *p)) ++p; /* eat leading whitespace */
    if (*p != '#') {
      p[strcspn(p, WHITESPACE)] = '\0';
      result[i] = gasneti_strdup(p);
      ++i;
      BOOTSTRAP_VERBOSE(("\t'%s'\n", p));
    }
  }

  (void)fclose(fp);

  return result;
}

/* Build an array of hostnames from a delimited string */
static char ** parse_servers(const char *list) {
  static const char *delims = SSH_SERVERS_DELIM_CHARS;
  char **result = NULL;
  char *string, *alloc;
  gasnet_node_t i;

  alloc = string = gasneti_strdup(list);
  result = gasneti_malloc(nnodes * sizeof(char *));
  BOOTSTRAP_VERBOSE(("Parsing servers list '%s'\n", string));
  for (i = 0; i < nnodes; ++i) {
    char *p;
    while (*string && strchr(delims,*string)) ++string; /* eat leading delimiters */
    if (!*string) {
      gasneti_fatalerror("Too few hosts in " ENV_PREFIX "SSH_SERVERS");
    }
    p = string;
    string += strcspn(string, delims);
    if (*string) *(string++) = '\0';
    result[i] = strdup(p);
    BOOTSTRAP_VERBOSE(("\t'%s'\n", result[i]));
  }
  gasneti_free(alloc);

  return result;
}

static void build_nodelist(void)
{
  const char *env_string;

  if (nproc < nnodes) {
    fprintf(stderr, "Warning: %d nodes is larger than %d processes, nodes reduced to %d\n", nnodes, nproc, nproc);
    nnodes = nproc;
  }

  if ((env_string = getenv(ENV_PREFIX "SSH_NODEFILE")) != NULL && strlen(env_string)) {
    nodelist = parse_nodefile(env_string);
  } else if ((env_string = getenv(ENV_PREFIX "SSH_SERVERS")) != NULL && strlen(env_string)) {
    nodelist = parse_servers(env_string);
  } else {
    gasneti_fatalerror("No " ENV_PREFIX "SSH_NODEFILE or " ENV_PREFIX "SSH_SERVERS in environment");
  }
}

static void send_nodelist(int s, int count, char ** list) {
  gasnet_node_t i;

  /* length of list is already known to the recipient */
  for (i = 0; i < count; ++i) {
    do_write_string(s, list[i]);
  }
}

static void recv_nodelist(int s, int count) {
  if (count) {
    gasnet_node_t i;

    nodelist = gasneti_malloc(count * sizeof(char *));
    for (i = 0; i < count; ++i) {
      nodelist[i] = do_read_string(s);
    }
  }
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

    gasneti_assert(is_master);

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

static void pre_spawn(count) {
  struct sockaddr_in sock_addr;
  socklen_t addr_len;

  /* Get the cwd */
  if (!getcwd(cwd, sizeof(cwd))) {
    gasneti_fatalerror("getcwd() failed");
  }

  /* Open /dev/null */
  devnull = open("/dev/null", O_RDONLY);
  if (devnull < 0) {
    gasneti_fatalerror("open(/dev/null) failed");
  }

  /* Create listening socket */
  if ((listener = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    gasneti_fatalerror("listener = socket() failed");
  }
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = 0;
  sock_addr.sin_addr.s_addr = INADDR_ANY;
  addr_len = sizeof(sock_addr);
  if (bind(listener, (struct sockaddr *)&sock_addr, addr_len) < 0) {
    gasneti_fatalerror("bind() failed");
  }
  if (listen(listener, count) < 0) {
    gasneti_fatalerror("listen() failed");
  }
  if (getsockname(listener, (struct sockaddr *)&sock_addr, &addr_len) < 0) {
    gasneti_fatalerror("getsockname() failed");
  }
  listen_port = ntohs(sock_addr.sin_port);

  /* Prepare to reap fallen children */
  gasneti_reghandler(SIGCHLD, &reaper);
}

static void post_spawn(int count, int argc, char * const *argv) {
  /* Accept count connections */
  while (count--) {
    struct sockaddr_in sock_addr;
    socklen_t addr_len;
    static const int one = 1;
    gasnet_node_t child_id;
    struct child *ch = NULL;
    int s;

    if ((s = accept(listener, (struct sockaddr *)&sock_addr, &addr_len)) < 0) {
      gasneti_fatalerror("accept() failed");
    }
    (void)ioctl(s, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one));
    do_read(s, &child_id, sizeof(gasnet_node_t));
    gasneti_assert(child_id < children);
    ch = &(child[child_id]);
    child[child_id].sock = s;
    gasneti_assert(ch->rank < nproc);
    gasneti_assert(ch->procs > 0);
    gasneti_assert(ch->procs <= nproc);
    do_write(s, &ch->rank, sizeof(gasnet_node_t));
    do_write(s, &nproc, sizeof(gasnet_node_t));
    do_write(s, &ch->procs, sizeof(gasnet_node_t));
    do_write(s, &ch->nodes, sizeof(gasnet_node_t));
    send_env(s);
    send_nodelist(s, ch->nodes, ch->nodelist);
    send_ssh_argv(s);
    send_argv(s, argc, argv);
    ++accepted;
  }

  /* Close listener and /dev/null */
  close(listener);
  close(devnull);

  /* Free the nodelist and ssh_argv */
  if (myproc != (gasnet_node_t)(-1L)) {
    gasnet_node_t i;
    int j;

    for (i = 0; i < tree_nodes; ++i) {
      gasneti_free(nodelist[i]);
    }
    gasneti_free(nodelist);

    for (j = 0; j < ssh_argc; ++j) {
      gasneti_free(ssh_argv[j]);
    }
    gasneti_free(ssh_argv);
  }
}

static void do_connect(gasnet_node_t child_id, const char *parent_name, int parent_port, int *argc_p, char ***argv_p) {
  struct sockaddr_in sock_addr;
  socklen_t addr_len;
  static const int one = 1;
  struct hostent *h = gethostbyname(parent_name);

  gasneti_assert(!is_master);

  if (h == NULL) {
    gasneti_fatalerror("gethostbyname(%s) failed", parent_name);
  }
  if ((parent = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    gasneti_fatalerror("parent = socket() failed");
  }
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(parent_port);
  sock_addr.sin_addr = *(struct in_addr *)(h->h_addr_list[0]);
  addr_len = sizeof(sock_addr);
  if (connect(parent, (struct sockaddr *)&sock_addr, addr_len) < 0) {
    gasneti_fatalerror("connect(host=%s, port=%d) failed w/ errno=%d", parent_name, parent_port, errno);
  }
  (void)ioctl(parent, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
  (void)setsockopt(parent, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one));
  do_write(parent, &child_id, sizeof(gasnet_node_t));
  do_read(parent, &myproc, sizeof(gasnet_node_t));
  do_read(parent, &nproc, sizeof(gasnet_node_t));
  do_read(parent, &tree_procs, sizeof(gasnet_node_t));
  do_read(parent, &tree_nodes, sizeof(gasnet_node_t));
  gasneti_assert(nproc > 0);
  gasneti_assert(myproc < nproc);
  gasneti_assert(tree_procs > 0);
  gasneti_assert(tree_procs <= nproc);
  recv_env(parent);
  recv_nodelist(parent, tree_nodes);
  recv_ssh_argv(parent);
  recv_argv(parent, argc_p, argv_p);
  BOOTSTRAP_VERBOSE(("Process %d connected\n", myproc));
}

static void spawn_one(const char *argv0, gasnet_node_t child_id, const char *myhost) {
  const char *host = child[child_id].nodelist ? child[child_id].nodelist[0] : NULL;
  pid_t pid;


  child[child_id].pid = pid = fork();
  if (pid < 0) {
    gasneti_fatalerror("fork() failed");
  } else if (pid == 0) {
    /* For all children except the root do </dev/null */
    if (child[child_id].rank != 0) {
      if (dup2(STDIN_FILENO, devnull) < 0) {
        gasneti_fatalerror("dup2(STDIN_FILENO, /dev/null) failed");
      }
    }
    if (USE_LOCAL_SPAWN && (!host || !strcmp(host, myhost))) {
      BOOTSTRAP_VERBOSE(("Process %d spawning process %d on %s via fork()\n",
			 (is_master ? -1 : (int)myproc),
			 (int)child[child_id].rank, myhost));
      execlp(argv0, argv0, "-slave", "localhost",
	     sappendf(NULL, "%d", listen_port),
	     sappendf(NULL, "%d", (int)child_id),
	     is_verbose ? "-v" : NULL,
	     NULL);
      gasneti_fatalerror("execlp(sh) failed");
    } else {
      BOOTSTRAP_VERBOSE(("Process %d spawning process %d on %s via %s\n",
			 (is_master ? -1 : (int)myproc),
			 (int)child[child_id].rank, host, ssh_argv[0]));
      ssh_argv[ssh_argc] = (/* noconst */ char *)host;
      ssh_argv[ssh_argc+1] = sappendf(NULL, "cd %s; exec %s -slave %s %d %d%s",
				      quote_arg(cwd), quote_arg(argv0),
				      myhost, listen_port, (int)child_id,
				      is_verbose ? " -v" : "");
      execvp(ssh_argv[0], ssh_argv);
      gasneti_fatalerror("execvp(ssh) failed");
    }
  }
}

static void do_spawn(int argc, char **argv, char *myhost) {
  int j;

  pre_spawn(children);
  for (j = 0; j < children; ++j) {
    spawn_one(argv[0], j, myhost);
  }
  post_spawn(children, argc, argv);
}

static void usage(const char *argv0) {
  gasneti_fatalerror("usage: %s [-master] [-v] NPROC[:NODES] [--] [ARGS...]", argv0);
}

static void do_master(int argc, char **argv) GASNET_NORETURN;
static void do_master(int argc, char **argv) {
  char myhost[1024];
  char *p;
  int status = -1;
  int argi=1;
  int j;

  is_master = 1;
  gasneti_reghandler(SIGURG, &sigurg_handler);

  if ((argi < argc) && (strcmp(argv[argi], "-master") == 0)) {
    argi++;
  }
  if ((argi < argc) && (strcmp(argv[argi], "-v") == 0)) {
    is_verbose = 1;
    argi++;
  }
  if (argi >= argc) usage(argv[0]); /* ran out of args */

  nproc = atoi(argv[argi]);
  if (nproc < 1) usage(argv[0]); /* bad argument */
  p = strchr(argv[argi], ':');
  if (p) {
    nnodes = atoi(p+1);
    if (nnodes < 1) usage(argv[0]); /* bad argument */
  } else {
    nnodes = nproc;
  }
  BOOTSTRAP_VERBOSE(("Spawning '%s': %d processes on %d nodes\n", argv[0], (int)nproc, (int)nnodes));
  argi++;

  if ((argi < argc) && (strcmp(argv[argi], "--") == 0)) {
    argi++;
  }

  argv[argi-1] = argv[0];
  argc -= argi-1;
  argv += argi-1;

  if (gethostname(myhost, sizeof(myhost)) < 0) {
    gasneti_fatalerror("gethostname() failed");
  }

  configure_ssh();
  build_nodelist();

  /* Arrange to forward termination signals */
  gasneti_reghandler(SIGQUIT, &sigforward);
  gasneti_reghandler(SIGINT,  &sigforward);
  gasneti_reghandler(SIGTERM, &sigforward);
  gasneti_reghandler(SIGHUP,  &sigforward);
  gasneti_reghandler(SIGPIPE, &sigforward);

  /* Configure child(ren) */
  children = 1;
  child = gasneti_calloc(children, sizeof(struct child));
  child[0].rank = 0;
  child[0].procs = nproc;
  child[0].nodes = nnodes;
  child[0].nodelist = nodelist;

  /* Start the root process */
  do_spawn(argc, argv, myhost);

  while (child[0].pid) {
    pause();
  }

  exit (exit_status);
}

static void do_slave(int *argc_p, char ***argv_p, gasnet_node_t *nodes_p, gasnet_node_t *mynode_p)
{
  int argc = *argc_p;
  char **argv = *argv_p;
  const char *args;
  gasnet_node_t child_id;
  const char *parent_name;
  int parent_port;

  is_master = 0;
  gasneti_reghandler(SIGURG, &sigurg_handler);

  if ((argc < 5) || (argc > 6)){
    gasneti_fatalerror("Invalid command line in slave process");
  }
  parent_name = argv[2];
  parent_port = atoi(argv[3]);
  child_id = atoi(argv[4]);
  if (argc == 6) {
    gasneti_assert(!strcmp("-v",argv[5]));
    is_verbose = 1;
  }

  mypid = getpid();

  /* Connect w/ parent to find out who we are */
  do_connect(child_id, parent_name, parent_port, argc_p, argv_p);

  /* Start any children */
  if (tree_procs > 1) {//XXX: tree_nodes throughout...
    gasnet_node_t p_quot, p_rem; /* quotient and remainder of nproc/nodes */
    gasnet_node_t n_quot, n_rem; /* quotient and remainder of nodes/OUT_DEGREE */
    gasnet_node_t local_procs; /* the local processes (proc-per-node), excluding self */
    gasnet_node_t rank, i, j;
    char **sublist;

    p_quot = tree_procs / tree_nodes;
    p_rem = tree_procs % tree_nodes;

    local_procs = p_quot + (p_rem?1:0) - 1;
    p_rem -= (p_rem?1:0);

    /* Children = (local_procs other than self) + (child nodes) */
    children = local_procs + MIN(OUT_DEGREE, (tree_nodes - 1));
    child = gasneti_calloc(children, sizeof(struct child));
    rank = myproc + 1;

    /* Map out the local processes */
    for (j = 0; j < local_procs; ++j) {
	child[j].rank = rank++;
	child[j].procs = 1;
	child[j].nodes = 0; /* N/A */
        child[j].nodelist = NULL;
    }

    /* Map out the child nodes */
    n_quot = (tree_nodes - 1) / OUT_DEGREE;
    n_rem = (tree_nodes - 1) % OUT_DEGREE;
    sublist = nodelist + 1;
    for (j = local_procs; rank < (myproc + tree_procs); j++) {
      gasnet_node_t nodes = n_quot + (n_rem?1:0);
      gasnet_node_t procs = (nodes * p_quot) + MIN(p_rem, nodes);
      n_rem -= (n_rem?1:0);
      p_rem -= MIN(p_rem, nodes);

      child[j].rank = rank;
      child[j].procs = procs;
      child[j].nodes = nodes;
      child[j].nodelist = sublist;
      sublist += nodes;
      rank += procs;
    }
  
    /* Spawn them */
    do_spawn(*argc_p, *argv_p, nodelist[0]);
  }

  *nodes_p = nproc;
  *mynode_p = myproc;
}

/*----------------------------------------------------------------------------------------------*/

/* gasnetc_bootstrapInit
 *
 * Upon return:
 *   + argc and argv are those the user specified
 *   + *nodes_p and *mynode_p are set
 *   + the global environment is available via gasnetc_bootstrapGetenv()
 * There is no barrier at the end, so it is possible that in a multi-level
 * tree, there are still some processes not yet spawned.  This is OK, since
 * we assume that at least one gasnetc_bootstrap*() collectives will follow.
 * Not waiting here allows any subsequent that first collective to overlap
 * with the spawning.
 */
void gasnetc_bootstrapInit(int *argc_p, char ***argv_p, gasnet_node_t *nodes_p, gasnet_node_t *mynode_p) {
  int argc = *argc_p;
  char **argv = *argv_p;

  if (argc < 2) {
    usage(argv[0]);
  }

  if (strcmp(argv[1], "-slave") == 0) {
    do_slave(argc_p, argv_p, nodes_p, mynode_p);
  } else {
    do_master(argc, argv); /* Does not return */
  }
}

/* gasnetc_bootstrapFini
 *
 * Waits for children to exit.
 */
void gasnetc_bootstrapFini(void) {
  int j;

  for (j = 0; j < children; ++j) {
    while (child[j].pid) pause();
    (void)close(child[j].sock);
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
    do_read(child[j].sock, (char *)dest + len*child[j].rank, len*child[j].procs);
  }
  if (myproc) {
    do_write(parent, (char *)dest + len*myproc, tree_procs*len);
  }

  /* Move data down, reducing traffic by sending
     only parts that a given node did not send to us */
  if (myproc) {
    gasnet_node_t next = myproc + tree_procs;
    do_read(parent, dest, len*myproc);
    do_read(parent, (char *)dest + len*next, len*(nproc - next));
  }
  for (j = 0; j < children; ++j) {
    gasnet_node_t next = child[j].rank + child[j].procs;
    do_write(child[j].sock, dest, len*child[j].rank);
    do_write(child[j].sock, (char *)dest + len*next, len*(nproc - next));
  }
}

void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest) {
  size_t row_len = len * nproc;
  char *tmp;
  int j;
                                                                                                              
  /* Collect rows from our subtree (gather) */
  tmp = gasneti_malloc(row_len * tree_procs);
  memcpy(tmp, src, row_len);
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, tmp + row_len*(child[j].rank - myproc), row_len*child[j].procs);
  }
  if (myproc) {
    do_write(parent, tmp, row_len * tree_procs);
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
    do_read(parent, tmp, row_len * tree_procs);
  }
  for (j = 0; j < children; ++j) {
    do_write(child[j].sock, tmp + row_len*(child[j].rank - myproc), row_len*child[j].procs);
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
    } else if ((rootnode > myproc) && (rootnode < (myproc + tree_procs))) {
      /* Forward from child to parent */
      for (j = 0; (rootnode >= child[j].rank + child[j].procs); ++j) {
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
char *gasnetc_bootstrapGetenv(const char *var) {
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
