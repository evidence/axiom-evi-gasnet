/*  $Archive:: /Ti/GASNet/gasnet_internal.c                               $
 *     $Date: 2002/06/26 23:30:06 $
 * $Revision: 1.4 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

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
  #else
    size_t pagesz = getpagesize();
    assert(pagesz > 0);
    return pagesz;
  #endif
}
/* ------------------------------------------------------------------------------------ */
/* GASNet Tracing and Statistics */

#ifdef TRACE
  GASNETI_IDENT(gasneti_IdentString_trace, "$GASNetTracingEnabled: 1 $");
#endif
#ifdef STATS
  GASNETI_IDENT(gasneti_IdentString_stats, "$GASNetStatisticsEnabled: 1 $");
#endif

char gasneti_tracetypes[256];
static FILE *tracefile = NULL;
gasneti_stattime_t starttime;

#if defined(STATS) || defined(TRACE)
  #define BUILD_STATS(type,name,desc) { #type, #name, #desc },
  gasneti_statinfo_t gasneti_stats[] = {
    GASNETI_ALL_STATS(BUILD_STATS, BUILD_STATS, BUILD_STATS)
    {'\0', NULL, NULL}
  };

  #define BUFSZ     1024
  #define NUMBUFS   32
  static char gasneti_printbufs[NUMBUFS][BUFSZ];
  static int gasneti_curbuf = 0;
  #ifdef GASNETI_THREADS
    static pthread_mutex_t gasneti_buflock = PTHREAD_MUTEX_INITIALIZER;
  #endif

  static char *gasneti_getbuf() {
    int bufidx;

    #ifdef GASNETI_THREADS
      pthread_mutex_lock(&gasneti_buflock);
    #endif

    bufidx = gasneti_curbuf;
    gasneti_curbuf = (gasneti_curbuf + 1) % NUMBUFS;

    #ifdef GASNETI_THREADS
      pthread_mutex_unlock(&gasneti_buflock);
    #endif
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
  extern void gasneti_trace_output(char *type, char *msg) {
    double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
    assert(tracefile);
    #ifdef GASNETI_THREADS
      fprintf(tracefile, "%i(%x) %8.6fs> (%c) %s%s", 
        gasnet_mynode(), (int)pthread_self(), time, *type, msg,
        (msg[strlen(msg)-1]=='\n'?"":"\n"));
    #else
      fprintf(tracefile, "%i %8.6fs> (%c) %s%s", gasnet_mynode(), time, *type, msg,
              (msg[strlen(msg)-1]=='\n'?"":"\n"));
    #endif
    fflush(tracefile);
  }
  /* dump message to tracefile with simple header */
  static void gasneti_trace_printf(char *format, ...) {
    va_list argptr;
    assert(tracefile);
    fprintf(tracefile, "%i> ", gasnet_mynode());
    va_start(argptr, format); /*  pass in last argument */
      vfprintf(tracefile, format, argptr);
    va_end(argptr);
    if (format[strlen(format)-1]!='\n') fprintf(tracefile, "\n");
    fflush(tracefile);
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
    if (!tracefilename) tracefilename = "stderr";

    if (!strcmp(tracefilename, "stderr")) tracefile = stderr;
    else if (!strcmp(tracefilename, "stdout")) tracefile = stdout;
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
      tracefile = fopen(tracefilename, "wt");
      if (!tracefile) {
        fprintf(stderr, "ERROR: Failed to open '%s' for tracing output. Redirecting to stderr.\n",
                tracefilename);
        tracefilename = "stderr";
        tracefile = stderr;
      }
    }

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
      time(&ltime); 
      gasneti_trace_printf("Program starting at: %s", ctime(&ltime));
      gasneti_trace_printf("GASNET_TRACEMASK: %s", tracetypes);
    }

    #ifdef NDEBUG
    { char *NDEBUG_warning =
       "WARNING: tracing/statistical collection may adversely affect application performance.";
      gasneti_trace_printf(NDEBUG_warning);
      if (tracefile != stdout && tracefile != stderr) {
        fprintf(stderr,NDEBUG_warning);
        fprintf(stderr,"\n");
      }
    #endif

    #ifdef GASNETI_USING_GETTIMEOFDAY
      gasneti_trace_printf("WARNING: using gettimeofday() for timing measurement - "
        "all short-term time measurements will be very rough and include significant timer overheads");
    #endif
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
    { 0, GASNETI_STATTIME_MAX, GASNETI_STATTIME_MIN, 0 };
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
  if (tracefile) {

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
            (int)CALC_AVG(wait_time->sumval, wait_time->count),
            (int)wait_time->minval,
            (int)wait_time->maxval,
            (int)wait_time->sumval);
      }
      if (GASNETI_TRACE_ENABLED(A)) 
        gasneti_trace_printf("%-25s  %6i", "Total AM's:", (int)AGGRNAME(ctr,A));

      gasneti_trace_printf("--------------------------------------------------------------------------------");
    }
    #endif


    if (tracefile != stdout && tracefile != stderr) fclose(tracefile);
    tracefile = NULL;
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

  #ifdef GASNETI_THREADS
    static pthread_mutex_t gasneti_statlock = PTHREAD_MUTEX_INITIALIZER;
    #define STAT_LOCK() pthread_mutex_lock(&gasneti_statlock);
    #define STAT_UNLOCK() pthread_mutex_unlock(&gasneti_statlock);
  #else
    #define STAT_LOCK()
    #define STAT_UNLOCK() 
  #endif
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
