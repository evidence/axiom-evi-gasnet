/*  $Archive:: /Ti/GASNet/gasnet_internal.c                               $
 *     $Date: 2002/11/28 05:50:18 $
 * $Revision: 1.20 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include <signal.h>
#ifdef IRIX
#define signal(a,b) bsd_signal(a,b)
#endif

#include <gasnet.h>
#include <gasnet_internal.h>

/* set to non-zero for verbose error reporting */
int gasneti_VerboseErrors = 1;

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  gasnet_hsl_t gasneti_atomicop_lock = GASNET_HSL_INITIALIZER;
#endif

#define GASNET_VERSION_STR  _STRINGIFY(GASNET_VERSION)
GASNETI_IDENT(gasneti_IdentString_APIVersion, "$GASNetAPIVersion: " GASNET_VERSION_STR " $");

#define GASNET_CONFIG_STR _STRINGIFY(GASNET_CONFIG)
GASNETI_IDENT(gasneti_IdentString_Config, "$GASNetConfig: GASNET_" GASNET_CONFIG_STR " $");

#define GASNET_SEGMENT_CONFIG_STR _STRINGIFY(GASNETI_SEGMENT_CONFIG)
GASNETI_IDENT(gasneti_IdentString_SegConfig, "$GASNetSegmentConfig: GASNET_SEGMENT_" GASNET_SEGMENT_CONFIG_STR " $");

