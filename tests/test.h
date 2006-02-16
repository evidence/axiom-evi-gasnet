/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/test.h,v $
 *     $Date: 2006/02/16 02:01:14 $
 * $Revision: 1.78 $
 * Description: helpers for GASNet tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */


#ifndef _TEST_H
#define _TEST_H

#ifdef TEST_GASNET_TOOLS_ONLY
  /* do not use gasnet.h */
  #include <gasnet_tools.h>
#else
  #include <gasnet.h>
  #include <gasnet_tools.h>
  #define TEST_GASNET_H
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#ifdef IRIX
#define signal(a,b) bsd_signal(a,b)
#endif

#if defined(HAVE_PTHREAD_H) && !defined(GASNET_SEQ)
  #include <pthread.h>
#endif

#if !defined(DEBUG) && !defined(NDEBUG)
  #ifdef GASNET_DEBUG
    #define DEBUG 1
  #else
    #define NDEBUG 1
  #endif
#endif

/* bug 1206: several systems (notably Compaq C++ and OSX gcc) have an assert.h header 
   which is broken in one or more subtle ways. Don't trust the system assert.h. */
#include <assert.h>
#undef assert

#define assert_always(expr) \
    ((expr) ? (void)0 : (void)FATALERR("Assertion failure: %s", #expr))

#ifdef NDEBUG
  #define assert(x) ((void)0)
#else
  #define assert(x) assert_always(x)
#endif

/* ------------------------------------------------------------------------------------ */
/* generic message output utility
   test_makeMsg(baseformatargs, msgpred, isfatal, msgeval): 
     baseformatargs - parenthesized printf-style argument defining the generic stem
     msgpred - predicate which must evaluate to true to perform message output (eg 1 for always)
     isfatal - non-zero to request an abort after the message output
     msgeval - expression which is evaluated in a critical section, 
               immediately before each message output (or 0 for none)
 */
#define test_makeMsg(baseformatargs, msgpred, isfatal, msgeval)     \
  ( _test_makeErrMsg baseformatargs ,                               \
    ( (msgpred) ? (void)(msgeval) : (void)(_test_squashmsg = 1) ) , \
    (void)(_test_fatalmsg = (isfatal)),                             \
    _test_doErrMsg )

/* define several useful messaging macros */
static int test_errs = 0;
#ifdef TEST_GASNET_H
  #define MSG   test_makeMsg(("node %i/%i %s\n", (int)gasnet_mynode(), (int)gasnet_nodes(), "%s"), 1, 0, \
                             GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__))
  #define MSG0  test_makeMsg(("%s\n","%s"), (gasnet_mynode() == 0), 0, \
                             GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__))
  #define TERR(isfatal)                                                                                        \
                test_makeMsg(("ERROR: node %i/%i %s (at %s:%i)\n",                                             \
                              (int)gasnet_mynode(), (int)gasnet_nodes(), "%s",__FILE__, __LINE__), 1, isfatal, \
                             (test_errs++, GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__)))
  #define THREAD_MSG0(id)  test_makeMsg(("%s\n","%s"), (gasnet_mynode() == 0 && id == 0), 0, 0)
  #define THREAD_ERR(id)   test_makeMsg(("ERROR: node %i/%i thread %i: %s (at %s:%i)\n", \
                              (int)gasnet_mynode(), (int)gasnet_nodes(),id, "%s", __FILE__, __LINE__), 1, 0, test_errs++)
#else
  #define MSG   test_makeMsg(("%s\n","%s"), 1, 0, 0)
  #define MSG0  MSG
  #define TERR(isfatal) test_makeMsg(("ERROR: %s (at %s:%i)\n","%s",__FILE__, __LINE__), 1, isfatal, test_errs++)
  #define THREAD_MSG0(id)  test_makeMsg(("%s\n","%s"), (id == 0), 0, 0)
  #define THREAD_ERR(id)   test_makeMsg(("ERROR: thread %i: %s (at %s:%i)\n", \
                              id, "%s", __FILE__, __LINE__), 1, 0, test_errs++)
#endif
#define ERR      TERR(0)
#define FATALERR TERR(1)

#if defined(_AIX) && defined(__cplusplus)
  /* AIX's stdio.h won't provide prototypes for snprintf() and vsnprintf()
   * by default since they are in C99 but not C89.
   */
  extern int snprintf(char * s, size_t n, const char * format, ...)
					__attribute__((__format__ (__printf__, 3, 4)));
  extern int vsnprintf(char * s, size_t n, const char * format, va_list ap)
					__attribute__((__format__ (__printf__, 3, 0)));
#endif

#define _TEST_MSG_BUFSZ 1024
static char _test_baseformat[_TEST_MSG_BUFSZ];
static volatile int _test_squashmsg = 0;
static volatile int _test_fatalmsg = 0;
#if defined(HAVE_PTHREAD_H) && !defined(GASNET_SEQ)
  static pthread_mutex_t _test_msg_lock = PTHREAD_MUTEX_INITIALIZER;
  #define _test_LOCKMSG()   pthread_mutex_lock(&_test_msg_lock)
  #define _test_UNLOCKMSG() pthread_mutex_unlock(&_test_msg_lock)
