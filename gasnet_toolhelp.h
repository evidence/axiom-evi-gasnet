/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_toolhelp.h,v $
 *     $Date: 2006/08/03 23:22:26 $
 * $Revision: 1.9 $
 * Description: misc declarations needed by both gasnet_tools and libgasnet
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_TOOLHELP_H
#define _GASNET_TOOLHELP_H

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#if GASNETI_THREADS || GASNETT_THREAD_SAFE
  #if PLATFORM_OS_LINUX
   struct timespec; /* avoid an annoying warning on Linux */
  #endif
  #include <pthread.h>
#endif

GASNETI_BEGIN_EXTERNC

#if PLATFORM_OS_MTA
   #include <machine/runtime.h>
   #define _gasneti_sched_yield() mta_yield()
#elif defined(HAVE_SCHED_YIELD) && !PLATFORM_OS_BLRTS && !PLATFORM_OS_CATAMOUNT
   #include <sched.h>
   #define _gasneti_sched_yield() sched_yield()
#else
   #include <unistd.h>
   #define _gasneti_sched_yield() (sleep(0),0)
#endif
#define gasneti_sched_yield() gasneti_assert_zeroret(_gasneti_sched_yield())

#if PLATFORM_COMPILER_GNU_CXX /* bug 1681 */
  #define GASNETI_CURRENT_FUNCTION __PRETTY_FUNCTION__
#elif defined(__GNUC__) || defined(__FUNCTION__)
  #define GASNETI_CURRENT_FUNCTION __FUNCTION__
#elif defined(HAVE_FUNC) && !defined(__cplusplus)
  /* __func__ should also work for ISO C99 compilers */
  #define GASNETI_CURRENT_FUNCTION __func__
#else
  #define GASNETI_CURRENT_FUNCTION ""
#endif
extern char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum);
#define gasneti_current_loc gasneti_build_loc_str(GASNETI_CURRENT_FUNCTION,__FILE__,__LINE__)

/* gasneti_assert_always():
 * an assertion that never compiles away - for sanity checks in non-critical paths 
 */
#define gasneti_assert_always(expr) \
    (PREDICT_TRUE(expr) ? (void)0 : gasneti_fatalerror("Assertion failure at %s: %s", gasneti_current_loc, #expr))

/* gasneti_assert():
 * an assertion that compiles away in non-debug mode - for sanity checks in critical paths 
 */
#if GASNET_NDEBUG
  #define gasneti_assert(expr) ((void)0)
#else
  #define gasneti_assert(expr) gasneti_assert_always(expr)
#endif

/* gasneti_assert_zeroret(), gasneti_assert_nzeroret():
 * evaluate an expression (always), and in debug mode additionally 
 * assert that it returns zero or non-zero
 * useful for making system calls and checking the result
 */
#if GASNET_DEBUG
  #define gasneti_assert_zeroret(op) do {                   \
    int _retval = (op);                                     \
    if_pf(_retval)                                          \
      gasneti_fatalerror(#op": %s(%i), errno=%s(%i) at %s", \
        strerror(_retval), _retval, strerror(errno), errno, \
        gasneti_current_loc);                               \
  } while (0)
  #define gasneti_assert_nzeroret(op) do {                  \
    int _retval = (op);                                     \
    if_pf(!_retval)                                         \
      gasneti_fatalerror(#op": %s(%i), errno=%s(%i) at %s", \
        strerror(_retval), _retval, strerror(errno), errno, \
        gasneti_current_loc);                               \
  } while (0)
#else
  #define gasneti_assert_zeroret(op)  op
  #define gasneti_assert_nzeroret(op) op
#endif

/* return physical memory of machine
   on failure, failureIsFatal nonzero => fatal error, failureIsFatal zero => return 0 */
extern uint64_t gasneti_getPhysMemSz(int failureIsFatal); 

GASNETI_FORMAT_PRINTF(gasneti_fatalerror,1,2,
extern void gasneti_fatalerror(const char *msg, ...) GASNETI_NORETURN);
GASNETI_NORETURNP(gasneti_fatalerror)

extern void gasneti_killmyprocess(int exitcode) GASNETI_NORETURN;
GASNETI_NORETURNP(gasneti_killmyprocess)

extern void gasneti_flush_streams(); /* flush all open streams */
extern void gasneti_close_streams(); /* close standard streams (for shutdown) */

