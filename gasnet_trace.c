/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_trace.c,v $
 *     $Date: 2005/04/06 06:59:08 $
 * $Revision: 1.101 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <signal.h>
#ifdef IRIX
#define signal(a,b) bsd_signal(a,b)
#endif

/* get MAXHOSTNAMELEN */
#ifdef SOLARIS
#include <netdb.h>
#else
#include <sys/param.h>
#endif 

#include <gasnet_internal.h>
#include <gasnet_tools.h>

/* set to non-zero for verbose error reporting */
int gasneti_VerboseErrors = 1;

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  gasnet_hsl_t gasneti_atomicop_lock = GASNET_HSL_INITIALIZER;
  void *gasneti_patomicop_lock = (void*)&gasneti_atomicop_lock;
  GASNETI_GENERIC_DEC_AND_TEST_DEF
  #ifdef GASNETI_GENERIC_CAS_DEF
    GASNETI_GENERIC_CAS_DEF
  #endif
#endif

#if GASNETI_THROTTLE_POLLERS
  gasneti_atomic_t gasneti_throttle_haveusefulwork = gasneti_atomic_init(0);
  gasneti_mutex_t gasneti_throttle_spinpoller = GASNETI_MUTEX_INITIALIZER;
#endif
#if GASNET_DEBUG && GASNETI_THREADS
  gasneti_threadkey_t gasneti_throttledebug_key = GASNETI_THREADKEY_INITIALIZER;
#elif GASNET_DEBUG
  int gasneti_throttledebug_cnt = 0;
#endif

#define GASNET_VERSION_STR  _STRINGIFY(GASNET_VERSION)
GASNETI_IDENT(gasneti_IdentString_APIVersion, "$GASNetAPIVersion: " GASNET_VERSION_STR " $");

#define GASNETI_THREADMODEL_STR _STRINGIFY(GASNETI_THREADMODEL)
GASNETI_IDENT(gasneti_IdentString_ThreadModel, "$GASNetThreadModel: GASNET_" GASNETI_THREADMODEL_STR " $");

#define GASNETI_SEGMENT_CONFIG_STR _STRINGIFY(GASNETI_SEGMENT_CONFIG)
GASNETI_IDENT(gasneti_IdentString_SegConfig, "$GASNetSegment: GASNET_SEGMENT_" GASNETI_SEGMENT_CONFIG_STR " $");

/* embed a string with complete configuration info to support versioning checks */
GASNETI_IDENT(gasneti_IdentString_libraryConfig, "$GASNetConfig: (libgasnet.a) " GASNET_CONFIG_STRING " $");

GASNETI_IDENT(gasneti_IdentString_BuildTimestamp, 
             "$GASNetBuildTimestamp: " __DATE__ " " __TIME__ " $");

GASNETI_IDENT(gasneti_IdentString_BuildID, 
             "$GASNetBuildId: " GASNETI_BUILD_ID " $");
GASNETI_IDENT(gasneti_IdentString_ConfigureArgs, 
             "$GASNetConfigureArgs: " GASNETI_CONFIGURE_ARGS " $");
GASNETI_IDENT(gasneti_IdentString_SystemTuple, 
             "$GASNetSystemTuple: " GASNETI_SYSTEM_TUPLE " $");
GASNETI_IDENT(gasneti_IdentString_SystemName, 
             "$GASNetSystemName: " GASNETI_SYSTEM_NAME " $");

int gasneti_init_done = 0; /*  true after init */
int gasneti_attach_done = 0; /*  true after attach */
extern void gasneti_checkinit() {
  if (!gasneti_init_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
}
extern void gasneti_checkattach() {
   gasneti_checkinit();
   if (!gasneti_attach_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
}

int gasneti_wait_mode = GASNET_WAIT_SPIN;

double *_gasneti_stattime_metric = NULL;

int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_THREADMODEL) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_SEGMENT_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_DEBUG_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TRACE_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_STATS_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ALIGN_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_PTR_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(CORE_,GASNET_CORE_NAME)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(EXTENDED_,GASNET_EXTENDED_NAME)) = 1;

/* Default global definitions of GASNet-wide internal variables
   if conduits override one of these, they must
   still provide variable or macro definitions for these tokens */
#ifdef _GASNET_MYNODE_DEFAULT
  gasnet_node_t gasneti_mynode = (gasnet_node_t)-1;
#endif
#ifdef _GASNET_NODES_DEFAULT
  gasnet_node_t gasneti_nodes = 0;
#endif

#if defined(_GASNET_GETMAXSEGMENTSIZE_DEFAULT) && !GASNET_SEGMENT_EVERYTHING
  uintptr_t gasneti_MaxLocalSegmentSize = 0;
  uintptr_t gasneti_MaxGlobalSegmentSize = 0;
#endif

#ifdef _GASNETI_SEGINFO_DEFAULT
  gasnet_seginfo_t *gasneti_seginfo = NULL;
  gasnet_seginfo_t *gasneti_seginfo_client = NULL;
  void **gasneti_seginfo_ub = NULL; /* cached result of gasneti_seginfo[i].addr + gasneti_seginfo[i].size */
  void **gasneti_seginfo_client_ub = NULL;
#endif

/* ------------------------------------------------------------------------------------ */
/* conduit-independent sanity checks */
extern void gasneti_check_config_preinit() {
  gasneti_assert_always(sizeof(int8_t) == 1);
  gasneti_assert_always(sizeof(uint8_t) == 1);
  #ifndef INTTYPES_16BIT_MISSING
    gasneti_assert_always(sizeof(int16_t) == 2);
    gasneti_assert_always(sizeof(uint16_t) == 2);
  #endif
  gasneti_assert_always(sizeof(int32_t) == 4);
  gasneti_assert_always(sizeof(uint32_t) == 4);
  gasneti_assert_always(sizeof(int64_t) == 8);
  gasneti_assert_always(sizeof(uint64_t) == 8);

  gasneti_assert_always(sizeof(uintptr_t) >= sizeof(void *));

  /* check GASNET_PAGESIZE is a power of 2 and > 0 */
  gasneti_assert_always(GASNET_PAGESIZE > 0 && 
         (GASNET_PAGESIZE & (GASNET_PAGESIZE - 1)) == 0);

  gasneti_assert_always(SIZEOF_GASNET_REGISTER_VALUE_T == sizeof(gasnet_register_value_t));
  gasneti_assert_always(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(int));
  gasneti_assert_always(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(void *));

  #if    defined(GASNETI_PTR32) && !defined(GASNETI_PTR64)
    gasneti_assert_always(sizeof(void*) == 4);
  #elif !defined(GASNETI_PTR32) &&  defined(GASNETI_PTR64)
    gasneti_assert_always(sizeof(void*) == 8);
  #else
    #error must #define exactly one of GASNETI_PTR32 or GASNETI_PTR64
  #endif

  #if defined(GASNETI_UNI_BUILD)
    if (gasneti_cpu_count() > 1) 
      gasneti_fatalerror("GASNet was built in uniprocessor (non-SMP-safe) configuration, "
        "but executed on an SMP. Please re-run GASNet configure with --enable-smp-safe and rebuild");
  #endif

  { static int firstcall = 1;
    if (firstcall) { /* miscellaneous conduit-independent initializations */
      firstcall = 0;
      #if GASNET_DEBUG && GASNETI_THREADS
        gasneti_threadkey_init(&gasneti_throttledebug_key);
      #endif
      gasneti_memcheck_all();
    }
  }
}

extern void gasneti_check_config_postattach() {
  gasneti_check_config_preinit();

  /*  verify sanity of the core interface */
  gasneti_assert_always(gasnet_AMMaxArgs() >= 2*MAX(sizeof(int),sizeof(void*)));      
  gasneti_assert_always(gasnet_AMMaxMedium() >= 512);
  gasneti_assert_always(gasnet_AMMaxLongRequest() >= 512);
  gasneti_assert_always(gasnet_AMMaxLongReply() >= 512);  

  gasneti_assert_always(gasnet_nodes() >= 1);
  gasneti_assert_always(gasnet_mynode() < gasnet_nodes());
  gasneti_memcheck_all();
}

/* ------------------------------------------------------------------------------------ */
#ifndef _GASNET_ERRORNAME
extern const char *gasnet_ErrorName(int errval) {
  switch (errval) {
    case GASNET_OK:           return "GASNET_OK";      
    case GASNET_ERR_NOT_INIT: return "GASNET_ERR_NOT_INIT";      
    case GASNET_ERR_BAD_ARG:  return "GASNET_ERR_BAD_ARG";       
    case GASNET_ERR_RESOURCE: return "GASNET_ERR_RESOURCE";      
    case GASNET_ERR_BARRIER_MISMATCH: return "GASNET_ERR_BARRIER_MISMATCH";      
    case GASNET_ERR_NOT_READY: return "GASNET_ERR_NOT_READY";      
    default: return "*unknown*";
  }
}
#endif

#ifndef _GASNET_ERRORDESC
extern const char *gasnet_ErrorDesc(int errval) {
  switch (errval) {
    case GASNET_OK:           return "No error";      
    case GASNET_ERR_NOT_INIT: return "GASNet message layer not initialized"; 
    case GASNET_ERR_BAD_ARG:  return "Invalid function parameter passed";    
    case GASNET_ERR_RESOURCE: return "Problem with requested resource";      
    case GASNET_ERR_BARRIER_MISMATCH: return "Barrier id's mismatched";      
    case GASNET_ERR_NOT_READY: return "Non-blocking operation not complete";      
    default: return "no description available";
  }
}
#endif
/* ------------------------------------------------------------------------------------ */
extern void gasneti_fatalerror(const char *msg, ...) {
  va_list argptr;
  char expandedmsg[255];

  strcpy(expandedmsg, "*** FATAL ERROR: ");
  strcat(expandedmsg, msg);
  strcat(expandedmsg, "\n");
  va_start(argptr, msg); /*  pass in last argument */
    vfprintf(stderr, expandedmsg, argptr);
    fflush(stderr);
  va_end(argptr);

  abort();
}
/* ------------------------------------------------------------------------------------ */
extern void gasneti_killmyprocess(int exitcode) {
  /* wrapper for _exit() that does the "right thing" to immediately kill this process */
  #if GASNETI_THREADS && defined(HAVE_PTHREAD_KILL_OTHER_THREADS_NP)
    /* on LinuxThreads we need to explicitly kill other threads before calling _exit() */
    pthread_kill_other_threads_np();
  #endif
  _exit(exitcode); /* use _exit to bypass atexit handlers */
  gasneti_fatalerror("gasneti_killmyprocess failed to kill the process!");
}
extern void gasneti_flush_streams() {
  if (fflush(NULL)) /* passing NULL to fflush causes it to flush all open FILE streams */
    gasneti_fatalerror("failed to fflush(NULL): %s", strerror(errno));
  if (fflush(stdout)) 
    gasneti_fatalerror("failed to flush stdout: %s", strerror(errno));
  if (fflush(stderr)) 
    gasneti_fatalerror("failed to flush stderr: %s", strerror(errno));
  gasneti_sched_yield();
}
extern void gasneti_close_streams() {
  if (fclose(stdin)) 
    gasneti_fatalerror("failed to fclose(stdin) in gasnetc_exit: %s", strerror(errno));
  if (fclose(stdout)) 
    gasneti_fatalerror("failed to fclose(stdout) in gasnetc_exit: %s", strerror(errno));
  if (fclose(stderr)) 
    gasneti_fatalerror("failed to fclose(stderr) in gasnetc_exit: %s", strerror(errno));
  gasneti_sched_yield();
}
/* ------------------------------------------------------------------------------------ */
#if defined(__sgi) || defined(__crayx1)
#define _SC_NPROCESSORS_ONLN _SC_NPROC_ONLN
#elif defined(_CRAYT3E)
#define _SC_NPROCESSORS_ONLN _SC_CRAY_MAXPES
#elif defined(__APPLE__) || defined(FREEBSD) || defined(NETBSD)
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
/* return the physical count of CPU's on this node, 
   or zero if that cannot be determined */