#else
  #define _test_LOCKMSG()   ((void)0)
  #define _test_UNLOCKMSG() ((void)0)
#endif
static void _test_doErrMsg(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
static void _test_doErrMsg(const char *format, ...) {
  if (_test_squashmsg) _test_squashmsg = 0; 
  else {
    char output[_TEST_MSG_BUFSZ];
    va_list argptr;
    va_start(argptr, format); /*  pass in last argument */
      { int sz = vsnprintf(output, _TEST_MSG_BUFSZ, format, argptr);
        if (sz >= (_TEST_MSG_BUFSZ-5) || sz < 0) strcpy(output+(_TEST_MSG_BUFSZ-5),"...");
      }
    va_end(argptr);
    printf(_test_baseformat, output); 
    GASNETT_TRACE_PRINTF(_test_baseformat, output);
    fflush(stdout);
  }
  if (_test_fatalmsg) {
    fflush(NULL);
    sleep(1);
    abort();
  }
  _test_UNLOCKMSG();
}
static void _test_makeErrMsg(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
static void _test_makeErrMsg(const char *format, ...) {
  va_list argptr;
  _test_LOCKMSG();
  va_start(argptr, format); /*  pass in last argument */
    { int sz = vsnprintf(_test_baseformat, _TEST_MSG_BUFSZ, format, argptr);
      if (sz >= (_TEST_MSG_BUFSZ-5) || sz < 0) strcpy(_test_baseformat+(_TEST_MSG_BUFSZ-5),"...");
    }
  va_end(argptr);
}
/* ------------------------------------------------------------------------------------ */
/* misc tools */

#ifndef MIN
  #define MIN(x,y) ((x)<(y)?(x):(y))
#endif

#ifndef MAX
  #define MAX(x,y) ((x)>(y)?(x):(y))
#endif

static uint64_t test_checksum(void *p, int numbytes) {
 uint8_t *buf = (uint8_t *)p;
 uint64_t result = 0;
 int i;
 for (i=0;i<numbytes;i++) {
   result = ((result << 8) | ((result >> 56) & 0xFF) ) ^ *buf;
   buf++;
 }
 return result;
}

#define PAGESZ GASNETT_PAGESIZE
#define alignup(a,b) ((((a)+(b)-1)/(b))*(b))
#define alignup_ptr(a,b) ((void *)(((((uintptr_t)(a))+(b)-1)/(b))*(b)))

static int _test_rand(int low, int high) {
  int result;
  assert(low <= high);
  result = low+(int)(((double)(high-low+1))*rand()/(RAND_MAX+1.0));
  assert(result >= low && result <= high);
  return result;
}
#define TEST_RAND(low,high) _test_rand((low), (high))
#define TEST_RAND_PICK(a,b) (TEST_RAND(0,1)==1?(a):(b))
#define TEST_SRAND(seed)    srand(seed)
#define TEST_RAND_ONEIN(p)  (TEST_RAND(1,p) == 1)

#define TEST_HIWORD(arg)     ((uint32_t)(((uint64_t)(arg)) >> 32))
#define TEST_LOWORD(arg)     ((uint32_t)((uint64_t)(arg)))

#define check_zeroret(op) do {                                       \
  int _retval = (op);                                                \
  if_pf(_retval) FATALERR(#op": %s(%i)",strerror(_retval), _retval); \
} while (0)

#define check_nzeroret(op) do {                                       \
  int _retval = (op);                                                 \
  if_pf(!_retval) FATALERR(#op": %s(%i)",strerror(_retval), _retval); \
} while (0)

/* ------------------------------------------------------------------------------------ */
/* timing - TIME() returns a microsecond time-stamp */

#ifdef FORCE_GETTIMEOFDAY
  static int64_t mygetMicrosecondTimeStamp(void) {
      int64_t retval;
      struct timeval tv;
      check_zeroret(gettimeofday(&tv, NULL));
      retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
      return retval;
  }
  #define TIME() mygetMicrosecondTimeStamp()
#else
  #define TIME() gasnett_ticks_to_us(gasnett_ticks_now()) 
#endif

/* ------------------------------------------------------------------------------------ */
/* memory management */

#ifdef TEST_GASNET_H
  #define test_hold_interrupts()    gasnet_hold_interrupts()
  #define test_resume_interrupts()  gasnet_resume_interrupts()
#else
  #define test_hold_interrupts()    ((void)0)
  #define test_resume_interrupts()  ((void)0)
#endif

static void *_test_malloc(size_t sz, const char *curloc) {
  void *ptr;
  test_hold_interrupts();
  ptr = malloc(sz);
  test_resume_interrupts();
  if (ptr == NULL) {
    fprintf(stderr,"*** ERROR: Failed to malloc(%i) bytes at %s\n",(int)sz,curloc);
    abort();
  }
  return ptr;
}
static void *_test_calloc(size_t sz, const char *curloc) {
  void *retval = _test_malloc(sz, curloc);
  if (retval) memset(retval, 0, sz);
  return retval;
}
#define test_malloc(sz) _test_malloc((sz), __FILE__ ":" _STRINGIFY(__LINE__))
#define test_calloc(N,S) _test_calloc((N*S), __FILE__ ":" _STRINGIFY(__LINE__))

static void test_free(void *ptr) {
  test_hold_interrupts();
  free(ptr);
  test_resume_interrupts();
}

/* ------------------------------------------------------------------------------------ */
/* progress bar */

#ifndef TEST_PROGRESS_STEPS
#define TEST_PROGRESS_STEPS 10
#endif
/* node 0 caller should call with curriter=0..(numiters-1) */
#define TEST_PROGRESS_BAR(curriter, numiters) do {    \
    static int _breakwidth, _breakpt;                 \
    if_pf(curriter == 0) {                            \
      _breakwidth = (numiters/TEST_PROGRESS_STEPS);   \
      _breakpt = _breakwidth;                         \
    }                                                 \
    if (((curriter)+1) == _breakpt) {                 \
      MSG0("%i%%",(int)(100.0*((curriter)+1)/iters)); \
      _breakpt += _breakwidth;                        \
    }                                                 \
  } while (0)

/* ------------------------------------------------------------------------------------ */
/* Functions for obtaining calibrated delays */

#ifdef TEST_DELAY
/* NOTE: If you #define TEST_DELAY, be certain Makefile.in shows you depend on delay.o */

extern void test_delay(int64_t n, int pollcnt);	 /* in delay.o */

/* smallest number of delay loops to try in calibration */
#ifndef TEST_DELAY_LOOP_MIN
  #define TEST_DELAY_LOOP_MIN        100
#endif
/* max number of calibration iterations to wait for convergance */
#ifndef TEST_DELAY_CALIBRATION_LIMIT
  #define TEST_DELAY_CALIBRATION_LIMIT 100
#endif

/* Compute the number of loops needed to get no less that the specified delay
 * when executing "test_delay(loops, pollcnt)" exactly 'iters' times.
 *
 * Returns the number of loops needed and overwrites the argument with the
 * actual achieved delay for 'iters' calls to "delay(*time_p)".
 * The 'time_p' is given in microseconds.
 */
static int64_t test_calibrate_delay(int iters, int pollcnt, int64_t *time_p) 
{
	int64_t begin, end, time;
	float target = *time_p;
	float ratio = 0.0;
	int i;
        int64_t loops = 0;
        int caliters = 0;

	do {
		if (loops == 0) {
			loops = TEST_DELAY_LOOP_MIN;	/* first pass */
		} else {
			int64_t tmp = loops * ratio;

			if (tmp > loops) {
				loops = tmp;
			} else {
				loops += 1;	/* ensure progress in the face of round-off */
			}
                        assert(loops < 1ll<<62);
		}

		begin = TIME();
		for (i = 0; i < iters; i++) { test_delay(loops, pollcnt); }
		end = TIME();
		time = end - begin;
                assert(time >= 0);
                if (time == 0) ratio = 2.0;/* handle systems with very high granularity clocks */
                else ratio = target / (float)time;
                caliters++;
                if (caliters > TEST_DELAY_CALIBRATION_LIMIT) {
                  fprintf(stderr,"ERROR: test_calibrate_delay(%i,%i,%i) failed to converge after %i iterations.\n",
                          iters, pollcnt, (int)*time_p, iters);
                  abort();
                }
              #if 0
                printf("loops=%llu\n",(unsigned long long)loops); fflush(stdout);
                printf("ratio=%f target=%f time=%llu\n",ratio,target,(unsigned long long)time); fflush(stdout);
              #endif
	} while (ratio > 1.0);

	*time_p = time;
	return loops;
}
#endif

/* ------------------------------------------------------------------------------------ */
/* config strings */

#ifndef TEST_OMIT_CONFIGSTRINGS
#ifdef TEST_GASNET_H
  #define TEST_CONFIG_STRING GASNET_CONFIG_STRING
  #define TEST_TITANIUM_BACKEND "gasnet-" GASNET_CORE_NAME_STR "-uni"
#else
  #define TEST_CONFIG_STRING \
    "RELEASE=x,SPEC=x,CONDUIT=SMP-x/REFERENCE-x,THREADMODEL=PAR,SEGMENT=FAST,PTR=x,align,nodebug,notrace,nostats"
  #define TEST_TITANIUM_BACKEND "sequential"
#endif
/* mimic Berkeley UPC build config strings, to allow running GASNet tests using upcrun */
GASNETT_IDENT(GASNetT_IdentString_link_GASNetConfig, 
 "$GASNetConfig: (<link>) " TEST_CONFIG_STRING " $");
GASNETT_IDENT(GASNetT_IdentString_link_UPCRConfig,
 "$UPCRConfig: (<link>) " TEST_CONFIG_STRING ",SHMEM=pthreads,dynamicthreads $");
GASNETT_IDENT(GASNetT_IdentString_link_upcver, 
 "$UPCVersion: (<link>) *** GASNet test *** $");
GASNETT_IDENT(GASNetT_IdentString_link_compileline, 
 "$UPCCompileLine: (<link>) *** GASNet test *** $");
GASNETT_IDENT(GASNetT_IdentString_link_compiletime, 
 "$UPCCompileTime: (<link>) " __DATE__ " " __TIME__ " $");
GASNETT_IDENT(GASNetT_IdentString_HeapSz, 
 "$UPCRDefaultHeapSizes: UPC_SHARED_HEAP_OFFSET=0 UPC_SHARED_HEAP_SIZE=0 $");
GASNETT_IDENT(GASNetT_IdentString_PthCnt, "$UPCRDefaultPthreadCount: 1 $");
#ifdef GASNETI_PTR32
  GASNETT_IDENT(GASNetT_IdentString_PtrSz, "$UPCRSizeof: void_ptr=( $");
#else
  GASNETT_IDENT(GASNetT_IdentString_PtrSz, "$UPCRSizeof: void_ptr=, $");
#endif
GASNETT_IDENT(GASNetT_IdentString_ABI, "$UPCRBinaryInterface: " GASNETI_SYSTEM_TUPLE " $");
/* Ditto for Titanium tcrun */
GASNETT_IDENT(GASNetT_TiBackend_IdentString, 
 "$TitaniumBackend: " TEST_TITANIUM_BACKEND " $");
GASNETT_IDENT(GASNetT_TiCompiler_IdentString, 
 "$TitaniumCompilerFlags: *** GASNet test *** -g $");
#endif

#if defined(HAVE_PTHREAD_H) && !defined(GASNET_SEQ)
/* create numthreads pthreads to call start_routine. 
   if threadarg_arr is NULL, then a unique 0-based integer threadid is passed as arg to start_routine
   else threadarg_arr is an array of numthreads opaque datastructures of size threadarg_elemsz bytes each,
     and each thread recieves a pointer to a unique element of this array as the arg to start_routine
   then join the threads and add any non-zero results to test_errs
 */
static void test_createandjoin_pthreads(int numthreads, void *(*start_routine)(void *), 
                                      void *threadarg_arr, size_t threadarg_elemsz) {
    int i;
    uint8_t *threadarg_pos = (uint8_t *)threadarg_arr;
    pthread_t *threadid = (pthread_t *)test_malloc(sizeof(pthread_t)*numthreads);
    #ifdef HAVE_PTHREAD_SETCONCURRENCY
      pthread_setconcurrency(numthreads);
    #endif

    for(i=0;i<numthreads;i++) {
      void *threadarg;
      pthread_attr_t attr;   
      pthread_attr_init(&attr);   
      pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); /* ignore failures */
      if (threadarg_arr == NULL) threadarg = (void *)(uintptr_t)i;
      else { threadarg = threadarg_pos; threadarg_pos += threadarg_elemsz; }
      check_zeroret(pthread_create(&threadid[i], &attr, start_routine, threadarg));
      check_zeroret(pthread_attr_destroy(&attr));
    }

    for(i=0;i<numthreads;i++) {
      void *retval = NULL;
      check_zeroret(pthread_join(threadid[i], &retval));
      test_errs += (intptr_t)retval;
    }
    test_free(threadid);
  }