extern int gasneti_cpu_count();

extern void gasneti_set_affinity(int rank);

extern int gasneti_isLittleEndian();

typedef void (*gasneti_sighandlerfn_t)(int);
gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp);

/* return a fast but simple/insecure 64-bit checksum of arbitrary data */
extern uint64_t gasneti_checksum(void *p, int numbytes);

/* ------------------------------------------------------------------------------------ */
/* Error checking system mutexes -
     wrapper around pthread mutexes that provides extra support for 
     error checking when GASNET_DEBUG is defined
   gasneti_mutex_lock(&lock)/gasneti_mutex_unlock(&lock) - 
     lock and unlock (checks for recursive locking errors)
   gasneti_mutex_trylock(&lock) - 
     non-blocking trylock - returns EBUSY on failure, 0 on success
   gasneti_mutex_assertlocked(&lock)/gasneti_mutex_assertunlocked(&lock) - 
     allow functions to assert a given lock is held / not held by the current thread
  
   -DGASNETI_USE_TRUE_MUTEXES=1 will force gasneti_mutex_t to always
    use true locking (even under GASNET_SEQ or GASNET_PARSYNC config)
*/
#if GASNET_PAR || GASNETI_CONDUIT_THREADS || GASNETT_THREAD_SAFE
  /* need to use true locking if we have concurrent calls from multiple client threads 
     or if conduit has private threads that can run handlers */
  #define GASNETI_USE_TRUE_MUTEXES 1 
#elif !defined(GASNETI_USE_TRUE_MUTEXES)
  #define GASNETI_USE_TRUE_MUTEXES 0
#endif

#if PLATFORM_OS_CYGWIN || defined(GASNETI_FORCE_MUTEX_INITCLEAR)
  /* we're sometimes unable to call pthread_mutex_destroy when freeing a mutex
     some pthread implementations will fail to re-init a mutex
     (eg after a free and realloc of the associated mem) unless
     the contents are first cleared to zero
   */
  #define GASNETI_MUTEX_INITCLEAR(pm) memset(pm,0,sizeof(*(pm)))
#else
  #define GASNETI_MUTEX_INITCLEAR(pm) ((void)0)
#endif

#if GASNET_DEBUG
  #define GASNETI_MUTEX_NOOWNER         ((uintptr_t)-1)
  #ifndef GASNETI_THREADIDQUERY
    /* allow conduit override of thread-id query */
    #if GASNETI_USE_TRUE_MUTEXES
      #define GASNETI_THREADIDQUERY()   ((uintptr_t)pthread_self())
    #else
      #define GASNETI_THREADIDQUERY()   ((uintptr_t)0)
    #endif
  #endif
  #if GASNETI_USE_TRUE_MUTEXES
    #include <pthread.h>
    typedef struct {
      pthread_mutex_t lock;
      volatile uintptr_t owner;
    } gasneti_mutex_t;
    #if defined(PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP)
      /* These are faster, though less "featureful" than the default
       * mutexes on linuxthreads implementations which offer them.
       */
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP, GASNETI_MUTEX_NOOWNER }
    #else
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, GASNETI_MUTEX_NOOWNER }
    #endif
    #define gasneti_mutex_lock(pl) do {                                        \
              gasneti_assert((pl)->owner != GASNETI_THREADIDQUERY());          \
              gasneti_assert_zeroret(pthread_mutex_lock(&((pl)->lock)));       \
              gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER);            \
              (pl)->owner = GASNETI_THREADIDQUERY();                           \
            } while (0)
    GASNETI_INLINE(gasneti_mutex_trylock)
    int gasneti_mutex_trylock(gasneti_mutex_t *pl) {
              int retval;
              gasneti_assert((pl)->owner != GASNETI_THREADIDQUERY());
              retval = pthread_mutex_trylock(&((pl)->lock));
              if (retval == EBUSY) return EBUSY;
              if (retval) gasneti_fatalerror("pthread_mutex_trylock()=%s",strerror(retval));
              gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER);
              (pl)->owner = GASNETI_THREADIDQUERY();
              return 0;
    }
    #define gasneti_mutex_unlock(pl) do {                                  \
              gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY());      \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;                         \
              gasneti_assert_zeroret(pthread_mutex_unlock(&((pl)->lock))); \
            } while (0)
    #define gasneti_mutex_init(pl) do {                                       \
              GASNETI_MUTEX_INITCLEAR(&((pl)->lock));                         \
              gasneti_assert_zeroret(pthread_mutex_init(&((pl)->lock),NULL)); \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;                            \
            } while (0)
    #define gasneti_mutex_destroy(pl) \
              gasneti_assert_zeroret(pthread_mutex_destroy(&((pl)->lock)))
  #else /* GASNET_DEBUG non-pthread (error-check-only) mutexes */
    typedef struct {
      volatile uintptr_t owner;
    } gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER   { GASNETI_MUTEX_NOOWNER }
    #define gasneti_mutex_lock(pl) do {                             \
              gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER); \
              (pl)->owner = GASNETI_THREADIDQUERY();                \
            } while (0)
    GASNETI_INLINE(gasneti_mutex_trylock)
    int gasneti_mutex_trylock(gasneti_mutex_t *pl) {
              gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER);
              (pl)->owner = GASNETI_THREADIDQUERY();
              return 0;
    }
    #define gasneti_mutex_unlock(pl) do {                             \
              gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY()); \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;                    \
            } while (0)
    #define gasneti_mutex_init(pl) do {                       \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;            \
            } while (0)
    #define gasneti_mutex_destroy(pl)
  #endif
  #define gasneti_mutex_assertlocked(pl)    gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY())
  #define gasneti_mutex_assertunlocked(pl)  gasneti_assert((pl)->owner != GASNETI_THREADIDQUERY())