extern int gasneti_cpu_count() {
  static int hwprocs = -1;
  if (hwprocs >= 0) return hwprocs;

  #if defined(__APPLE__) || defined(FREEBSD) || defined(NETBSD)
      {
        int mib[2];
        size_t len;

        mib[0] = CTL_HW;
        mib[1] = HW_NCPU;
        len = sizeof(hwprocs);
        if (sysctl(mib, 2, &hwprocs, &len, NULL, 0)) {
           perror("sysctl");
           abort();
        }
        if (hwprocs < 1) hwprocs = 0;
      }
  #elif defined(HPUX) || defined(SUPERUX) || defined(__MTA__)
      hwprocs = 0; /* appears to be no way to query CPU count on these */
  #else
      hwprocs = sysconf(_SC_NPROCESSORS_ONLN);
      if (hwprocs < 1) hwprocs = 0; /* catch failures on Solaris/Cygwin */
  #endif

  gasneti_assert_always(hwprocs >= 0);
  return hwprocs;
}
/* ------------------------------------------------------------------------------------ */
/* build a code-location string (used by gasnete_current_loc) */
char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum) {
  int sz;
  char *loc;
  int fnlen;
  if (!funcname) funcname = "";
  if (!filename) filename = "*unknown file*";
  fnlen = strlen(funcname);
  sz = fnlen + strlen(filename) + 20;
  loc = gasneti_malloc(sz);
  if (*funcname)
    sprintf(loc,"%s%s at %s:%i",
           funcname,
           (fnlen && funcname[fnlen-1] != ')'?"()":""),
           filename, linenum);
  else
    sprintf(loc,"%s:%i", filename, linenum);
  return loc;
}
/* ------------------------------------------------------------------------------------ */
#ifndef GASNETI_UNFREEZE_SIGNAL
/* signal to use for unfreezing, could also use SIGUSR1/2 or several others */
#define GASNETI_UNFREEZE_SIGNAL SIGCONT
#define GASNETI_UNFREEZE_SIGNAL_STR "SIGCONT"
#endif

static volatile int gasnet_frozen = TRUE;
static void gasneti_unfreezeHandler(int sig) {
  gasnet_frozen = 0;
}
/*  all this to make sure we get a full stack frame for debugger */
static void _freezeForDebugger(int depth) {
  if (!depth) _freezeForDebugger(1);
  else {
    volatile int i=0;
    gasneti_sighandlerfn_t old = gasneti_reghandler(GASNETI_UNFREEZE_SIGNAL, gasneti_unfreezeHandler);
    while (gasnet_frozen) {
      i++;
      sleep(1);
    }
    gasneti_reghandler(GASNETI_UNFREEZE_SIGNAL, old);
  }
}
extern void gasneti_freezeForDebugger() {
  char name[255];
  if (gasneti_getenv_yesno_withdefault("GASNET_FREEZE",0)) {
    gethostname(name, 255);
    fprintf(stderr,"GASNet node frozen for debugger: host=%s  pid=%i\n"
                   "To unfreeze, attach a debugger and set 'gasnet_frozen' to 0, or send a "
                   GASNETI_UNFREEZE_SIGNAL_STR "\n", 
                   name, (int)getpid()); 
    fflush(stderr);
    _freezeForDebugger(0);
  }
}
/* ------------------------------------------------------------------------------------ */
gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp) {
  gasneti_sighandlerfn_t fpret = (gasneti_sighandlerfn_t)signal(sigtocatch, fp); 
  if (fpret == (gasneti_sighandlerfn_t)SIG_ERR) {
    gasneti_fatalerror("Got a SIG_ERR while registering handler for signal %i : %s", 
                       sigtocatch,strerror(errno));
    return NULL;
  }
  #ifdef SIG_HOLD
    else if (fpret == (gasneti_sighandlerfn_t)SIG_HOLD) {
      gasneti_fatalerror("Got a SIG_HOLD while registering handler for signal %i : %s", 
                         sigtocatch,strerror(errno));
      return NULL;
    }
  #endif
  return fpret;
}

#define DEF_SIGNAL(name) { name, #name, NULL }
static struct {
  int signum;
  const char *signame;
  gasneti_sighandlerfn_t oldhandler;
} gasneti_signals[] = {
  /* abort signals */
  DEF_SIGNAL(SIGABRT),
  DEF_SIGNAL(SIGILL),
  DEF_SIGNAL(SIGSEGV),
  DEF_SIGNAL(SIGBUS),
  DEF_SIGNAL(SIGFPE),
  /* termination signals */
  DEF_SIGNAL(SIGQUIT),
  DEF_SIGNAL(SIGINT),
  DEF_SIGNAL(SIGTERM),
  DEF_SIGNAL(SIGHUP),
  DEF_SIGNAL(SIGPIPE)
};

#ifndef GASNETC_FATALSIGNAL_CALLBACK
#define GASNETC_FATALSIGNAL_CALLBACK(sig)
#endif

void gasneti_defaultSignalHandler(int sig) {
  gasneti_sighandlerfn_t oldhandler = NULL;
  const char *signame = NULL;
  int i;
  for (i = 0; i < sizeof(gasneti_signals)/sizeof(gasneti_signals[0]); i++) {
    if (gasneti_signals[i].signum == sig) {
      oldhandler = gasneti_signals[i].oldhandler;
      signame = gasneti_signals[i].signame;
    }
  }
  gasneti_assert(signame);

  switch (sig) {
    case SIGQUIT:
      /* client didn't register a SIGQUIT handler, so just exit */
      gasnet_exit(1);
      break;
    case SIGABRT:
    case SIGILL:
    case SIGSEGV:
    case SIGBUS:
    case SIGFPE:
      GASNETC_FATALSIGNAL_CALLBACK(sig); /* give conduit first crack at it */
      fprintf(stderr,"*** Caught a fatal signal: %s(%i) on node %i/%i\n",
        signame, sig, (int)gasnet_mynode(), (int)gasnet_nodes()); 
      fflush(stderr);
      signal(sig, SIG_DFL); /* restore default core-dumping handler and re-raise */
      #if 1
        raise(sig);
      #elif 0
        kill(getpid(),sig);
      #else
        oldhandler(sig);
      #endif
      break;
    default: 
      /* translate signal to SIGQUIT */
      fprintf(stderr,"*** Caught a signal: %s(%i) on node %i/%i\n",
        signame, sig, (int)gasnet_mynode(), (int)gasnet_nodes()); 
      fflush(stderr);
      #if 1
        raise(SIGQUIT);
      #elif 0
        kill(getpid(),SIGQUIT);
      #else
        oldhandler(SIGQUIT);
      #endif
      break;
  }
}

void gasneti_registerSignalHandlers(gasneti_sighandlerfn_t handler) {
  int i;
  for (i = 0; i < sizeof(gasneti_signals)/sizeof(gasneti_signals[0]); i++) {
    gasneti_signals[i].oldhandler = 
      gasneti_reghandler(gasneti_signals[i].signum, handler);
  }
}