#endif

/* ------------------------------------------------------------------------------------ *
 * ------------------------------------------------------------------------------------ *
 *                        begin gasnet.h specific stuff                                 *
 * ------------------------------------------------------------------------------------ *
 * ------------------------------------------------------------------------------------ */
#ifdef TEST_GASNET_H

/* ------------------------------------------------------------------------------------ */
/* misc GASNet utilities */

#define GASNET_Safe(fncall) do {                                     \
    int _retval;                                                     \
    if ((_retval = fncall) != GASNET_OK) {                           \
      fprintf(stderr, "ERROR calling: %s\n"                          \
                   " at: %s:%i\n"                                    \
                   " error: %s (%s)\n",                              \
              #fncall, __FILE__, __LINE__,                           \
              gasnet_ErrorName(_retval), gasnet_ErrorDesc(_retval)); \
      fflush(stderr);                                                \
      gasnet_exit(_retval);                                          \
    }                                                                \
  } while(0)

/* ------------------------------------------------------------------------------------ */
/* barriers */
#define BARRIER() do {                                              \
  gasnet_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS);            \
  GASNET_Safe(gasnet_barrier_wait(0,GASNET_BARRIERFLAG_ANONYMOUS)); \
} while (0)

#if defined(GASNET_PAR) || defined(GASNET_PARSYNC)
  /* Cheap (but functional!) pthread + gasnet barrier */
  #if defined(__crayx1) || defined(__CYGWIN__)
    /* pthread_cond is unreliable on some versions of these OS's - use semaphores */
    #include <semaphore.h>
    static void test_pthread_barrier(unsigned int local_pthread_count, int doGASNetbarrier) {
      static pthread_mutex_t barrier_mutex = PTHREAD_MUTEX_INITIALIZER;
      static volatile int phase = 0;
      static volatile unsigned int barrier_count = 0;
      static sem_t sem[2];
      check_zeroret(pthread_mutex_lock(&barrier_mutex));
      { int myphase = phase;
        static volatile int firsttime = 1;
        if (firsttime) {
          check_zeroret(sem_init(&sem[0], 0, 0));
          check_zeroret(sem_init(&sem[1], 0, 0));
          firsttime = 0;
        }
        barrier_count++;
        if (barrier_count < local_pthread_count) { 
          check_zeroret(pthread_mutex_unlock(&barrier_mutex));
          check_zeroret(sem_wait(&sem[myphase]));
        } else {
          int i;
          if (doGASNetbarrier) BARRIER();
          barrier_count = 0;
          phase = !phase;
          check_zeroret(pthread_mutex_unlock(&barrier_mutex));
          for (i=0; i < local_pthread_count-1; i++) {
            check_zeroret(sem_post(&sem[myphase]));
          }
        }
      }
    }
  #else
    static void test_pthread_barrier(unsigned int local_pthread_count, int doGASNetbarrier) {
      static pthread_cond_t barrier_cond[2] = /* must be phased on some OS's (HPUX) */
        { PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };
      static pthread_mutex_t barrier_mutex[2] = 
        { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };
      static volatile unsigned int barrier_count = 0;
      static int volatile phase = 0;
      check_zeroret(pthread_mutex_lock(&barrier_mutex[phase]));
      barrier_count++;
      if (barrier_count < local_pthread_count) {
        int myphase = phase;
        while (myphase == phase) {
          check_zeroret(pthread_cond_wait(&barrier_cond[phase], &barrier_mutex[phase]));
        }
      } else {  
        /* Now do the gasnet barrier */
        if (doGASNetbarrier) BARRIER();
        barrier_count = 0;
        phase = !phase;
        check_zeroret(pthread_cond_broadcast(&barrier_cond[!phase]));
      }       
      check_zeroret(pthread_mutex_unlock(&barrier_mutex[!phase]));
    }
  #endif
  #define PTHREAD_BARRIER(local_pthread_count)      \
    test_pthread_barrier(local_pthread_count, 1)
  #define PTHREAD_LOCALBARRIER(local_pthread_count) \
    test_pthread_barrier(local_pthread_count, 0)