#else /* non-debug mutexes */
  #if GASNETI_USE_TRUE_MUTEXES
    #include <pthread.h>
    typedef pthread_mutex_t           gasneti_mutex_t;
    #if defined(PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP)
      /* These are faster, though less "featureful" than the default
       * mutexes on linuxthreads implementations which offer them.
       */
      #define GASNETI_MUTEX_INITIALIZER PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
    #else
      #define GASNETI_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
    #endif
    #define gasneti_mutex_lock(pl)      pthread_mutex_lock(pl)
    #define gasneti_mutex_trylock(pl)   pthread_mutex_trylock(pl)
    #define gasneti_mutex_unlock(pl)    pthread_mutex_unlock(pl)
    #define gasneti_mutex_init(pl)      (GASNETI_MUTEX_INITCLEAR(pl),  \
                                         pthread_mutex_init((pl),NULL))
    #define gasneti_mutex_destroy(pl)   pthread_mutex_destroy(pl)
  #else
    typedef char           gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER '\0'
    #define gasneti_mutex_lock(pl)    ((void)0)
    #define gasneti_mutex_trylock(pl) 0
    #define gasneti_mutex_unlock(pl)  ((void)0)
    #define gasneti_mutex_init(pl)    ((void)0)
    #define gasneti_mutex_destroy(pl) ((void)0)
  #endif
  #define gasneti_mutex_assertlocked(pl)    ((void)0)
  #define gasneti_mutex_assertunlocked(pl)  ((void)0)
#endif

/* ------------------------------------------------------------------------------------ */
/* Wrappers for thread-local data storage
   In threaded configurations, uses the fastest-available target-specific mechanisms 
    for access to thread-local storage (eg __thread), or pthread_getspecific() for 
    generic platforms. Automatically handles the hassle of pthread key creation as required.
   In non-threaded configurations, expands to simple process-global storage. 

  Must be declared as:
    GASNETI_THREADKEY_DEFINE(mykey); - must be defined in exactly one C file at global scope
    GASNETI_THREADKEY_DECLARE(mykey); - optional, use in headers to reference externally-defined key
  and then can be used as:
    void *val = gasneti_threadkey_get(mykey);
    gasneti_threadkey_set(mykey,val);
  no initialization is required (happens automatically on first access).

  Initialization can optionally be performed using:
    gasneti_threadkey_init(mykey);
  which then allows subsequent calls to:
    void *val = gasneti_threadkey_get_noinit(mykey);
    gasneti_threadkey_set_noinit(mykey,val);
  these save a branch by avoiding the initialization check.
  gasneti_threadkey_init is permitted to be called multiple times and
  from multiple threads - calls after the first one will be ignored.
*/
#define _GASNETI_THREADKEY_MAGIC 0xFF00ABCDEF573921ULL