extern int gasneti_set_waitmode(int wait_mode) {
  const char *desc = NULL;
  GASNETI_CHECKINIT();
  switch (wait_mode) {
    case GASNET_WAIT_SPIN:      desc = "GASNET_WAIT_SPIN"; break;
    case GASNET_WAIT_BLOCK:     desc = "GASNET_WAIT_BLOCK"; break;
    case GASNET_WAIT_SPINBLOCK: desc = "GASNET_WAIT_SPINBLOCK"; break;
    default:
      GASNETI_RETURN_ERRR(BAD_ARG, "illegal wait mode");
  }
  GASNETI_TRACE_PRINTF(I, ("gasnet_set_waitmode(%s)", desc));
  gasneti_wait_mode = wait_mode;
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/* Global environment variable handling */

extern uint64_t gasneti_checksum(void *p, int numbytes) {
 uint8_t *buf = (uint8_t *)p;
 uint64_t result = 0;
 int i;
 for (i=0;i<numbytes;i++) {
   result = ((result << 4) | ((result >> 60) & 0x0F) ) ^ *buf;
   buf++;
 }
 return result;
}

extern char **environ; 

static void gasneti_serializeEnvironment(uint8_t **pbuf, int *psz) {
  /* flatten a snapshot of the environment to make it suitable for transmission
   * here we assume the standard representation where a pointer to the environment 
   * is stored in a global variable 'environ' and the environment is represented as an array 
   * of null-terminated strings where each has the form 'key=value' and value may be empty, 
   * and the final string pointer is a NULL pointer
   * we flatten this into a list of null-terminated 'key=value' strings, 
   * terminated with a double-null
   */
  uint8_t *buf; 
  uint8_t *p;
  int i;
  int totalEnvSize = 0;
  if (!environ) {
    /* T3E stupidly omits environ support, despite documentation to the contrary */
    GASNETI_TRACE_PRINTF(I,("WARNING: environ appears to be empty -- ignoring it"));
    *pbuf = NULL;
    *psz = 0;
    return;
  }
  for(i = 0; environ[i]; i++) 
    totalEnvSize += strlen(environ[i]) + 1;
  totalEnvSize++;

  buf = (uint8_t *)gasneti_malloc(totalEnvSize);
  p = buf;
  p[0] = 0;
  for(i = 0; environ[i]; i++) {
    strcpy((char*)p, environ[i]);
    p += strlen((char*)p) + 1;
    }
  *p = 0;
  gasneti_assert((p+1) - buf == totalEnvSize);

  *pbuf = buf;
  *psz = totalEnvSize;
}

static char *gasneti_globalEnv = NULL;

typedef struct {
  int sz;
  uint64_t checksum;
} gasneti_envdesc_t;

/* do the work necessary to setup the global environment for use by gasneti_getenv
   broadcast the environment variables from one node to all nodes
   Note this currently assumes that at least one of the compute nodes has the full
    environment - systems where the environment is not propagated to any compute node
    will need something more sophisticated.
   exchangefn is required function for exchanging data 
   broadcastfn is optional (can be NULL) but highly recommended for scalability
 */
extern void gasneti_setupGlobalEnvironment(gasnet_node_t numnodes, gasnet_node_t mynode,
                                           gasneti_bootstrapExchangefn_t exchangefn,
                                           gasneti_bootstrapBroadcastfn_t broadcastfn) {
  uint8_t *myenv; 
  int sz; 
  uint64_t checksum;
  gasneti_envdesc_t myenvdesc;
  gasneti_envdesc_t *allenvdesc;

  gasneti_assert(exchangefn);

  gasneti_serializeEnvironment(&myenv,&sz);
  checksum = gasneti_checksum(myenv,sz);

  myenvdesc.sz = sz;
  myenvdesc.checksum = checksum;

  allenvdesc = gasneti_malloc(numnodes*sizeof(gasneti_envdesc_t));
  /* gather environment description from all nodes */
  (*exchangefn)(&myenvdesc, sizeof(gasneti_envdesc_t), allenvdesc);

  { /* see if the node environments differ and find the largest */
    int i;
    int rootid = 0;
    int identical = 1;
    gasneti_envdesc_t rootdesc = allenvdesc[rootid];
    for (i=1; i < numnodes; i++) {
      if (rootdesc.checksum != allenvdesc[i].checksum || 
          rootdesc.sz != allenvdesc[i].sz) 
          identical = 0;
      if (allenvdesc[i].sz > rootdesc.sz) { 
        /* assume the largest env is the one we want */
        rootdesc = allenvdesc[i];
        rootid = i;
      }
    }
    if (identical) { /* node environments all identical - don't bother to propagate */
      gasneti_free(allenvdesc);
      gasneti_free(myenv);
      return;
    } else {
      int envsize = rootdesc.sz;
      gasneti_globalEnv = gasneti_malloc(envsize);
      if (broadcastfn) {
        (*broadcastfn)(myenv, envsize, gasneti_globalEnv, rootid);
      } else {
        /* this is wasteful of memory and bandwidth, and non-scalable */
        char *tmp = gasneti_malloc(envsize*numnodes);
        memcpy(tmp+mynode*envsize, myenv, sz);
        (*exchangefn)(tmp+mynode*envsize, envsize, tmp);
        memcpy(gasneti_globalEnv, tmp+rootid*envsize, envsize);
        gasneti_free(tmp);
      }
      gasneti_assert(gasneti_checksum(gasneti_globalEnv,envsize) == rootdesc.checksum);
      gasneti_free(allenvdesc);
      gasneti_free(myenv);
      return;
    }
  }

}

extern char *gasneti_getenv(const char *keyname) {
  char *retval = NULL;

  #ifdef GASNETI_CONDUIT_GETENV
    /* highest priority given to conduit-specific getenv */
    retval = GASNETI_CONDUIT_GETENV(keyname);
  #endif

  if (keyname && !retval && gasneti_globalEnv) { 
    /* global environment takes precedence 
     * (callers who want the local environment can call getenv directly)
     */
    char *p = gasneti_globalEnv;
    int keylen = strlen(keyname);
    while (*p) {
      if (!strncmp(keyname, p, keylen) && p[keylen] == '=') {
        retval = p + keylen + 1;
        break;
      }
      p += strlen(p) + 1;
    }
  }

  if (keyname && !retval) /* try local environment */
    retval = getenv(keyname);
  
  GASNETI_TRACE_PRINTF(I,("gasnet_getenv(%s) => '%s'",
                          (keyname?keyname:"NULL"),(retval?retval:"NULL")));

  return retval;
}

/* expression that defines whether the given process should report to the console
   on env queries - needs to work before gasnet_init
 */
#ifndef GASNETI_ENV_OUTPUT_NODE
#define GASNETI_ENV_OUTPUT_NODE() \
        (gasneti_mynode == 0 || gasneti_mynode == (gasnet_node_t)-1)
#endif

static char *_gasneti_getenv_withdefault(const char *keyname, const char *defaultval, int yesno) {
  const char * retval = NULL;
  static int firsttime = 1;
  static int verboseenv = 0;
  const char *dflt = "";
  if (firsttime) {
    #if GASNET_DEBUG_VERBOSE
      verboseenv = 1;
    #else
      verboseenv = !!gasneti_getenv("GASNET_VERBOSEENV");
    #endif
    if (gasneti_init_done) firsttime = 0;
  }
  gasneti_assert(defaultval != NULL);
  retval = gasneti_getenv(keyname);
  if (retval == NULL) {
    retval = defaultval;
    dflt = "   (default)";
  }
  if (yesno) {
    char s[10];
    int i;
    strncpy(s, retval, 10); s[9] = '\0';
    for (i = 0; i < 10; i++) s[i] = toupper(s[i]);
    if (!strcmp(s, "N") || !strcmp(s, "NO") || !strcmp(s, "0")) retval = "NO";
    else if (!strcmp(s, "Y") || !strcmp(s, "YES") || !strcmp(s, "1")) retval = "YES";
    else gasneti_fatalerror("If used, environment variable '%s' must be set to 'Y|YES|y|yes|1' or 'N|n|NO|no|0'", keyname);
  }
  if (verboseenv && GASNETI_ENV_OUTPUT_NODE()) {
    const char *displayval = retval;
    int width;
    if (strlen(retval) == 0) displayval = "*empty*";
    width = MAX(10,55 - strlen(keyname) - strlen(displayval));
    fprintf(stderr, "ENV parameter: %s = %s%*s\n", keyname, displayval, width, dflt);
    fflush(stderr);
  }
  GASNETI_TRACE_PRINTF(I,("ENV parameter: %s = %s%s", keyname, retval, dflt));
  return (char *)retval;
}
extern char *gasneti_getenv_withdefault(const char *keyname, const char *defaultval) {
  return _gasneti_getenv_withdefault(keyname, defaultval, 0);
}
extern int gasneti_getenv_yesno_withdefault(const char *keyname, int defaultval) {
  return !strcmp(_gasneti_getenv_withdefault(keyname, (defaultval?"YES":"NO"), 1), "YES");
}

/* set an environment variable, for the local process ONLY */
extern void gasneti_setenv(const char *key, const char *value) {
  /* prefer putenv because it's POSIX, setenv is not */
  #if HAVE_PUTENV 
    char *tmp = gasneti_malloc(strlen(key) + strlen(value) + 2);
    int retval;
    strcpy(tmp, key);
    strcat(tmp, "=");
    strcat(tmp, value);
    retval = putenv(tmp);
    if (retval) gasneti_fatalerror("Failed to putenv(\"%s\") in gasneti_setenv => %s(%i)",
                                     tmp, strerror(errno), errno);
  #elif HAVE_SETENV
    int retval = setenv(key, value, 1);
    if (retval) gasneti_fatalerror("Failed to setenv(\"%s\",\"%s\",1) in gasneti_setenv => %s(%i)",
                                     key, value, strerror(errno), errno);
  #else
    gasneti_fatalerror("Got a call to gasneti_setenv, but don't know how to do that on your system");
  #endif
}

/* unset an environment variable, for the local process ONLY */
extern void gasneti_unsetenv(const char *key) {
  /* prefer putenv because it's POSIX, unsetenv is not */
  #if HAVE_PUTENV
    char *tmp = gasneti_malloc(strlen(key) + 1);
    int retval;
    strcpy(tmp, key);
    retval = putenv(tmp);
    if (retval) gasneti_fatalerror("Failed to putenv(\"%s\") in gasneti_unsetenv => %s(%i)",
                                     key, strerror(errno), errno);
  #elif HAVE_UNSETENV
    int retval = unsetenv(key);
    if (!retval) gasneti_fatalerror("Failed to unsetenv(\"%s\") in gasneti_unsetenv => %s(%i)",
                                     key, strerror(errno), errno);
  #else
    gasneti_fatalerror("Got a call to gasneti_unsetenv, but don't know how to do that on your system");
  #endif
}

/* ------------------------------------------------------------------------------------ */
/* GASNet Tracing and Statistics */

#if GASNET_TRACE
  GASNETI_IDENT(gasneti_IdentString_trace, "$GASNetTracingEnabled: 1 $");
#endif
#if GASNET_STATS
  GASNETI_IDENT(gasneti_IdentString_stats, "$GASNetStatisticsEnabled: 1 $");
#endif

gasneti_mutex_t gasneti_tracelock = GASNETI_MUTEX_INITIALIZER;
char gasneti_tracetypes[256];
char gasneti_statstypes[256];
int gasneti_trace_suppresslocal;
FILE *gasneti_tracefile = NULL;
FILE *gasneti_statsfile = NULL;
static gasneti_stattime_t starttime;

#if GASNET_STATS
  gasnett_stats_callback_t gasnett_stats_callback = NULL;
#endif

#if GASNET_TRACE
  extern void _gasnett_trace_printf(const char *format, ...) {
    #define TMPBUFSZ 1024
    char output[TMPBUFSZ];
    va_list argptr;
    va_start(argptr, format); /*  pass in last argument */
      { int sz = vsnprintf(output, TMPBUFSZ, format, argptr);
        if (sz >= (TMPBUFSZ-5) || sz < 0) strcpy(output+(TMPBUFSZ-5),"...");
      }
    va_end(argptr);
    GASNETI_TRACE_MSG(H, output);
    #undef TMPBUFSZ
  }
#endif

/* these are legal even without STATS/TRACE */
extern int gasneti_format_memveclist_bufsz(size_t count) {
  return 200+count*50;
}
extern gasneti_memveclist_stats_t gasneti_format_memveclist(char *buf, size_t count, gasnet_memvec_t const *list) {
  const int bufsz = gasneti_format_memveclist_bufsz(count);
  char * p = buf;
  int i, j=0;
  gasneti_memveclist_stats_t stats = gasnete_memveclist_stats((count), (list));
  sprintf(p, "%i entries, totalsz=%i, bounds=["GASNETI_LADDRFMT"..."GASNETI_LADDRFMT"]\n"
             "list=[",
              (int)(count), (int)(stats.totalsz),
              GASNETI_LADDRSTR(stats.minaddr), GASNETI_LADDRSTR(stats.maxaddr));
  p += strlen(p);
  for (i=0; i < count; i++) {
    j++;
    sprintf(p, "{"GASNETI_LADDRFMT",%5lu}", 
      GASNETI_LADDRSTR(list[i].addr), (unsigned long)list[i].len);
    if (i < count-1) { 
      strcat(p, ", ");
      if (j % 4 == 0) strcat(p,"\n      ");
    }
    p += strlen(p);
    gasneti_assert(p-buf < bufsz);
  }
  strcat(p,"]");
  p += strlen(p);
  gasneti_assert(p-buf < bufsz);
  return stats;
}
extern int gasneti_format_addrlist_bufsz(size_t count) {
  return 200+count*25;
}
extern gasneti_addrlist_stats_t gasneti_format_addrlist(char *buf, size_t count, void * const *list, size_t len) {
  const int bufsz = gasneti_format_addrlist_bufsz(count);
  char * p = buf;
  int i,j=0;
  gasneti_addrlist_stats_t stats = gasnete_addrlist_stats((count), (list), (len));
  sprintf(p, "%i entries, totalsz=%i, len=%i, bounds=["GASNETI_LADDRFMT"..."GASNETI_LADDRFMT"]\n"
             "list=[",
              (int)(count), (int)((count)*(len)), (int)(len),
              GASNETI_LADDRSTR(stats.minaddr), GASNETI_LADDRSTR(stats.maxaddr));
  p += strlen(p);
  for (i=0; i < count; i++) {
    j++;
    sprintf(p, GASNETI_LADDRFMT, GASNETI_LADDRSTR(list[i]));
    if (i < count-1) {
      strcat(p, ", ");
      if (j % 8 == 0) strcat(p,"\n      ");
    }
    p += strlen(p);
    gasneti_assert(p-buf < bufsz);
  }
  strcat(p,"]");
  p += strlen(p);
  gasneti_assert(p-buf < bufsz);
  return stats;
}

#if GASNETI_STATS_OR_TRACE
  #define BUILD_STATS(type,name,desc) { #type, #name, #desc },
  gasneti_statinfo_t gasneti_stats[] = {
    GASNETI_ALL_STATS(BUILD_STATS, BUILD_STATS, BUILD_STATS)
    {NULL, NULL, NULL}
  };

  #define BUFSZ     8192
  #define NUMBUFS   32
  static char gasneti_printbufs[NUMBUFS][BUFSZ];
  static int gasneti_curbuf = 0;
  static gasneti_mutex_t gasneti_buflock = GASNETI_MUTEX_INITIALIZER;

  /* give gcc enough information to type-check our format strings */
  static void gasneti_file_vprintf(FILE *fp, const char *format, va_list argptr) __attribute__((__format__ (__printf__, 2, 0)));
  static void gasneti_trace_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
  static void gasneti_stats_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
  static void gasneti_tracestats_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));

  /* line number control */
  #if GASNETI_CLIENT_THREADS
    gasneti_threadkey_t gasneti_srclineinfo_key = GASNETI_THREADKEY_INITIALIZER;
    typedef struct {
      const char *filename;
      unsigned int linenum;
      unsigned int frozen;
    } gasneti_srclineinfo_t;
    GASNET_INLINE_MODIFIER(gasneti_mysrclineinfo)
    gasneti_srclineinfo_t *gasneti_mysrclineinfo() {
      gasneti_srclineinfo_t *srclineinfo = gasneti_threadkey_get(gasneti_srclineinfo_key);
      if_pt (srclineinfo) {
        gasneti_memcheck(srclineinfo);
        return srclineinfo;
      } else {
        /*  first time we've seen this thread - need to set it up */
        gasneti_srclineinfo_t *srclineinfo = gasneti_calloc(1,sizeof(gasneti_srclineinfo_t));
        gasneti_threadkey_set(gasneti_srclineinfo_key, srclineinfo);
        return srclineinfo;
      }
    }
    void gasneti_trace_setsourceline(const char *filename, unsigned int linenum) {
      gasneti_srclineinfo_t *sli = gasneti_mysrclineinfo();
      if_pf (sli->frozen > 0) return;
      if_pt (filename) sli->filename = filename;
      sli->linenum = linenum;
    }
    extern void gasneti_trace_getsourceline(const char **pfilename, unsigned int *plinenum) {
      gasneti_srclineinfo_t *sli = gasneti_mysrclineinfo();
      *pfilename = sli->filename;
      *plinenum = sli->linenum;
    }
    extern void gasneti_trace_freezesourceline() {
      gasneti_srclineinfo_t *sli = gasneti_mysrclineinfo();
      sli->frozen++;
    }
    extern void gasneti_trace_unfreezesourceline() {
      gasneti_srclineinfo_t *sli = gasneti_mysrclineinfo();
      gasneti_assert(sli->frozen > 0);
      sli->frozen--;
    }
  #else
    const char *gasneti_srcfilename = NULL;
    unsigned int gasneti_srclinenum = 0;
    unsigned int gasneti_srcfreeze = 0;
  #endif

  static char *gasneti_getbuf() {
    int bufidx;

    gasneti_mutex_lock(&gasneti_buflock);

    bufidx = gasneti_curbuf;
    gasneti_curbuf = (gasneti_curbuf + 1) % NUMBUFS;

    gasneti_mutex_unlock(&gasneti_buflock);
    return gasneti_printbufs[bufidx];
  }

  /* format and return a string result
     caller should not deallocate string, they are recycled automatically
  */
  extern char *gasneti_dynsprintf(const char *format, ...) {
    va_list argptr;
    char *output = gasneti_getbuf();

    va_start(argptr, format); /*  pass in last argument */
      { int sz = vsnprintf(output, BUFSZ, format, argptr);
        if (sz >= (BUFSZ-5) || sz < 0) strcpy(output+(BUFSZ-5),"...");
      }
    va_end(argptr);
    return output;
  }

  #define BYTES_PER_LINE 16
  #define MAX_LINES 10
  /* format a block of data into a string and return it - 
     caller should not deallocate string, they are recycled automatically
   */
  extern char *gasneti_formatdata(void *p, size_t nbytes) { 
    uint8_t *data = (uint8_t *)p;
    char *output = gasneti_getbuf();
    *output = '\0';
    if (nbytes <= 8) { /* fits on one line */
      size_t i;
      for (i=0; i < nbytes; i++) {
        char temp[5];
        sprintf(temp,"%02x ",(int)data[i]);
        strcat(output, temp);
      }
    } else {
      size_t line;
      size_t col;
      size_t byteidx = 0;
      strcat(output,"\n");
      for (line=0;line<MAX_LINES && byteidx<nbytes;line++) {
        char nicefmt[BYTES_PER_LINE+1];
        char lineheader[10];
        nicefmt[0] = '\0';
        sprintf(lineheader, "  0x%-2x:  ", (int)byteidx);
        strcat(output, lineheader);
        for (col=0;col<BYTES_PER_LINE && byteidx<nbytes;col++) {
          char temp[5];
          sprintf(temp,"%02x ",(int)data[byteidx]);
          strcat(output, temp);
          if (isprint(data[byteidx])) nicefmt[col] = data[byteidx];
          else nicefmt[col] = '.';
          byteidx++;
        }
        nicefmt[col] = '\0';
        for(;col < BYTES_PER_LINE; col++) strcat(output, "   ");
        strcat(output, "  ");
        strcat(output, nicefmt);
        strcat(output, "\n");
      }
      if (byteidx < nbytes) strcat(output, "         (output truncated)\n");
    }
    return output;
  }

  extern const char *gasneti_format_strides(size_t count, const size_t *list) {
    char * retval;
    const int bufsz = count*25+2;
    char * temp = gasneti_malloc(bufsz);
    char * p = temp;
    int i;
    p[0] = '\0';
    for (i=0; i < count; i++) {
      sprintf(p, "%lu", (unsigned long)list[i]);
      if (i < count-1) strcat(p, ", ");
      p += strlen(p);
      gasneti_assert(p-temp < bufsz);
    }
    retval = gasneti_dynsprintf("[%s]", temp);
    gasneti_free(temp);
    return retval;
  }

  static int gasneti_autoflush = 0;
  #define GASNETI_TRACEFILE_FLUSH(fp) do {  \
    if (gasneti_autoflush) fflush(fp);      \
  } while (0)

  /* private helper for gasneti_trace/stats_output */
  static void gasneti_file_output(FILE *fp, double time, const char *type, const char *msg, int traceheader) {
    gasneti_mutex_assertlocked(&gasneti_tracelock);
    gasneti_assert(fp);
    if (traceheader) {
      char srclinestr[255];
      srclinestr[0] ='\0';
      #if GASNET_TRACE
        if (GASNETI_TRACE_ENABLED(N)) {
          const char *filename; 
          unsigned int linenum;
          gasneti_trace_getsourceline(&filename, &linenum);
          if (filename) sprintf(srclinestr," [%s:%i]", filename, linenum);
        }
      #endif
      #if GASNETI_THREADS
        fprintf(fp, "%i(%x) %8.6fs>%s (%c) %s%s", 
          (int)gasnet_mynode(), (int)(uintptr_t)pthread_self(), time, srclinestr, *type,
          msg, (msg[strlen(msg)-1]=='\n'?"":"\n"));
      #else
        fprintf(fp, "%i %8.6fs>%s (%c) %s%s", (int)gasnet_mynode(), time, srclinestr, *type,
          msg, (msg[strlen(msg)-1]=='\n'?"":"\n"));
      #endif
    } else {
        fprintf(fp, "%i> (%c) %s%s", (int)gasnet_mynode(), *type, msg,
                (msg[strlen(msg)-1]=='\n'?"":"\n"));
    }
    GASNETI_TRACEFILE_FLUSH(fp);
  }

  /* dump message to tracefile */
  extern void gasneti_trace_output(const char *type, const char *msg, int traceheader) {
    if (gasneti_tracefile) {
      double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
      gasneti_mutex_lock(&gasneti_tracelock);
        if (gasneti_tracefile) 
          gasneti_file_output(gasneti_tracefile, time, type, msg, traceheader);
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  extern void gasneti_stats_output(const char *type, const char *msg, int traceheader) {
    if (gasneti_tracefile || gasneti_statsfile) {
      double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
      gasneti_mutex_lock(&gasneti_tracelock);
        if (gasneti_statsfile) 
          gasneti_file_output(gasneti_statsfile, time, type, msg, traceheader);
        #if GASNETI_STATS_ECHOED_TO_TRACEFILE
        if (gasneti_tracefile) /* stat output also goes to trace */
          gasneti_file_output(gasneti_tracefile, time, type, msg, traceheader);
        #endif
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  extern void gasneti_tracestats_output(const char *type, const char *msg, int traceheader) {
    if (gasneti_tracefile || gasneti_statsfile) {
      double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
      gasneti_mutex_lock(&gasneti_tracelock);
        if (gasneti_statsfile) 
          gasneti_file_output(gasneti_statsfile, time, type, msg, traceheader);
        if (gasneti_tracefile) 
          gasneti_file_output(gasneti_tracefile, time, type, msg, traceheader);
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }

  /* private helper for gasneti_trace/stats_printf */
  static void gasneti_file_vprintf(FILE *fp, const char *format, va_list argptr) {
    gasneti_mutex_assertlocked(&gasneti_tracelock);
    gasneti_assert(fp);
    fprintf(fp, "%i> ", (int)gasnet_mynode());
    vfprintf(fp, format, argptr);
    if (format[strlen(format)-1]!='\n') fprintf(fp, "\n");
    GASNETI_TRACEFILE_FLUSH(fp);
  }

  /* dump message to tracefile with simple header */
  static void gasneti_trace_printf(const char *format, ...) {
    va_list argptr;
    if (gasneti_tracefile) {
      gasneti_mutex_lock(&gasneti_tracelock);
      if (gasneti_tracefile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_tracefile, format, argptr);
        va_end(argptr);
      }
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  static void gasneti_stats_printf(const char *format, ...) {
    va_list argptr;
    if (gasneti_tracefile || gasneti_statsfile) {
      gasneti_mutex_lock(&gasneti_tracelock);
      if (gasneti_statsfile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_statsfile, format, argptr);
        va_end(argptr);
      }
      #if GASNETI_STATS_ECHOED_TO_TRACEFILE
      if (gasneti_tracefile) { /* stat output also goes to trace */
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_tracefile, format, argptr);
        va_end(argptr);
      }
      #endif
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  static void gasneti_tracestats_printf(const char *format, ...) {
    va_list argptr;
    if (gasneti_tracefile || gasneti_statsfile) {
      gasneti_mutex_lock(&gasneti_tracelock);
      if (gasneti_statsfile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_statsfile, format, argptr);
        va_end(argptr);
      }
      if (gasneti_tracefile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_tracefile, format, argptr);
        va_end(argptr);
      }
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
#endif

static FILE *gasneti_open_outputfile(const char *filename, const char *desc) {
  FILE *fp = NULL;
  char pathtemp[255];
  if (!strcmp(filename, "stderr") ||
      !strcmp(filename, "-")) {
    filename = "stderr";
    fp = stderr;
  } else if (!strcmp(filename, "stdout")) {
    filename = "stdout";
    fp = stdout;
  } else {
    strcpy(pathtemp,filename);
    while (strchr(pathtemp,'%')) { /* replace any '%' with node num */
      char temp[255];
      char *p = strchr(pathtemp,'%');
      *p = '\0';
      sprintf(temp,"%s%i%s",pathtemp,(int)gasnet_mynode(),p+1);
      strcpy(pathtemp,temp);
    }
    filename = pathtemp;
    fp = fopen(filename, "wt");
    if (!fp) {
      fprintf(stderr, "ERROR: Failed to open '%s' for %s output (%s). Redirecting output to stderr.\n",
              filename, desc, strerror(errno));
      filename = "stderr";
      fp = stderr;
    }
  }
  fprintf(stderr, "GASNet reporting enabled - %s output directed to %s\n", 
          desc, filename);
  return fp;
}

extern void gasneti_trace_init(int argc, char **argv) {
  gasneti_free(gasneti_malloc(1)); /* touch the malloc system to ensure it's intialized */

 #if GASNETI_STATS_OR_TRACE
{ const char *tracetypes = NULL;
  const char *statstypes = NULL;

  starttime = GASNETI_STATTIME_NOW();
  { /* setup tracefile */
    char *tracefilename = gasneti_getenv_withdefault("GASNET_TRACEFILE","");
    char *statsfilename = gasneti_getenv_withdefault("GASNET_STATSFILE","");
    if (tracefilename && !strcmp(tracefilename, "")) tracefilename = NULL;
    if (statsfilename && !strcmp(statsfilename, "")) statsfilename = NULL;
    #if GASNET_TRACE || (GASNET_STATS && GASNETI_STATS_ECHOED_TO_TRACEFILE)
      if (tracefilename) {
        gasneti_tracefile = gasneti_open_outputfile(tracefilename, 
        #if GASNET_TRACE
          "tracing"
          #if GASNET_STATS && GASNETI_STATS_ECHOED_TO_TRACEFILE
            " and "
          #endif
        #endif
        #if GASNET_STATS && GASNETI_STATS_ECHOED_TO_TRACEFILE
          "statistical"
        #endif
        );
      } else 
    #endif
      gasneti_tracefile = NULL;
    #if GASNET_STATS
      if (statsfilename) gasneti_statsfile = gasneti_open_outputfile(statsfilename, "statistical");
      else 
    #endif
        gasneti_statsfile = NULL;
  }

  { /* setup tracetypes */
    const char *types;
    types = gasneti_getenv_withdefault("GASNET_TRACEMASK", GASNETI_ALLTYPES);
    tracetypes = types;
    while (*types) {
      gasneti_tracetypes[(int)(*types)] = 1;
      types++;
    }
    types = gasneti_getenv_withdefault("GASNET_STATSMASK", GASNETI_ALLTYPES);
    statstypes = types;
    while (*types) {
      gasneti_statstypes[(int)(*types)] = 1;
      types++;
    }
  }

  gasneti_autoflush = gasneti_getenv_yesno_withdefault("GASNET_TRACEFLUSH",0);
  gasneti_trace_suppresslocal = !gasneti_getenv_yesno_withdefault("GASNET_TRACELOCAL",1);

  { time_t ltime;
    int i;
    char hostname[MAXHOSTNAMELEN];
    char temp[1024];
    char *p;
    time(&ltime); 
    strcpy(temp, ctime(&ltime));
    if (temp[strlen(temp)-1] == '\n') temp[strlen(temp)-1] = '\0';
    gethostname(hostname, MAXHOSTNAMELEN);
    gasneti_tracestats_printf("Program %s (pid=%i) starting on %s at: %s", 
      argv[0], (int)getpid(), hostname, temp);
    p = temp;
    for (i=0; i < argc; i++) { 
      char *q = argv[i];
      int hasspace = 0;
      for (;*q;q++) if (isspace((int)*q)) hasspace = 1;
      if (hasspace) sprintf(p, "'%s'", argv[i]);
      else sprintf(p, "%s", argv[i]);
      if (i < argc-1) strcat(p, " ");
      p += strlen(p);
    }
    gasneti_tracestats_printf("Command-line: %s", temp);
    #if GASNET_STATS
      gasneti_stats_printf("GASNET_STATSMASK: %s", statstypes);
    #endif
    #if GASNET_TRACE
      gasneti_trace_printf("GASNET_TRACEMASK: %s", tracetypes);
    #endif
  }

  gasneti_tracestats_printf("GASNET_CONFIG_STRING: %s", GASNET_CONFIG_STRING);
  gasneti_tracestats_printf("GASNet build timestamp:   " __DATE__ " " __TIME__);
  gasneti_tracestats_printf("GASNet configure args:    %s", GASNETI_CONFIGURE_ARGS);
  gasneti_tracestats_printf("GASNet configure buildid: " GASNETI_BUILD_ID);
  gasneti_tracestats_printf("GASNet system tuple:      " GASNETI_SYSTEM_TUPLE);
  gasneti_tracestats_printf("GASNet configure system:  " GASNETI_SYSTEM_NAME);
  gasneti_tracestats_printf("gasnet_mynode(): %i", (int)gasnet_mynode());
  gasneti_tracestats_printf("gasnet_nodes(): %i", (int)gasnet_nodes());

  #if GASNET_NDEBUG
  { char *NDEBUG_warning =
     "WARNING: tracing/statistical collection may adversely affect application performance.";
    gasneti_tracestats_printf(NDEBUG_warning);
    if (gasneti_tracefile != stdout && gasneti_tracefile != stderr &&
        gasneti_statsfile != stdout && gasneti_statsfile != stderr) {
      fprintf(stderr,NDEBUG_warning);
      fprintf(stderr,"\n");
    }
  }
  #endif

  #ifdef GASNETI_USING_GETTIMEOFDAY
    gasneti_tracestats_printf("WARNING: using gettimeofday() for timing measurement - "
      "all short-term time measurements will be very rough and include significant timer overheads");
  #endif
  gasneti_tracestats_printf("Timer granularity: ~ %.3f us, overhead: ~ %.3f us",
   GASNETI_STATTIME_GRANULARITY(), GASNETI_STATTIME_OVERHEAD());

  if (!gasneti_tracefile) /* clear types entries if we're not tracing */
    memset(gasneti_tracetypes, 0, 256);
  if (!gasneti_statsfile && !gasneti_tracefile)
    memset(gasneti_statstypes, 0, 256);
  }
  #endif
}

#define AGGRNAME(cat,type) gasneti_aggregate_##cat##_##type
#define AGGR(type)                                       \
  static gasneti_statctr_t AGGRNAME(ctr,type) = 0;       \
  static gasneti_stat_intval_t AGGRNAME(intval,type) =   \
    { 0, GASNETI_STATCTR_MAX, GASNETI_STATCTR_MIN, 0 };  \
  static gasneti_stat_timeval_t AGGRNAME(timeval,type) = \
    { 0, GASNETI_STATTIME_MAX, GASNETI_STATTIME_MIN, 0 }
AGGR(G);
AGGR(P);
AGGR(S);
AGGR(W);
AGGR(X);
AGGR(B);
AGGR(L);
AGGR(A);
AGGR(I);
AGGR(C);
AGGR(D);

extern void gasneti_trace_finish() {
#if GASNETI_STATS_OR_TRACE
  static gasneti_mutex_t gasneti_tracefinishlock = GASNETI_MUTEX_INITIALIZER;
  gasneti_mutex_lock(&gasneti_tracefinishlock);
  if (gasneti_tracefile || gasneti_statsfile) {

    double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
    gasneti_tracestats_printf("Total application run time: %10.6fs", time);

    #if GASNET_STATS
    { /* output statistical summary */

      if (gasnett_stats_callback && GASNETI_STATS_ENABLED(H)) {
        gasneti_stats_printf("--------------------------------------------------------------------------------");
        (*gasnett_stats_callback)(gasneti_stats_printf);
      }

      gasneti_stats_printf("--------------------------------------------------------------------------------");
      gasneti_stats_printf("GASNet Statistical Summary:");
    
      #define ACCUM(pacc, pintval) do {                                       \
          pacc->count += pintval->count;                                      \
          if (pintval->minval < pacc->minval) pacc->minval = pintval->minval; \
          if (pintval->maxval > pacc->maxval) pacc->maxval = pintval->maxval; \
          pacc->sumval += pintval->sumval;                                    \
      } while (0)
      #define CALC_AVG(sum,count) ((count) == 0 ? (gasneti_statctr_t)-1 : (sum) / (count))
      #define DUMP_CTR(type,name,desc)                     \
        if (GASNETI_STATS_ENABLED(type)) {                 \
          gasneti_statctr_t *p = &gasneti_stat_ctr_##name; \
          gasneti_stats_printf(" %-25s %6i",               \
                  #name" "#desc":", (int)*p);              \
          AGGRNAME(ctr,type) += *p;                        \
        }
      #define DUMP_INTVAL(type,name,desc)                                          \
        if (GASNETI_STATS_ENABLED(type)) {                                         \
          gasneti_stat_intval_t *p = &gasneti_stat_intval_##name;                  \
          const char *pdesc = #desc;                                               \
          if (!p->count)                                                           \
            gasneti_stats_printf(" %-25s %6i", #name":", 0);                       \
          else                                                                     \
            gasneti_stats_printf(" %-25s %6i  avg/min/max/total %s = %i/%i/%i/%i", \
                  #name":", (int)p->count,                                         \
                  pdesc,                                                           \
                  (int)CALC_AVG(p->sumval,p->count),                               \
                  (int)p->minval,                                                  \
                  (int)p->maxval,                                                  \
                  (int)p->sumval);                                                 \
          ACCUM((&AGGRNAME(intval,type)), p);                                      \
        }
      #define DUMP_TIMEVAL(type,name,desc)                                              \
        if (GASNETI_STATS_ENABLED(type)) {                                              \
          gasneti_stat_timeval_t *p = &gasneti_stat_timeval_##name;                     \
          const char *pdesc = #desc;                                                    \
          if (!p->count)                                                                \
            gasneti_stats_printf(" %-25s %6i", #name":", 0);                            \
          else                                                                          \
            gasneti_stats_printf(" %-25s %6i  avg/min/max/total %s (us) = %i/%i/%i/%i", \
                  #name":", (int)p->count,                                              \
                  pdesc,                                                                \
                  (int)GASNETI_STATTIME_TO_US(CALC_AVG(p->sumval, p->count)),           \
                  (int)GASNETI_STATTIME_TO_US(p->minval),                               \
                  (int)GASNETI_STATTIME_TO_US(p->maxval),                               \
                  (int)GASNETI_STATTIME_TO_US(p->sumval));                              \
          ACCUM((&AGGRNAME(timeval,type)), p);                                          \
        }

      GASNETI_ALL_STATS(DUMP_CTR, DUMP_INTVAL, DUMP_TIMEVAL);

      gasneti_stats_printf(" ");
      gasneti_stats_printf(" ");

      if (GASNETI_STATS_ENABLED(G)) {
        gasneti_stat_intval_t *p = &AGGRNAME(intval,G);
        if (!p->count)
          gasneti_stats_printf("%-25s  %6i","Total gets:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total sz = %i/%i/%i/%i", "Total gets:",
            (int)p->count,
            (int)CALC_AVG(p->sumval, p->count),
            (int)p->minval,
            (int)p->maxval,
            (int)p->sumval);
      }
      if (GASNETI_STATS_ENABLED(P)) {
        gasneti_stat_intval_t *p = &AGGRNAME(intval,P);
        if (!p->count)
          gasneti_stats_printf("%-25s  %6i","Total puts:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total sz = %i/%i/%i/%i", "Total puts:",
            (int)p->count,
            (int)CALC_AVG(p->sumval, p->count),
            (int)p->minval,
            (int)p->maxval,
            (int)p->sumval);
      }
      if (GASNETI_STATS_ENABLED(W)) {
        gasneti_stat_intval_t *w = &AGGRNAME(intval,W);
        if (!w->count)
          gasneti_stats_printf("%-25s  %6i","Total collectives:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total sz = %i/%i/%i/%i", "Total collectives:",
            (int)w->count,
            (int)CALC_AVG(w->sumval, w->count),
            (int)w->minval,
            (int)w->maxval,
            (int)w->sumval);
      }
      if (GASNETI_STATS_ENABLED(S)) {
        gasneti_stat_intval_t *try_succ = &AGGRNAME(intval,S);
        gasneti_stat_timeval_t *wait_time = &AGGRNAME(timeval,S);
        if (!try_succ->count)
          gasneti_stats_printf("%-25s  %6i","Total try sync. calls:",0);
        else
          gasneti_stats_printf("%-25s  %6i  try success rate = %f%%  \n",
            "Total try sync. calls:",  ((int)try_succ->count),
            (float)(CALC_AVG((float)try_succ->sumval, try_succ->count) * 100.0));
        if (!wait_time->count)
          gasneti_stats_printf("%-25s  %6i","Total wait sync. calls:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total waittime (us) = %i/%i/%i/%i", 
            "Total wait sync. calls:", ((int)wait_time->count),
            (int)GASNETI_STATTIME_TO_US(CALC_AVG(wait_time->sumval, wait_time->count)),
            (int)GASNETI_STATTIME_TO_US(wait_time->minval),
            (int)GASNETI_STATTIME_TO_US(wait_time->maxval),
            (int)GASNETI_STATTIME_TO_US(wait_time->sumval));
      }
      if (GASNETI_STATS_ENABLED(X)) {
        gasneti_stat_intval_t *try_succ = &AGGRNAME(intval,X);
        gasneti_stat_timeval_t *wait_time = &AGGRNAME(timeval,X);
        if (!try_succ->count)
          gasneti_stats_printf("%-25s  %6i","Total coll. try syncs:",0);
        else
          gasneti_stats_printf("%-25s  %6i  collective try success rate = %f%%  \n",
            "Total coll. try syncs:",  ((int)try_succ->count),
            (float)(CALC_AVG((float)try_succ->sumval, try_succ->count) * 100.0));
        if (!wait_time->count)
          gasneti_stats_printf("%-25s  %6i","Total coll. wait syncs:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total waittime (us) = %i/%i/%i/%i", 
            "Total coll. wait syncs:", ((int)wait_time->count),
            (int)GASNETI_STATTIME_TO_US(CALC_AVG(wait_time->sumval, wait_time->count)),
            (int)GASNETI_STATTIME_TO_US(wait_time->minval),
            (int)GASNETI_STATTIME_TO_US(wait_time->maxval),
            (int)GASNETI_STATTIME_TO_US(wait_time->sumval));
      }
      if (GASNETI_STATS_ENABLED(A)) 
        gasneti_stats_printf("%-25s  %6i", "Total AM's:", (int)AGGRNAME(ctr,A));

      gasneti_stats_printf("--------------------------------------------------------------------------------");
    }
    #endif

    GASNETC_TRACE_FINISH(); /* allow for final output of conduit-specific statistics */

    gasneti_mutex_lock(&gasneti_tracelock);
    if (gasneti_tracefile && gasneti_tracefile != stdout && gasneti_tracefile != stderr) 
      fclose(gasneti_tracefile);
    if (gasneti_statsfile && gasneti_statsfile != stdout && gasneti_statsfile != stderr) 
      fclose(gasneti_statsfile);
    gasneti_tracefile = NULL;
    gasneti_statsfile = NULL;
    gasneti_mutex_unlock(&gasneti_tracelock);
    gasneti_mutex_unlock(&gasneti_tracefinishlock);
    sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */
  }
#endif
}


/* ------------------------------------------------------------------------------------ */
#if GASNET_STATS
  #define DEF_CTR(type,name,desc) \
    gasneti_statctr_t gasneti_stat_ctr_##name = 0;
  #define DEF_INTVAL(type,name,desc)                   \
    gasneti_stat_intval_t gasneti_stat_intval_##name = \
      { 0, GASNETI_STATCTR_MAX, GASNETI_STATCTR_MIN, 0 };
  #define DEF_TIMEVAL(type,name,desc)                    \
    gasneti_stat_timeval_t gasneti_stat_timeval_##name = \
      { 0, GASNETI_STATTIME_MAX, GASNETI_STATTIME_MIN, 0 };
  GASNETI_ALL_STATS(DEF_CTR, DEF_INTVAL, DEF_TIMEVAL)

static gasneti_mutex_t gasneti_statlock = GASNETI_MUTEX_INITIALIZER;
#define GASNETI_STAT_LOCK()   gasneti_mutex_lock(&gasneti_statlock);
#define GASNETI_STAT_UNLOCK() gasneti_mutex_unlock(&gasneti_statlock);

extern void gasneti_stat_count_accumulate(gasneti_statctr_t *pctr) {
  GASNETI_STAT_LOCK();
    (*pctr)++;
  GASNETI_STAT_UNLOCK();
}
extern void gasneti_stat_intval_accumulate(gasneti_stat_intval_t *pintval, gasneti_statctr_t val) {
  GASNETI_STAT_LOCK();
    pintval->count++;
    pintval->sumval += val;
    if_pf (val > pintval->maxval) pintval->maxval = val;
    if_pf (val < pintval->minval) pintval->minval = val;
  GASNETI_STAT_UNLOCK();
}
extern void gasneti_stat_timeval_accumulate(gasneti_stat_timeval_t *pintval, gasneti_stattime_t val) {
  GASNETI_STAT_LOCK();
    pintval->count++;
    pintval->sumval += val;
    if_pf (val > pintval->maxval) pintval->maxval = val;
    if_pf (val < pintval->minval) pintval->minval = val;
  GASNETI_STAT_UNLOCK();
}
#endif
/* ------------------------------------------------------------------------------------ */
#define GASNETI_GASNET_INTERNAL_C
#include "gasnet_mmap.c"
/* ------------------------------------------------------------------------------------ */
/* Debug memory management
   debug memory format:
  | prev | next | allocdesc (pad to 8 bytes) | datasz | BEGINPOST | <user data> | ENDPOST |
                                             ptr returned by malloc ^
 */
#if GASNET_DEBUG
  typedef struct gasneti_memalloc_desc {  
    struct gasneti_memalloc_desc * volatile prevdesc;
    struct gasneti_memalloc_desc * volatile nextdesc;
    uint64_t allocdesc; /* actually a (void*) */
    uint64_t datasz;
    uint64_t beginpost;
  } gasneti_memalloc_desc_t;
  static uint64_t gasneti_memalloc_allocatedbytes = 0;   /* num bytes ever allocated */
  static uint64_t gasneti_memalloc_freedbytes = 0;       /* num bytes ever freed */
  static uint64_t gasneti_memalloc_allocatedobjects = 0; /* num objects ever allocated */
  static uint64_t gasneti_memalloc_freedobjects = 0;     /* num objects ever freed */
  static uint64_t gasneti_memalloc_ringobjects = 0;      /* num objects in the ring */
  static uint64_t gasneti_memalloc_ringbytes = 0;        /* num bytes in the ring */
  static size_t   gasneti_memalloc_maxobjectsize = 0;    /* max object size ever allocated */
  static uintptr_t gasneti_memalloc_maxobjectloc = 0;    /* max address ever allocated */
  static uint64_t gasneti_memalloc_maxlivebytes = 0;     /* max num bytes live at any given time */
  static uint64_t gasneti_memalloc_maxliveobjects = 0;   /* max num bytes live at any given time */
  static int gasneti_memalloc_extracheck = 0;
  static int gasneti_memalloc_init = -1;
  static uint64_t gasneti_memalloc_initval = 0;
  static int gasneti_memalloc_clobber = -1;
  static uint64_t gasneti_memalloc_clobberval = 0;
  static int gasneti_memalloc_leakall = -1;
  static int gasneti_memalloc_scanfreed = -1;
  static int gasneti_memalloc_envisinit = 0;
  static gasneti_mutex_t gasneti_memalloc_lock = GASNETI_MUTEX_INITIALIZER;
  static gasneti_memalloc_desc_t *gasneti_memalloc_pos = NULL;
  #define GASNETI_MEM_BEGINPOST   ((uint64_t)0xDEADBABEDEADBABEllu)
  #define GASNETI_MEM_ENDPOST     ((uint64_t)0xCAFEDEEDCAFEDEEDllu)
  #define GASNETI_MEM_FREEMARK    ((uint64_t)0xBEEFEFADBEEFEFADllu)
  #define GASNETI_MEM_HEADERSZ    (sizeof(gasneti_memalloc_desc_t))
  #define GASNETI_MEM_TAILSZ      8     
  #define GASNETI_MEM_EXTRASZ     (GASNETI_MEM_HEADERSZ+GASNETI_MEM_TAILSZ)     
  #define GASNETI_MEM_MALLOCALIGN 4
  #define gasneti_looksaligned(p) (!(((uintptr_t)(p)) & (GASNETI_MEM_MALLOCALIGN-1)))

  static uint64_t gasneti_memalloc_envint(const char *name, const char *deflt) {
    /* Signaling NaN: any bit pattern between 0x7ff0000000000001 and 0x7ff7ffffffffffff  
                   or any bit pattern between 0xfff0000000000001 and 0xfff7ffffffffffff
       Quiet NaN: any bit pattern between 0x7ff8000000000000 and 0x7fffffffffffffff 
               or any bit pattern between 0xfff8000000000000 and 0xffffffffffffffff
    */
    uint64_t sNAN = 0x7ff7ffffffffffffllu; 
    uint64_t qNAN = 0x7fffffffffffffffllu;
    uint64_t val = 0;
    const char *envval = gasneti_getenv_withdefault(name, deflt);
    const char *p = envval;
    char tmp[255];
    int i = 0;
    for ( ; *p; p++) {
      if (!isspace(*p)) tmp[i++] = toupper(*p);
      if (i == 254) break;
    }
    tmp[i] = '\0';
    if (!strcmp(tmp, "NAN")) return sNAN;
    else if (!strcmp(tmp, "SNAN")) return sNAN;
    else if (!strcmp(tmp, "QNAN")) return qNAN;
    else if (!strncmp(tmp, "0X", 2)) { /* hex value */
      p = tmp+2;
      if (strlen(p) > 16) gasneti_fatalerror("too many digits in hex value %s=%s", name, envval);
      for ( ; *p; p++) {
        uint8_t byte;
        if (*p >= '0' && *p <= '9') byte = *p - '0';
        else if (*p >= 'A' && *p <= 'F') byte = *p - 'A';
        else gasneti_fatalerror("illegal hex value %s=%s", name, envval);
        val = (val << 4) | (uint64_t)byte;
      } 
    } else { /* int rep */
      int neg = 0;
      p = tmp;
      if (*p == '-') { neg = 1; p++; }
      for ( ; *p; p++) {
        uint8_t digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else gasneti_fatalerror("illegal decimal value %s=%s", name, envval);
        val = (val * 10) + (uint64_t)digit;
      } 
      if (neg) val = (uint64_t)(-(int64_t)val);
    }
    if (val <= 0xFF) {
      int i;
      uint64_t byte = val;
      for (i = 0; i < 7; i++) {
        val = (val << 8) | byte;
      }
    }
    return val;
  }
  static void gasneti_memalloc_valset(void *p, size_t len, uint64_t val) {
    uint64_t *output = p;
    size_t blocks = len/8;
    size_t extra = len%8;
    size_t i;
    for (i = 0; i < blocks; i++) {
      *output = val; 
      output++;
    }
    if (extra) memcpy(output, &val, extra);
  }
  static const void *gasneti_memalloc_valcmp(const void *p, size_t len, uint64_t val) {
    const uint64_t *input = p;
    size_t blocks = len/8;
    size_t extra = len%8;
    size_t i;
    for (i = 0; i < blocks; i++) {
      if (*input != val) {
        const uint8_t *in = (uint8_t *)input;
        const uint8_t *cmp = (uint8_t *)&val;
        for (i = 0; i < 8; i++, in++, cmp++)
          if (*in != *cmp) return in;
        gasneti_fatalerror("bizarre failure in gasneti_memalloc_valcmp");
      }
      input++;
    }
    if (extra) {
      const uint8_t *in = (uint8_t *)input;
      const uint8_t *cmp = (uint8_t *)&val;
      for (i = 0; i < extra; i++, in++, cmp++)
        if (*in != *cmp) return in;
    }
    return NULL;
  }

  GASNET_INLINE_MODIFIER(gasneti_memalloc_envinit)
  void gasneti_memalloc_envinit() {
    if (!gasneti_memalloc_envisinit) {
      gasneti_mutex_lock(&gasneti_memalloc_lock);
        if (!gasneti_memalloc_envisinit && gasneti_init_done) {
          gasneti_memalloc_envisinit = 1; /* set first, because getenv might call malloc when tracing */
          gasneti_memalloc_init =       gasneti_getenv_yesno_withdefault("GASNET_MALLOC_INIT",0);
          gasneti_memalloc_initval =    gasneti_memalloc_envint("GASNET_MALLOC_INITVAL","NAN");
          gasneti_memalloc_clobber =    gasneti_getenv_yesno_withdefault("GASNET_MALLOC_CLOBBER",0);
          gasneti_memalloc_clobberval = gasneti_memalloc_envint("GASNET_MALLOC_CLOBBERVAL","NAN");
          gasneti_memalloc_leakall =    gasneti_getenv_yesno_withdefault("GASNET_MALLOC_LEAKALL", 0);
          gasneti_memalloc_scanfreed =  gasneti_getenv_yesno_withdefault("GASNET_MALLOC_SCANFREED", 0);
          gasneti_memalloc_extracheck = gasneti_getenv_yesno_withdefault("GASNET_MALLOC_EXTRACHECK", 0);
          if (gasneti_memalloc_scanfreed && !gasneti_memalloc_clobber) {
            gasneti_memalloc_clobber = 1;
            if (gasneti_mynode == 0) { 
              fprintf(stderr, "WARNING: GASNET_MALLOC_SCANFREED requires GASNET_MALLOC_CLOBBER: enabling it.\n");
              fflush(stderr);
            }
          }
          if (gasneti_memalloc_scanfreed && !gasneti_memalloc_leakall) {
            gasneti_memalloc_leakall = 1;
            if (gasneti_mynode == 0) { 
              fprintf(stderr, "WARNING: GASNET_MALLOC_SCANFREED requires GASNET_MALLOC_LEAKALL: enabling it.\n");
              fflush(stderr);
            }
          }
        }
      gasneti_mutex_unlock(&gasneti_memalloc_lock);
    }
  }

  extern void _gasneti_memcheck_one(const char *curloc) {
    if (gasneti_memalloc_extracheck) _gasneti_memcheck_all(curloc);
    else {
      if_pt (gasneti_attach_done) gasnet_hold_interrupts();
      gasneti_mutex_lock(&gasneti_memalloc_lock);
        if (gasneti_memalloc_pos) {
          _gasneti_memcheck(gasneti_memalloc_pos+1, curloc, 2);
          gasneti_memalloc_pos = gasneti_memalloc_pos->nextdesc;
        } else gasneti_assert(gasneti_memalloc_ringobjects == 0 && gasneti_memalloc_ringbytes == 0);
      gasneti_mutex_unlock(&gasneti_memalloc_lock);
      if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    }
  }
  extern void _gasneti_memcheck_all(const char *curloc) {
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    gasneti_mutex_lock(&gasneti_memalloc_lock);
      if (gasneti_memalloc_pos) {
        gasneti_memalloc_desc_t *begin = gasneti_memalloc_pos;
        uint64_t cnt;
        uint64_t sumsz = 0;
        for (cnt=0; cnt < gasneti_memalloc_ringobjects; cnt++) {
          sumsz += _gasneti_memcheck(gasneti_memalloc_pos+1, curloc, 2);
          gasneti_memalloc_pos = gasneti_memalloc_pos->nextdesc;
          if (gasneti_memalloc_pos == begin) break;
        } 
        if (cnt+1 != gasneti_memalloc_ringobjects || gasneti_memalloc_pos != begin || 
            sumsz != gasneti_memalloc_ringbytes) {
          gasneti_fatalerror("Debug malloc memcheck_all (called at %s) detected an error "
                             "in the memory ring linkage, most likely as a result of memory corruption.", 
                             curloc);
        }
      } else gasneti_assert(gasneti_memalloc_ringobjects == 0 && gasneti_memalloc_ringbytes == 0);
    gasneti_mutex_unlock(&gasneti_memalloc_lock);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }

  /* assert the integrity of given memory block and return size of the user object 
      checktype == 0: check a live object
      checktype == 1: check an object which is about to be freed
      checktype == 2: check an object which resides in the ring (and may be dead)
  */
  extern size_t _gasneti_memcheck(void *ptr, const char *curloc, int checktype) {
    const char *corruptstr = NULL;
    char tmpstr[255];
    size_t nbytes = 0;
    char *allocptr = NULL;
    uint64_t beginpost = 0;
    uint64_t endpost = 0;
    int doscan = 0;
    gasneti_assert(checktype >= 0 && checktype <= 2);
    if (gasneti_looksaligned(ptr)) {
      gasneti_memalloc_desc_t *desc = ((gasneti_memalloc_desc_t *)ptr) - 1;
      beginpost = desc->beginpost;
      nbytes = (size_t)desc->datasz;
      if (nbytes == 0 || nbytes > gasneti_memalloc_maxobjectsize || 
          ((uintptr_t)ptr)+nbytes > gasneti_memalloc_maxobjectloc ||
          !desc->prevdesc || !desc->nextdesc || 
          !gasneti_looksaligned(desc->prevdesc) || 
          !gasneti_looksaligned(desc->nextdesc)) {
            nbytes = 0; /* bad metadata, don't trust any of it */
      } else {
        allocptr = (void *)(uintptr_t)desc->allocdesc;
        memcpy(&endpost,((char*)ptr)+nbytes,GASNETI_MEM_TAILSZ);
      }
    }
    if (beginpost == GASNETI_MEM_FREEMARK) {
      switch (checktype) {
        case 0: /* should be a live object */
          corruptstr = "Debug malloc memcheck() called on freed memory (may indicate local heap corruption)";
          break;
        case 1: /* about to be freed - should still be a live object */
          corruptstr = "Debug free detected a duplicate free() or local heap corruption";
          break;
        case 2:
          if (gasneti_memalloc_scanfreed <= 0) /* freed objects should not be in ring */
            corruptstr = "Debug malloc found a freed object in the memory ring, indicating local heap corruption";
          else doscan = 1;
          break;
      }
    }  
    if (beginpost != GASNETI_MEM_FREEMARK && 
        (beginpost != GASNETI_MEM_BEGINPOST || endpost != GASNETI_MEM_ENDPOST)) {
      const char *diagnosis = "a bad pointer or local heap corruption";
      if (nbytes && beginpost == GASNETI_MEM_BEGINPOST && endpost != GASNETI_MEM_ENDPOST)
        diagnosis = "local heap corruption (probable buffer overflow)";
      else if (nbytes && beginpost != GASNETI_MEM_BEGINPOST && endpost == GASNETI_MEM_ENDPOST)
        diagnosis = "local heap corruption (probable buffer underflow)";
      if (checktype == 1) {
        sprintf(tmpstr, "Debug free detected %s", diagnosis);
      } else {
        sprintf(tmpstr, "Debug malloc memcheck() detected %s", diagnosis);
      }
      corruptstr = tmpstr;
    }
    if (corruptstr == NULL && doscan) {
      const void *badloc = gasneti_memalloc_valcmp(ptr, nbytes, gasneti_memalloc_clobberval);
      if (badloc) {
        sprintf(tmpstr, "Debug malloc memcheck() detected a write to freed memory at object offset: %i bytes",
                        (int)((uintptr_t)badloc - (uintptr_t)ptr));
        corruptstr = tmpstr;
      }
    }

    if (corruptstr != NULL) {
      char nbytesstr[80];
      if (allocptr != NULL && memchr(allocptr,'\0',255) == 0) /* allocptr may be bad */
        allocptr = NULL; 
      if (allocptr == NULL) nbytesstr[0] = '\0';
      else sprintf(nbytesstr," nbytes=%i",(int)nbytes);
      gasneti_fatalerror("%s\n   ptr="GASNETI_LADDRFMT"%s%s%s%s%s",
           corruptstr,
           GASNETI_LADDRSTR(ptr), nbytesstr,
           (allocptr!=NULL?"\n   allocated at: ":""), (allocptr!=NULL?allocptr:""),
           (curloc!=NULL?(checktype == 1?"\n   freed at: ":"\n   detected at: "):""), 
           (curloc!=NULL?curloc:"")
           );
    }
    return nbytes;
  }

  /* get access to system malloc/free */
  #undef malloc
  #undef free
  static void *_gasneti_malloc_inner(int allowfail, size_t nbytes, const char *curloc) {
    void *ret = NULL;
    gasneti_memalloc_envinit();
    _gasneti_memcheck_one(curloc);
    GASNETI_STAT_EVENT_VAL(I, GASNET_MALLOC, nbytes);
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    if_pf (nbytes == 0) {
      if_pt (gasneti_attach_done) gasnet_resume_interrupts();
      return NULL;
    }
    ret = malloc(nbytes+GASNETI_MEM_EXTRASZ);
    gasneti_assert((((uintptr_t)ret) & 0x3) == 0); /* should have at least 4-byte alignment */
    if_pf (ret == NULL) {
      if (allowfail) {
        if_pt (gasneti_attach_done) gasnet_resume_interrupts();
        GASNETI_TRACE_PRINTF(I,("Warning: returning NULL for a failed gasneti_malloc(%i): %s",
                                (int)nbytes, (curloc == NULL ? (const char *)"" : curloc)));
        return NULL;
      }
      gasneti_fatalerror("Debug malloc(%d) failed (%lu bytes in use, in %lu objects): %s", 
        (int)nbytes, (unsigned long)(gasneti_memalloc_allocatedobjects - gasneti_memalloc_freedobjects),
                     (unsigned long)(gasneti_memalloc_allocatedbytes - gasneti_memalloc_freedbytes),
                     (curloc == NULL ? (const char *)"" : curloc));
    } else {
      uint64_t gasneti_endpost_ref = GASNETI_MEM_ENDPOST;
      gasneti_memalloc_desc_t *desc = ret;
      desc->allocdesc = (uint64_t)(uintptr_t)curloc;
      desc->datasz = (uint64_t)nbytes;
      desc->beginpost = GASNETI_MEM_BEGINPOST;
      memcpy(((char*)ret)+nbytes+GASNETI_MEM_HEADERSZ, &gasneti_endpost_ref, GASNETI_MEM_TAILSZ);

      gasneti_mutex_lock(&gasneti_memalloc_lock);
        gasneti_memalloc_allocatedbytes += nbytes;
        gasneti_memalloc_allocatedobjects++;
        gasneti_memalloc_ringobjects++;
        gasneti_memalloc_ringbytes += nbytes;
        if (nbytes > gasneti_memalloc_maxobjectsize) gasneti_memalloc_maxobjectsize = nbytes;
        if (((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ > gasneti_memalloc_maxobjectloc) 
          gasneti_memalloc_maxobjectloc = ((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ;
        gasneti_memalloc_maxlivebytes = 
          MAX(gasneti_memalloc_maxlivebytes, gasneti_memalloc_allocatedbytes-gasneti_memalloc_freedbytes);
        gasneti_memalloc_maxliveobjects = 
          MAX(gasneti_memalloc_maxliveobjects, gasneti_memalloc_allocatedobjects-gasneti_memalloc_freedobjects);
        if (gasneti_memalloc_pos == NULL) { /* first object */
          gasneti_memalloc_pos = desc;
          desc->prevdesc = desc;
          desc->nextdesc = desc;
        } else { /* link into ring */
          desc->prevdesc = gasneti_memalloc_pos->prevdesc;
          desc->nextdesc = gasneti_memalloc_pos;
          gasneti_memalloc_pos->prevdesc->nextdesc = desc;
          gasneti_memalloc_pos->prevdesc = desc;
        }
      gasneti_mutex_unlock(&gasneti_memalloc_lock);

      ret = desc+1;
      if (gasneti_memalloc_init > 0) gasneti_memalloc_valset(ret, nbytes, gasneti_memalloc_initval);
    }
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern void *_gasneti_malloc(size_t nbytes, const char *curloc) {
    return _gasneti_malloc_inner(0, nbytes, curloc);
  }
  extern void *_gasneti_malloc_allowfail(size_t nbytes, const char *curloc) {
    return _gasneti_malloc_inner(1, nbytes, curloc);
  }

  extern void _gasneti_free(void *ptr, const char *curloc) {
    size_t nbytes;
    gasneti_memalloc_desc_t *desc;
    gasneti_memalloc_envinit();
    _gasneti_memcheck_one(curloc);
    if_pf (ptr == NULL) return;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    nbytes = _gasneti_memcheck(ptr, curloc, 1);
    GASNETI_STAT_EVENT_VAL(I, GASNET_FREE, nbytes);
    desc = ((gasneti_memalloc_desc_t *)ptr) - 1;
    if (gasneti_memalloc_clobber > 0) gasneti_memalloc_valset(desc+1, nbytes, gasneti_memalloc_clobberval);

    gasneti_mutex_lock(&gasneti_memalloc_lock);
      desc->beginpost = GASNETI_MEM_FREEMARK;
      gasneti_memalloc_freedbytes += nbytes;
      gasneti_memalloc_freedobjects++;
      if (gasneti_memalloc_scanfreed <= 0) {
        gasneti_memalloc_ringobjects--;
        gasneti_memalloc_ringbytes -= nbytes;
        if (desc->nextdesc == desc) { /* last item in list */
          gasneti_assert(desc->prevdesc == desc && gasneti_memalloc_ringobjects == 0);
          gasneti_memalloc_pos = NULL;
        } else {
          if (gasneti_memalloc_pos == desc) gasneti_memalloc_pos = desc->nextdesc;
          desc->prevdesc->nextdesc = desc->nextdesc;
          desc->nextdesc->prevdesc = desc->prevdesc;
        }
      }
    gasneti_mutex_unlock(&gasneti_memalloc_lock);

    if (gasneti_memalloc_leakall <= 0) free(desc);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }

  extern void *_gasneti_calloc(size_t N, size_t S, const char *curloc) {
    void *ret;
    size_t nbytes = N*S;
    if_pf (nbytes == 0) return NULL;
    ret = _gasneti_malloc(nbytes, curloc);
    memset(ret,0,nbytes);
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern void *_gasneti_realloc(void *ptr, size_t sz, const char *curloc) {
    void *ret = _gasneti_malloc(sz, curloc);
    memcpy(ret,ptr,sz);
    _gasneti_free(ptr, curloc);
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern int gasneti_getheapstats(gasneti_heapstats_t *pstat) {
    pstat->allocated_bytes = gasneti_memalloc_allocatedbytes;
    pstat->freed_bytes = gasneti_memalloc_freedbytes;
    pstat->live_bytes = gasneti_memalloc_allocatedbytes - gasneti_memalloc_freedbytes;
    pstat->live_bytes_max = gasneti_memalloc_maxlivebytes;
    pstat->allocated_objects = gasneti_memalloc_allocatedobjects;
    pstat->freed_objects = gasneti_memalloc_freedobjects;
    pstat->live_objects = gasneti_memalloc_allocatedobjects - gasneti_memalloc_freedobjects;
    pstat->live_objects_max = gasneti_memalloc_maxliveobjects;
    pstat->overhead_bytes = gasneti_memalloc_ringbytes - pstat->live_bytes + 
                            gasneti_memalloc_ringobjects*GASNETI_MEM_EXTRASZ;
    return 0;
  }
#endif
/* extern versions of gasnet malloc fns for use in public headers */
extern void *_gasneti_extern_malloc(size_t sz GASNETI_CURLOCFARG) {
  return _gasneti_malloc(sz GASNETI_CURLOCPARG);
}
extern void *_gasneti_extern_realloc(void *ptr, size_t sz GASNETI_CURLOCFARG) {
  return _gasneti_realloc(ptr, sz GASNETI_CURLOCPARG);
}
extern void *_gasneti_extern_calloc(size_t N, size_t S GASNETI_CURLOCFARG) {
  return _gasneti_calloc(N,S GASNETI_CURLOCPARG);
}
extern void _gasneti_extern_free(void *ptr GASNETI_CURLOCFARG) {
  _gasneti_free(ptr GASNETI_CURLOCPARG);
}
extern char *_gasneti_extern_strdup(const char *s GASNETI_CURLOCFARG) {
  return _gasneti_strdup(s GASNETI_CURLOCPARG);
}
extern char *_gasneti_extern_strndup(const char *s, size_t n GASNETI_CURLOCFARG) {
  return _gasneti_strndup(s,n GASNETI_CURLOCPARG);
}

#if GASNET_DEBUG
  extern void *(*gasnett_debug_malloc_fn)(size_t sz GASNETI_CURLOCFARG);
  extern void *(*gasnett_debug_calloc_fn)(size_t N, size_t S GASNETI_CURLOCFARG);
  extern void (*gasnett_debug_free_fn)(void *ptr GASNETI_CURLOCFARG);
  void *(*gasnett_debug_malloc_fn)(size_t sz GASNETI_CURLOCFARG) =
         &_gasneti_extern_malloc;
  void *(*gasnett_debug_calloc_fn)(size_t N, size_t S GASNETI_CURLOCFARG) =
         &_gasneti_extern_calloc;
  void (*gasnett_debug_free_fn)(void *ptr GASNETI_CURLOCFARG) =
         &_gasneti_extern_free;
#endif

/* don't put anything here - malloc stuff must come last */