#else
  #define PTHREAD_BARRIER(local_pthread_count) do { \
    PTHREAD_LOCALBARRIER(local_pthread_count);      \
    BARRIER();                                      \
  } while (0)
  #define PTHREAD_LOCALBARRIER(local_pthread_count) do {            \
    if (local_pthread_count != 1) {                                 \
      MSG("ERROR: cannot call PTHREAD_BARRIER in GASNET_SEQ mode"); \
      abort();                                                      \
    }                                                               \
  } while (0)
#endif

/* ------------------------------------------------------------------------------------ */
/* standard messages */
static void TEST_DEBUGPERFORMANCE_WARNING() {
  if (gasnet_mynode() == 0) {
    const char *debug = "";
    const char *trace = "";
    const char *stats = "";
    #ifdef GASNET_DEBUG
      debug = "debugging ";
    #endif
    #ifdef GASNET_TRACE
      trace = "tracing ";
    #endif
    #ifdef GASNET_STATS
      stats = "statistical collection ";
    #endif
    if (*debug || *trace || *stats) {
      fflush(NULL);
      fprintf(stdout,
        "-----------------------------------------------------------------------\n"
        " WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING\n"
        "\n"
        " GASNet was configured and built with these optional features enabled:\n"
        "        %s%s%senabled\n"
        " This usually has a SERIOUS impact on performance, so you should NOT\n"
        " trust any performance numbers reported in this run!!!\n"
        " You should configure and build from scratch without the configure\n"
        " flags that enable the optional additional checking/reporting.\n"
        "\n"
        " WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING\n"
        "-----------------------------------------------------------------------\n"
        ,debug,trace,stats);
      fflush(NULL);
    }
  }
}