#if GASNETI_THREADS || GASNETT_THREAD_SAFE
  #if GASNETI_HAVE_TLS_SUPPORT /* use __thread, if available */ \
     && !PLATFORM_COMPILER_SUN /* causes sunC 5.7 for x86 to crash on libgasnet-par */
    #define _GASNETI_THREADKEY_USES_TLS 1
  #else
    #define _GASNETI_THREADKEY_USES_PTHREAD_GETSPECIFIC 1
  #endif
#else /* no threads */
    #define _GASNETI_THREADKEY_USES_NOOP 1
#endif

typedef struct { 
  #if GASNET_DEBUG
    uint64_t magic;
    #define _GASNETI_THREADKEY_MAGIC_INIT _GASNETI_THREADKEY_MAGIC,
  #else
    #define _GASNETI_THREADKEY_MAGIC_INIT
  #endif
  #if _GASNETI_THREADKEY_USES_PTHREAD_GETSPECIFIC
    gasneti_mutex_t initmutex;
    volatile int isinit;
    pthread_key_t value;
    #define _GASNETI_THREADKEY_REST_INIT GASNETI_MUTEX_INITIALIZER, 0 /* value field left NULL */
  #else
    void *value;
    #define _GASNETI_THREADKEY_REST_INIT 0
  #endif
} _gasneti_threadkey_t;

#define _GASNETI_THREADKEY_INITIALIZER \
  { _GASNETI_THREADKEY_MAGIC_INIT _GASNETI_THREADKEY_REST_INIT }

#if _GASNETI_THREADKEY_USES_TLS
  #define _GASNETI_THREADKEY_STORAGE __thread
#else
  #define _GASNETI_THREADKEY_STORAGE 
#endif
#define GASNETI_THREADKEY_DECLARE(keyname) \
  extern _GASNETI_THREADKEY_STORAGE _gasneti_threadkey_t keyname
#define GASNETI_THREADKEY_DEFINE(keyname) \
  _GASNETI_THREADKEY_STORAGE _gasneti_threadkey_t keyname = _GASNETI_THREADKEY_INITIALIZER

#if _GASNETI_THREADKEY_USES_PTHREAD_GETSPECIFIC
  #define _gasneti_threadkey_check(key, requireinit)         \
   ( gasneti_assert((key).magic == _GASNETI_THREADKEY_MAGIC), \
     (requireinit ? gasneti_assert((key).isinit) : ((void)0)))
  #define gasneti_threadkey_get_noinit(key) \
    ( _gasneti_threadkey_check((key), 1),   \
      pthread_getspecific((key).value) )
  #define gasneti_threadkey_set_noinit(key, newvalue) do {                \
    _gasneti_threadkey_check((key), 1);                                   \
    gasneti_assert_zeroret(pthread_setspecific((key).value, (newvalue))); \
  } while (0)
  GASNETI_NEVER_INLINE(_gasneti_threadkey_init, /* avoid inserting overhead for an uncommon path */
  static void _gasneti_threadkey_init(_gasneti_threadkey_t *pkey)) {
    _gasneti_threadkey_check(*pkey, 0);
    gasneti_mutex_lock(&(pkey->initmutex));
      if (pkey->isinit == 0) {
        gasneti_assert_zeroret(pthread_key_create(&(pkey->value),NULL));
        { /* need a wmb, but have to avoid a header dependency cycle */
          gasneti_mutex_t dummymutex = GASNETI_MUTEX_INITIALIZER;
          gasneti_mutex_lock(&dummymutex);gasneti_mutex_unlock(&dummymutex); 
        }
        pkey->isinit = 1;
      } 
    gasneti_mutex_unlock(&(pkey->initmutex));
    _gasneti_threadkey_check(*pkey, 1);
  }
  #define gasneti_threadkey_init(key) _gasneti_threadkey_init(&(key))
  #define gasneti_threadkey_get(key)       \
    ( _gasneti_threadkey_check(key, 0),    \
      ( PREDICT_FALSE((key).isinit == 0) ? \
        gasneti_threadkey_init(key) :      \
        ((void)0) ),                       \
      gasneti_threadkey_get_noinit(key) )

  #define gasneti_threadkey_set(key,newvalue) do { \
      _gasneti_threadkey_check(key, 0);            \
      if_pf((key).isinit == 0)                     \
        gasneti_threadkey_init(key);               \
      gasneti_threadkey_set_noinit(key, newvalue); \
    } while (0)