/* ------------------------------------------------------------------------------------ */
extern void gasneti_fatalerror(char *msg, ...) {
  va_list argptr;
  char expandedmsg[255];

  va_start(argptr, msg); /*  pass in last argument */
  sprintf(expandedmsg, "*** GASNet FATAL ERROR: %s\n", msg);
  vfprintf(stderr, expandedmsg, argptr);
  fflush(stderr);
  va_end(argptr);

  abort();
}
/* ------------------------------------------------------------------------------------ */
extern int64_t gasneti_getMicrosecondTimeStamp(void) {
    int64_t retval;
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
	perror("gettimeofday");
	abort();
    }
    retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
    return retval;
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
size_t gasneti_getSystemPageSize() {
  #ifdef CRAYT3E
    /* on Cray: shmemalign allocates mem aligned across nodes, 
        but there seems to be no fixed page size (man pagesize)
        this is probably because they don't support VM
       actual page size is set separately for each linker section, 
        ranging from 512KB(default) to 8MB
       Here we return 1 to reflect the lack of page alignment constraints
   */
    return 1;
  #elif 1
    size_t pagesz = getpagesize();
    assert(pagesz > 0);
    return pagesz;
  #else
    /* alternate method that works on many systems */
    size_t pagesz = sysconf(_SC_PAGE_SIZE)
    assert(pagesz > 0);
    return pagesz;
  #endif
}
/* ------------------------------------------------------------------------------------ */
static volatile int gasnet_frozen = TRUE;
/*  all this to make sure we get a full stack frame for debugger */
static void _freezeForDebugger(int depth) {
  if (!depth) _freezeForDebugger(1);
  else {
    volatile int i;
    while (gasnet_frozen) {
      i++;
      sleep(1);
    }
  }
}
extern void gasneti_freezeForDebugger() {
  char name[255];
  gethostname(name, 255);
  fprintf(stderr,"GASNet node frozen for debugger: host=%s  pid=%i\n", name, getpid()); 
  fflush(stderr);
  _freezeForDebugger(0);
}
/* ------------------------------------------------------------------------------------ */
static gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp) {
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
  assert(signame);

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
      fprintf(stderr,"*** GASNet caught a fatal signal: %s(%i) on node %i/%i\n",
        signame, sig, gasnet_mynode(), gasnet_nodes()); 
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
      fprintf(stderr,"*** GASNet caught a signal: %s(%i) on node %i/%i\n",
        signame, sig, gasnet_mynode(), gasnet_nodes()); 
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
/* ------------------------------------------------------------------------------------ */
/* GASNet Tracing and Statistics */

#ifdef TRACE
  GASNETI_IDENT(gasneti_IdentString_trace, "$GASNetTracingEnabled: 1 $");
#endif
#ifdef STATS
  GASNETI_IDENT(gasneti_IdentString_stats, "$GASNetStatisticsEnabled: 1 $");
#endif

static gasneti_mutex_t gasneti_tracelock = GASNETI_MUTEX_INITIALIZER;
char gasneti_tracetypes[256];
FILE *gasneti_tracefile = NULL;
gasneti_stattime_t starttime;

#if defined(STATS) || defined(TRACE)
  #define BUILD_STATS(type,name,desc) { #type, #name, #desc },
  gasneti_statinfo_t gasneti_stats[] = {
    GASNETI_ALL_STATS(BUILD_STATS, BUILD_STATS, BUILD_STATS)
    {NULL, NULL, NULL}
  };

  #define BUFSZ     1024
  #define NUMBUFS   32
  static char gasneti_printbufs[NUMBUFS][BUFSZ];
  static int gasneti_curbuf = 0;
  static gasneti_mutex_t gasneti_buflock = GASNETI_MUTEX_INITIALIZER;

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
  extern char *gasneti_dynsprintf(char *format, ...) {
    va_list argptr;
    char *output = gasneti_getbuf();

    va_start(argptr, format); /*  pass in last argument */
      { int sz = vsnprintf(output, BUFSZ, format, argptr);
        assert(sz <= BUFSZ);
        if (sz >= (BUFSZ-5)) strcpy(output+(BUFSZ-5),"...");
      }
    va_end(argptr);
    return output;
  }

  #define BYTES_PER_LINE 16
  #define MAX_LINES 10
  /* format a block of data into a string and return it - 
     caller should not deallocate string, they are recycled automatically
   */
  extern char *gasneti_formatdata(void *p, int nbytes) { 
    uint8_t *data = (uint8_t *)p;
    char *output = gasneti_getbuf();
    *output = '\0';
    if (nbytes <= 8) { /* fits on one line */
      int i;
      for (i=0; i < nbytes; i++) {
        char temp[5];
        sprintf(temp,"%02x ",(int)data[i]);
        strcat(output, temp);
      }
    } else {
      int line;
      int col;
      int byteidx = 0;
      strcat(output,"\n");
      for (line=0;line<MAX_LINES && byteidx<nbytes;line++) {
        char nicefmt[BYTES_PER_LINE+1];
        char lineheader[10];
        nicefmt[0] = '\0';
        sprintf(lineheader, "  0x%-2x:  ", byteidx);
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

  /* dump message to tracefile */
  extern void gasneti_trace_output(char *type, char *msg, int traceheader) {
    if (gasneti_tracefile) {
      double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
      gasneti_mutex_lock(&gasneti_tracelock);
        if (gasneti_tracefile) {
          if (traceheader) {
            #ifdef GASNETI_THREADS
              fprintf(gasneti_tracefile, "%i(%x) %8.6fs> (%c) %s%s", 
                gasnet_mynode(), (uintptr_t)pthread_self(), time, *type, msg,
                (msg[strlen(msg)-1]=='\n'?"":"\n"));
            #else
              fprintf(gasneti_tracefile, "%i %8.6fs> (%c) %s%s", gasnet_mynode(), time, *type, msg,
                      (msg[strlen(msg)-1]=='\n'?"":"\n"));
            #endif
          } else {
              fprintf(gasneti_tracefile, "%i> (%c) %s%s", gasnet_mynode(), *type, msg,
                      (msg[strlen(msg)-1]=='\n'?"":"\n"));
          }
          fflush(gasneti_tracefile);
        }
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  /* dump message to tracefile with simple header */
  static void gasneti_trace_printf(char *format, ...) {
    va_list argptr;
    if (gasneti_tracefile) {
      gasneti_mutex_lock(&gasneti_tracelock);
      if (gasneti_tracefile) {
        fprintf(gasneti_tracefile, "%i> ", gasnet_mynode());
        va_start(argptr, format); /*  pass in last argument */
          vfprintf(gasneti_tracefile, format, argptr);
        va_end(argptr);
        if (format[strlen(format)-1]!='\n') fprintf(gasneti_tracefile, "\n");
        fflush(gasneti_tracefile);
      }
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
#endif

extern void gasneti_trace_init() {

  #if defined(STATS) || defined(TRACE)
  char *tracetypes = NULL;
  { /* setup tracetypes */
    char *types = gasnet_getenv("GASNET_TRACEMASK");
    if (!types) types = GASNETI_ALLTYPES;
    tracetypes = types;
    while (*types) {
      gasneti_tracetypes[(int)(*types)] = 1;
      types++;
    }
  }

  { /* setup tracefile */
    char pathtemp[255];
    char *tracefilename = gasnet_getenv("GASNET_TRACEFILE");
    if (!tracefilename || !strcmp(tracefilename, "")) {
      tracefilename = NULL;
      gasneti_tracefile = NULL;
    }
    else if (!strcmp(tracefilename, "stderr") ||
             !strcmp(tracefilename, "-")) gasneti_tracefile = stderr;
    else if (!strcmp(tracefilename, "stdout")) gasneti_tracefile = stdout;
    else {
      strcpy(pathtemp,tracefilename);
      tracefilename = pathtemp;
      while (strchr(tracefilename,'%')) { /* replace any '%' with node num */
        char temp[255];
        char *p = strchr(tracefilename,'%');
        *p = '\0';
        sprintf(temp,"%s%i%s",tracefilename,gasnet_mynode(),p+1);
        strcpy(tracefilename,temp);
      }
      gasneti_tracefile = fopen(tracefilename, "wt");
      if (!gasneti_tracefile) {
        fprintf(stderr, "ERROR: Failed to open '%s' for tracing output. Redirecting to stderr.\n",
                tracefilename);
        tracefilename = "stderr";
        gasneti_tracefile = stderr;
      }
    }

    if (gasneti_tracefile) {
      fprintf(stderr, "GASNet %s reporting enabled - output directed to %s\n", 
        #ifdef TRACE
          "trace"
        #else
          ""
        #endif
        #if defined(STATS) && defined(TRACE)
          " and "
        #else 
          ""
        #endif
        #ifdef STATS
          "statistical"
        #else
          ""
        #endif
        , tracefilename);

      { time_t ltime;
        char temp[255];
        time(&ltime); 
        strcpy(temp, ctime(&ltime));
        if (temp[strlen(temp)-1] == '\n') temp[strlen(temp)-1] = '\0';
        gasneti_trace_printf("Program starting at: %s", temp);
        gasneti_trace_printf("GASNET_TRACEMASK: %s", tracetypes);
      }
    }

    #ifdef NDEBUG
    { char *NDEBUG_warning =
       "WARNING: tracing/statistical collection may adversely affect application performance.";
      gasneti_trace_printf(NDEBUG_warning);
      if (gasneti_tracefile != stdout && gasneti_tracefile != stderr) {
        fprintf(stderr,NDEBUG_warning);
        fprintf(stderr,"\n");
      }
    }
    #endif

    #ifdef GASNETI_USING_GETTIMEOFDAY
      gasneti_trace_printf("WARNING: using gettimeofday() for timing measurement - "
        "all short-term time measurements will be very rough and include significant timer overheads");
    #endif
    if (gasneti_tracefile) { 
      int i, ticks, iters = 1000, minticks = 10;
      gasneti_stattime_t min = GASNETI_STATTIME_MAX;
      gasneti_stattime_t start = GASNETI_STATTIME_NOW();
      gasneti_stattime_t last = start;
      for (i=0,ticks=0; i < iters || ticks < minticks; i++) {
        gasneti_stattime_t x = GASNETI_STATTIME_NOW();
        gasneti_stattime_t curr = (x - last);
        if (curr > 0) { 
          ticks++;
          if (curr < min) min = curr;
        }
        last = x;
      }
      { int granularity = (int)GASNETI_STATTIME_TO_US(min);
        double overhead = ((double)(GASNETI_STATTIME_TO_US(last)-GASNETI_STATTIME_TO_US(start)))/i;
        if (granularity == 0)
          gasneti_trace_printf("Timer granularity: < 1 us, overhead: ~ %.3f us",
            overhead);
        else
          gasneti_trace_printf("Timer granularity: ~ %i us, overhead: ~ %.3f us",
            granularity, overhead);
      }
    }
  }

    starttime = GASNETI_STATTIME_NOW();
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
AGGR(B);
AGGR(L);
AGGR(A);
AGGR(I);
AGGR(C);
AGGR(D);

extern void gasneti_trace_finish() {
#if defined(STATS) || defined(TRACE)
  static gasneti_mutex_t gasneti_tracefinishlock = GASNETI_MUTEX_INITIALIZER;
  gasneti_mutex_lock(&gasneti_tracefinishlock);
  if (gasneti_tracefile) {

    double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
    gasneti_trace_printf("Total application run time: %10.6fs", time);

    #ifdef STATS
    { /* output statistical summary */

      gasneti_trace_printf("--------------------------------------------------------------------------------");
      gasneti_trace_printf("GASNet Statistical Summary:");
    
      #define ACCUM(pacc, pintval) do {                                       \
          pacc->count += pintval->count;                                      \
          if (pintval->minval < pacc->minval) pacc->minval = pintval->minval; \
          if (pintval->maxval > pacc->maxval) pacc->maxval = pintval->maxval; \
          pacc->sumval += pintval->sumval;                                    \
      } while (0)
      #define CALC_AVG(sum,count) ((count) == 0 ? -1 : (sum) / (count))
      #define DUMP_CTR(type,name,desc)                     \
        if (GASNETI_TRACE_ENABLED(type)) {                 \
          gasneti_statctr_t *p = &gasneti_stat_ctr_##name; \
          gasneti_trace_printf(" %-25s %6i",               \
                  #name" "#desc":", (int)*p);              \
          AGGRNAME(ctr,type) += *p;                        \
        }
      #define DUMP_INTVAL(type,name,desc)                                          \
        if (GASNETI_TRACE_ENABLED(type)) {                                         \
          gasneti_stat_intval_t *p = &gasneti_stat_intval_##name;                  \
          char *pdesc = #desc;                                                     \
          if (!p->count)                                                           \
            gasneti_trace_printf(" %-25s %6i", #name":", 0);                       \
          else                                                                     \
            gasneti_trace_printf(" %-25s %6i  avg/min/max/total %s = %i/%i/%i/%i", \
                  #name":", (int)p->count,                                         \
                  pdesc,                                                           \
                  (int)CALC_AVG(p->sumval,p->count),                               \
                  (int)p->minval,                                                  \
                  (int)p->maxval,                                                  \
                  (int)p->sumval);                                                 \
          ACCUM((&AGGRNAME(intval,type)), p);                                      \
        }
      #define DUMP_TIMEVAL(type,name,desc)                                         \
        if (GASNETI_TRACE_ENABLED(type)) {                                         \
          gasneti_stat_timeval_t *p = &gasneti_stat_timeval_##name;                \
          char *pdesc = #desc;                                                     \
          if (!p->count)                                                           \
            gasneti_trace_printf(" %-25s %6i", #name":", 0);                       \
          else                                                                     \
            gasneti_trace_printf(" %-25s %6i  avg/min/max/total %s (us) = %i/%i/%i/%i", \
                  #name":", (int)p->count,                                         \
                  pdesc,                                                           \
                  (int)GASNETI_STATTIME_TO_US(CALC_AVG(p->sumval, p->count)),      \
                  (int)GASNETI_STATTIME_TO_US(p->minval),                          \
                  (int)GASNETI_STATTIME_TO_US(p->maxval),                          \
                  (int)GASNETI_STATTIME_TO_US(p->sumval));                         \
          ACCUM((&AGGRNAME(timeval,type)), p);                                     \
        }

      GASNETI_ALL_STATS(DUMP_CTR, DUMP_INTVAL, DUMP_TIMEVAL);

      gasneti_trace_printf("");
      gasneti_trace_printf("");

      if (GASNETI_TRACE_ENABLED(G)) {
        gasneti_stat_intval_t *p = &AGGRNAME(intval,G);
        if (!p->count)
          gasneti_trace_printf("%-25s  %6i","Total gets:",0);
        else
          gasneti_trace_printf("%-25s  %6i  avg/min/max/total sz = %i/%i/%i/%i", "Total gets:",
            (int)p->count,
            (int)CALC_AVG(p->sumval, p->count),
            (int)p->minval,
            (int)p->maxval,
            (int)p->sumval);
      }
      if (GASNETI_TRACE_ENABLED(P)) {
        gasneti_stat_intval_t *p = &AGGRNAME(intval,P);
        if (!p->count)
          gasneti_trace_printf("%-25s  %6i","Total puts:",0);
        else
          gasneti_trace_printf("%-25s  %6i  avg/min/max/total sz = %i/%i/%i/%i", "Total puts:",
            (int)p->count,
            (int)CALC_AVG(p->sumval, p->count),
            (int)p->minval,
            (int)p->maxval,
            (int)p->sumval);
      }
      if (GASNETI_TRACE_ENABLED(S)) {
        gasneti_stat_intval_t *try_succ = &AGGRNAME(intval,S);
        gasneti_stat_timeval_t *wait_time = &AGGRNAME(timeval,S);
        if (!try_succ->count)
          gasneti_trace_printf("%-25s  %6i","Total try sync. calls:",0);
        else
          gasneti_trace_printf("%-25s  %6i  try success rate = %f%%  \n"
            "Total try sync. calls:",  ((int)try_succ->count),
            CALC_AVG((double)try_succ->sumval, try_succ->count) * 100.0);
        if (!wait_time->count)
          gasneti_trace_printf("%-25s  %6i","Total wait sync. calls:",0);
        else
          gasneti_trace_printf("%-25s  %6i  avg/min/max/total waittime (us) = %i/%i/%i/%i", 
            "Total wait sync. calls:", ((int)wait_time->count),
            (int)GASNETI_STATTIME_TO_US(CALC_AVG(wait_time->sumval, wait_time->count)),
            (int)GASNETI_STATTIME_TO_US(wait_time->minval),
            (int)GASNETI_STATTIME_TO_US(wait_time->maxval),
            (int)GASNETI_STATTIME_TO_US(wait_time->sumval));
      }
      if (GASNETI_TRACE_ENABLED(A)) 
        gasneti_trace_printf("%-25s  %6i", "Total AM's:", (int)AGGRNAME(ctr,A));

      gasneti_trace_printf("--------------------------------------------------------------------------------");
    }
    #endif

    GASNETC_TRACE_FINISH(); /* allow for final output of conduit-specific statistics */

    gasneti_mutex_lock(&gasneti_tracelock);
    if (gasneti_tracefile != stdout && gasneti_tracefile != stderr) 
      fclose(gasneti_tracefile);
    gasneti_tracefile = NULL;
    gasneti_mutex_unlock(&gasneti_tracelock);
    gasneti_mutex_unlock(&gasneti_tracefinishlock);
  }
#endif
}


/* ------------------------------------------------------------------------------------ */
#ifdef STATS
  #define DEF_CTR(type,name,desc)                   \
    gasneti_statctr_t gasneti_stat_ctr_##name = 0;
  #define DEF_INTVAL(type,name,desc)                   \
    gasneti_stat_intval_t gasneti_stat_intval_##name = \
      { 0, GASNETI_STATCTR_MAX, GASNETI_STATCTR_MIN, 0 };
  #define DEF_TIMEVAL(type,name,desc)                    \
    gasneti_stat_timeval_t gasneti_stat_timeval_##name = \
      { 0, GASNETI_STATTIME_MAX, GASNETI_STATTIME_MIN, 0 };
  GASNETI_ALL_STATS(DEF_CTR, DEF_INTVAL, DEF_TIMEVAL)

static gasneti_mutex_t gasneti_statlock = GASNETI_MUTEX_INITIALIZER;
#define STAT_LOCK() gasneti_mutex_lock(&gasneti_statlock);
#define STAT_UNLOCK() gasneti_mutex_unlock(&gasneti_statlock);

extern void gasneti_stat_count_accumulate(gasneti_statctr_t *pctr) {
  STAT_LOCK();
    (*pctr)++;
  STAT_UNLOCK();
}
extern void gasneti_stat_intval_accumulate(gasneti_stat_intval_t *pintval, gasneti_statctr_t val) {
  STAT_LOCK();
    pintval->count++;
    pintval->sumval += val;
    if_pf (val > pintval->maxval) pintval->maxval = val;
    if_pf (val < pintval->minval) pintval->minval = val;
  STAT_UNLOCK();
}
extern void gasneti_stat_timeval_accumulate(gasneti_stat_timeval_t *pintval, gasneti_stattime_t val) {
  STAT_LOCK();
    pintval->count++;
    pintval->sumval += val;
    if_pf (val > pintval->maxval) pintval->maxval = val;
    if_pf (val < pintval->minval) pintval->minval = val;
  STAT_UNLOCK();
}
#endif
/* ------------------------------------------------------------------------------------ */
#define GASNETI_GASNET_INTERNAL_C
#include "gasnet_mmap.c"