#define TEST_PRINT_CONDUITINFO() do {                              \
  MSG0("%s conduit: v%s GASNET_ALIGNED_SEGMENTS=%i\n"              \
       " gasnet_AMMaxArgs():        %i\n"                          \
       " gasnet_AMMaxMedium():      %i\n"                          \
       " gasnet_AMMaxLongRequest(): %i\n"                          \
       " gasnet_AMMaxLongReply():   %i"                            \
    ,                                                              \
    _STRINGIFY(GASNET_CORE_NAME), _STRINGIFY(GASNET_CORE_VERSION), \
    GASNET_ALIGNED_SEGMENTS,                                       \
    (int)gasnet_AMMaxArgs(),                                       \
    (int)gasnet_AMMaxMedium(),                                     \
    (int)gasnet_AMMaxLongRequest(),                                \
    (int)gasnet_AMMaxLongReply());                                 \
  } while (0)

#if defined(GASNET_SEQ)
  #define TEST_PARSEQ SEQ
#elif defined(GASNET_PAR)
  #define TEST_PARSEQ PAR
#elif defined(GASNET_PARSYNC)
  #define TEST_PARSEQ PARSYNC
#endif

/* ------------------------------------------------------------------------------------ */
/* segment management */
#ifndef TEST_SEGSZ_PADFACTOR
  #define TEST_SEGSZ_PADFACTOR 1