#else
  #define _gasneti_threadkey_check(key)         \
          gasneti_assert((key).magic == _GASNETI_THREADKEY_MAGIC)
  #define gasneti_threadkey_init(key) _gasneti_threadkey_check(key)
  #define gasneti_threadkey_get_noinit(key) \
    (_gasneti_threadkey_check(key), (key).value)
  #define gasneti_threadkey_set_noinit(key, newvalue) do { \
    _gasneti_threadkey_check(key);                         \
    (key).value = (newvalue);                              \
    } while (0)
  #define gasneti_threadkey_get gasneti_threadkey_get_noinit
  #define gasneti_threadkey_set gasneti_threadkey_set_noinit
#endif

/* ------------------------------------------------------------------------------------ */
/* environment support */

/* format a integer value as a human-friendly string, with appropriate mem suffix */
extern char *gasneti_format_number(int64_t val, char *buf, size_t bufsz, int is_mem_size);
/* parse an integer value back out again
  if mem_size_multiplier==0, it's a unitless quantity
  otherwise, it's a memory size quantity, and mem_size_multiplier provides the 
    default memory unit (ie 1024=1KB) if the string provides none  */
extern int64_t gasneti_parse_int(const char *str, uint64_t mem_size_multiplier);

/* set/unset an environment variable, for the local process ONLY */
extern void gasneti_setenv(const char *key, const char *value);
extern void gasneti_unsetenv(const char *key);

/* Conduit-specific supplement to gasneti_getenv
 * If set to non-NULL this has precedence over gasneti_globalEnv.
 */
typedef char *(gasneti_getenv_fn_t)(const char *keyname);
extern gasneti_getenv_fn_t *gasneti_conduit_getenv;

/* GASNet environment query function
 * uses the gasneti_globalEnv if available or regular getenv otherwise
 * legal to call before gasnet_init, but may malfunction if
 * the conduit has not yet established the contents of the environment
 */
extern char *gasneti_getenv(const char *keyname);

/* GASNet environment query for a string parameter
   if user has set value the return value indicates their selection
   if value is not set, the provided default value is returned
   call is reported to the console in verbose-environment mode,
   so this function should never be called more than once per key
   legal to call before gasnet_init, but may malfunction if
   the conduit has not yet established the contents of the environment
 */
extern char *gasneti_getenv_withdefault(const char *keyname, const char *defaultval);

/* GASNet environment query for a yes/no parameter
   if user has set value to 'Y|YES|y|yes|1' or 'N|n|NO|no|0', 
   the return value indicates their selection
   if value is not set, the provided default value is returned
   same restrictions on gasneti_getenv_withdefault also apply
 */
extern int gasneti_getenv_yesno_withdefault(const char *keyname, int defaultval);

/* GASNet environment query for an integral parameter
   if mem_size_multiplier non-zero, expect a (possibly fractional) memory size with suffix (B|KB|MB|GB|TB)
     and the default multiplier is mem_size_multiplier (eg 1024 for KB)
   otherwise, expect a positive or negative integer in decimal or hex ("0x" prefix)
   the return value indicates their selection
   if value is not set, the provided default value is returned
   same restrictions on gasneti_getenv_withdefault also apply
 */
extern int64_t gasneti_getenv_int_withdefault(const char *keyname, int64_t defaultval, uint64_t mem_size_multiplier);

/* gasneti_verboseenv() returns true iff GASNET_VERBOSEENV reporting is enabled on this node 
   note the answer may change during initialization
 */
extern int gasneti_verboseenv();

/* display an integral/string environment setting iff gasneti_verboseenv() */
extern void gasneti_envint_display(const char *key, int64_t val, int is_dflt, int is_mem_size);
extern void gasneti_envstr_display(const char *key, const char *val, int is_dflt);
/* ------------------------------------------------------------------------------------ */

#if PLATFORM_OS_AIX
  /* AIX's stdio.h won't provide prototypes for snprintf() and vsnprintf()
   * by default since they are in C99 but not C89.
   */
  GASNETI_FORMAT_PRINTF(snprintf,3,4,
  extern int snprintf(char * s, size_t n, const char * format, ...));
  GASNETI_FORMAT_PRINTF(vsnprintf,3,0,
  extern int vsnprintf(char * s, size_t n, const char * format, va_list ap));
#endif

GASNETI_END_EXTERNC

#endif