#endif
#define TEST_SEGSZ_REQUEST (TEST_SEGSZ_PADFACTOR*TEST_SEGSZ)

#if defined(GASNET_PAR) || defined(GASNET_PARSYNC)
  #ifndef TEST_MAXTHREADS
    #define TEST_MAXTHREADS      256
  #endif
  #ifndef TEST_SEGZ_PER_THREAD
    #define TEST_SEGZ_PER_THREAD (64*1024)
  #endif
  #ifndef TEST_SEGSZ
    #ifdef TEST_SEGSZ_EXPR
      #define TEST_SEGSZ  alignup(TEST_SEGSZ_EXPR,PAGESZ)
    #else
      #define TEST_SEGSZ  alignup(TEST_MAXTHREADS*TEST_SEGZ_PER_THREAD,PAGESZ)
    #endif
  #endif
  #ifndef TEST_SEGSZ_EXPR
    #if TEST_SEGSZ < (TEST_MAXTHREADS*TEST_SEGZ_PER_THREAD)
      #error "TEST_SEGSZ < (TEST_MAXTHREADS*TEST_SEGZ_PER_THREAD)"
    #endif
  #endif
#else
  #ifndef TEST_SEGSZ
    #ifdef TEST_SEGSZ_EXPR
      #define TEST_SEGSZ  alignup(TEST_SEGSZ_EXPR,PAGESZ)
    #else
      #define TEST_SEGSZ  alignup(64*1024,PAGESZ)
    #endif
  #endif
#endif
#ifndef TEST_SEGSZ_EXPR
  #if (TEST_SEGSZ % PAGESZ) != 0 || TEST_SEGSZ <= 0
    #error Bad TEST_SEGSZ
  #endif
#endif

#define TEST_MINHEAPOFFSET  alignup(128*4096,PAGESZ)
#if (TEST_MINHEAPOFFSET % PAGESZ) != 0 
  #error Bad TEST_MINHEAPOFFSET
#endif

#ifdef GASNET_SEGMENT_EVERYTHING
  gasnet_seginfo_t *_test_seginfo;
  #define TEST_SEG(node) (assert(_test_seginfo), _test_seginfo[node].addr)
  /* following trivially handles the case where static data is aligned
     across the nodes, and also works on X-1 where the static data is
     misaligned across nodes. 
     The only assumption is that AM mediums, barriers and atomics work properly
     We intercept the gasnet_attach call and do the segment exchange there
   */
  static int _test_seggather_idx;
  static gasnett_atomic_t _test_seggather_done = gasnett_atomic_init(0);
  static void _test_seggather(gasnet_token_t token, void *buf, size_t nbytes) {
    gasnet_node_t srcid;
    assert(nbytes == sizeof(gasnet_seginfo_t));
    assert(_test_seginfo != NULL);
    gasnet_AMGetMsgSource(token, &srcid);
    assert(srcid < gasnet_nodes());
    _test_seginfo[srcid] = *(gasnet_seginfo_t *)buf;
    gasnett_local_wmb();
    gasnett_atomic_increment(&_test_seggather_done);
  }
  static int _test_segbcast_idx;
  static int _test_segbcast_done = 0;
  static void _test_segbcast(gasnet_token_t token, void *buf, size_t nbytes) {
    assert(nbytes == sizeof(gasnet_seginfo_t)*gasnet_nodes());
    memcpy(_test_seginfo, buf, nbytes);
    gasnett_local_wmb();
    _test_segbcast_done = 1;
  }
  static int _test_attach(gasnet_handlerentry_t *table, int numentries, uintptr_t segsize, uintptr_t minheapoffset) {
    #ifdef TEST_SEGSZ_EXPR
      /* dynamically allocate segment */
      uint8_t *_test_hidden_seg;
    #else
      /* use a block of static data as the segment */
      static uint8_t _test_hidden_seg[TEST_SEGSZ+PAGESZ];
    #endif
    int i, result;
    gasnet_seginfo_t myseg;

    /* must use malloc here, pre-attach */
    gasnet_handlerentry_t *mytab = (gasnet_handlerentry_t *)malloc((numentries+2)*sizeof(gasnet_handlerentry_t));
    if (numentries) memcpy(mytab, table, numentries*sizeof(gasnet_handlerentry_t));
    mytab[numentries].index = 0; /* "dont care" index */
    mytab[numentries].fnptr = (void (*)())_test_seggather;
    mytab[numentries+1].index = 0; /* "dont care" index */
    mytab[numentries+1].fnptr = (void (*)())_test_segbcast;
    /* do regular attach, then setup seg_everything segment */
    GASNET_Safe(result = gasnet_attach(mytab, numentries+2, segsize, minheapoffset));
    _test_seggather_idx = mytab[numentries].index;
    _test_segbcast_idx = mytab[numentries+1].index;
    if (numentries) memcpy(table, mytab, numentries*sizeof(gasnet_handlerentry_t));
    free(mytab);

    _test_seginfo = (gasnet_seginfo_t *)test_malloc(gasnet_nodes()*sizeof(gasnet_seginfo_t));
    #ifdef TEST_SEGSZ_EXPR
      _test_hidden_seg = (uint8_t *)test_malloc(TEST_SEGSZ+PAGESZ);
    #endif
    myseg.addr = ((void *)(((uint8_t*)_test_hidden_seg) + 
      (((((uintptr_t)_test_hidden_seg)%PAGESZ) == 0)? 0 : 
       (PAGESZ-(((uintptr_t)_test_hidden_seg)%PAGESZ)))));
    myseg.size = TEST_SEGSZ;
    BARRIER();
    GASNET_Safe(gasnet_AMRequestMedium0(0, _test_seggather_idx, &myseg, sizeof(gasnet_seginfo_t)));
    if (gasnet_mynode() == 0) {
      GASNET_BLOCKUNTIL((int)gasnett_atomic_read(&_test_seggather_done) == (int)gasnet_nodes());
      for (i=0; i < (int)gasnet_nodes(); i++) {
        GASNET_Safe(gasnet_AMRequestMedium0(i, _test_segbcast_idx, _test_seginfo, gasnet_nodes()*sizeof(gasnet_seginfo_t)));
      }
    }
    GASNET_BLOCKUNTIL(_test_segbcast_done);
    BARRIER();
    for (i=0; i < (int)gasnet_nodes(); i++) {
      assert_always(_test_seginfo[i].size >= TEST_SEGSZ);
      assert_always((((uintptr_t)_test_seginfo[i].addr) % PAGESZ) == 0);
    }
    return result;
  }
  #undef gasnet_attach
  #define gasnet_attach _test_attach
#else
  static void *_test_getseg(gasnet_node_t node) {
    static gasnet_seginfo_t *si = NULL;
    if (si == NULL) {
      gasnet_node_t i;
      gasnet_seginfo_t *s = (gasnet_seginfo_t *)test_malloc(gasnet_nodes()*sizeof(gasnet_seginfo_t));
      GASNET_Safe(gasnet_getSegmentInfo(s, gasnet_nodes()));
      for (i=0; i < gasnet_nodes(); i++) {
        assert_always(s[i].size >= TEST_SEGSZ);
        assert_always(((uintptr_t)s[i].size) % PAGESZ == 0);
        #if GASNET_ALIGNED_SEGMENTS == 1
          assert_always(s[i].addr == s[0].addr);
        #endif
      }
      si = s;
    }
    return si[node].addr;
  }
  #define TEST_SEG(node) (_test_getseg(node))
#endif

#define TEST_MYSEG()          (TEST_SEG(gasnet_mynode()))

#endif /* TEST_GASNET_H */
/* ------------------------------------------------------------------------------------ */
/* test initialization boilerplate */
#if defined(__alpha) || defined(_CRAYT3E)
  #define TEST_SIG_INIT() do {                          \
      if (signal(SIGFPE, SIG_IGN) == SIG_ERR)           \
        { perror("signal(SIGFPE, SIG_IGN)"); abort(); } \
  } while (0)
#else
  #define TEST_SIG_INIT()
#endif

static int test_getenv_yesno(const char *key, int deflt) {
  #ifdef TEST_GASNET_H
    const char *p = gasnet_getenv(key);
  #else
    const char *p = getenv(key);
  #endif
  if (!p) return deflt;
  if (!*p) return 1; /* empty == yes */
  if (*p == 'Y' || *p == 'y' || atoi(p)) return 1;
  return 0;
}

static void TEST_GENERICS_WARNING() {
  #ifdef TEST_GASNET_H
    if (gasnet_mynode() == 0)
  #endif
  {
    #ifdef GASNETT_USING_GETTIMEOFDAY
      fflush(NULL);
      fprintf(stdout, 
        "WARNING: using gettimeofday() for timing measurement - all short-term time measurements\n"             
        "WARNING: will be very rough and include significant timer overheads\n");
      fflush(NULL);
    #endif
    #ifdef GASNETT_USING_GENERIC_ATOMICOPS
      fflush(NULL);
      fprintf(stdout, 
        "WARNING: using generic mutex-based GASNet atomics, which are likely to have high overhead\n"             
        "WARNING: consider implementing true GASNet atomics, if supported by your platform/compiler\n");
      fflush(NULL);
    #endif
  }
}

static const char *_test_usagestr = NULL;
static const char *_test_testname = NULL;
static const char *_test_argvzero = NULL;
static void _test_usage(int early) {
  #ifdef TEST_GASNET_H
    if (gasnet_mynode() == 0) {
      fprintf(stderr, "%s %s\n", _test_testname, GASNET_CONFIG_STRING);
      fprintf(stderr, "Usage: %s %s%s", _test_argvzero, _test_usagestr,
        (_test_usagestr[strlen(_test_usagestr)-1] == '\n' ? "" : "\n"));
      fflush(NULL);
      sleep(1);
      gasnet_exit(1);
    } else { /* wait to die */
      if (early) while(1) gasnett_sched_yield();
      else BARRIER();
    }
  #else
      fprintf(stderr, "%s %s\n", _test_testname, GASNETT_CONFIG_STRING);
      fprintf(stderr, "Usage: %s %s%s", _test_argvzero, _test_usagestr,
        (_test_usagestr[strlen(_test_usagestr)-1] == '\n' ? "" : "\n"));
      fflush(NULL);
      sleep(1);
      exit(1);
  #endif
}
#define test_usage() _test_usage(0)
static void _test_init(const char *testname, int reports_performance, int early,
                       int argc, const char * const *argv, const char *usagestr) {
  /* convenient place to put inits we want in all tests */
  int i;
  _test_usagestr = strdup(usagestr);
  _test_testname = strdup(testname);
  _test_argvzero = strdup(argv[0]);
  for (i = 0; i < argc; i++) { /* check for standard help option */
    if (!strcmp(argv[i],"-h") || !strcmp(argv[i],"--h") || 
        !strcmp(argv[i],"-help") || !strcmp(argv[i],"--help")) _test_usage(early);
  }

  TEST_SIG_INIT();
  #ifdef TEST_GASNET_H
    if (!early) BARRIER();
    if (reports_performance) {
      TEST_DEBUGPERFORMANCE_WARNING();
      TEST_GENERICS_WARNING();
      if (gasnet_mynode() == 0)
        fprintf(stdout, "Timer granularity: <= %.3f us, overhead: ~ %.3f us\n",
                       gasnett_timer_granularityus(), gasnett_timer_overheadus());
      fflush(NULL);
    }
    if (test_getenv_yesno("GASNET_TEST_POLITE_SYNC",0)) {
      MSG0("WARNING: GASNET_TEST_POLITE_SYNC is set - enabling  \"polite\", low-performance synchronization algorithms");
      gasnet_set_waitmode(GASNET_WAIT_BLOCK);
    }
    MSG0("=====> %s nprocs=%d config=%s",
        testname, (int)gasnet_nodes(), GASNET_CONFIG_STRING);
    if (!early) {
      char hostname[255];
      TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */
      BARRIER();
      if (!gethostname(hostname,255)) {
        MSG("hostname is: %s", hostname);
        fflush(NULL);
        BARRIER();
      }
    }
  #else
    MSG0("=====> %s config=%s",
        testname, GASNETT_CONFIG_STRING);
  #endif
  if (test_getenv_yesno("GASNET_VERBOSEENV",0)) MSG("%s running...", testname);
}
#define test_init(testname, reports_performance, usagestr) \
       _test_init(testname, reports_performance, 0, argc, (const char * const *)argv, usagestr)
#define test_init_early(testname, reports_performance, usagestr) \
       _test_init(testname, reports_performance, 1, argc, (const char * const *)argv, usagestr)


#define TEST_TRACING_MACROS() do {                                                 \
  const char *file; unsigned int line;                                             \
  GASNETT_TRACE_GETSOURCELINE(&file, &line);                                       \
  GASNETT_TRACE_SETSOURCELINE(file, line);                                         \
  GASNETT_TRACE_FREEZESOURCELINE();                                                \
  GASNETT_TRACE_UNFREEZESOURCELINE();                                              \
  if (GASNETT_TRACE_ENABLED)                                                       \
    GASNETT_TRACE_PRINTF("TEST_TRACING_MACROS: GASNETT_TRACE_PRINTF()");           \
  GASNETT_TRACE_PRINTF_FORCE("TEST_TRACING_MACROS: GASNETT_TRACE_PRINTF_FORCE()"); \
  GASNETT_TRACE_SETMASK(GASNETT_TRACE_GETMASK());                                  \
  GASNETT_STATS_SETMASK(GASNETT_STATS_GETMASK());                                  \
  GASNETT_TRACE_SET_TRACELOCAL(GASNETT_TRACE_GET_TRACELOCAL());                    \
} while (0)

#endif
