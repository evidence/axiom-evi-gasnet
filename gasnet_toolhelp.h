/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_toolhelp.h,v $
 *     $Date: 2007/01/05 08:09:35 $
 * $Revision: 1.25 $
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
#include <unistd.h>
#if GASNETI_THREADS
  #if PLATFORM_OS_LINUX
   struct timespec; /* avoid an annoying warning on Linux */
  #endif
  #include <pthread.h>
#endif

#ifndef STDIN_FILENO
  #define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
  #define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
  #define STDERR_FILENO 2
#endif

GASNETI_BEGIN_EXTERNC

#if PLATFORM_OS_MTA
   #include <machine/runtime.h>
   #define _gasneti_sched_yield() mta_yield()
#elif defined(HAVE_SCHED_YIELD) && !PLATFORM_OS_BLRTS && !PLATFORM_OS_CATAMOUNT
   #include <sched.h>
   #define _gasneti_sched_yield() sched_yield()
#else
   #define _gasneti_sched_yield() (sleep(0),0)
#endif
#define gasneti_sched_yield() gasneti_assert_zeroret(_gasneti_sched_yield())

#if PLATFORM_OS_MTA
  #define gasneti_filesystem_sync() mta_sync()
#elif PLATFORM_OS_CATAMOUNT
  #define gasneti_filesystem_sync() ((void)0)
#else
  #define gasneti_filesystem_sync() sync()
#endif

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

extern void gasneti_freezeForDebuggerErr(); /* freeze iff user enabled error freezing */
extern void gasneti_freezeForDebuggerNow(volatile int *flag, const char *flagsymname);
extern volatile int gasnet_frozen; /* export to simplify debugger restart */ 
extern void gasneti_backtrace_init(const char *exename);
extern int (*gasneti_print_backtrace_ifenabled)(int fd);
extern int gasneti_print_backtrace(int fd);

extern void gasneti_flush_streams(); /* flush all open streams */
extern void gasneti_close_streams(); /* close standard streams (for shutdown) */

extern int gasneti_cpu_count();

extern void gasneti_set_affinity(int rank);

const char *gasneti_gethostname(); /* returns the current host name - dies with an error on failure */

extern int gasneti_isLittleEndian();

typedef void (*gasneti_sighandlerfn_t)(int);
gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp);

/* return a fast but simple/insecure 64-bit checksum of arbitrary data */
extern uint64_t gasneti_checksum(const void *p, int numbytes);

/* ------------------------------------------------------------------------------------ */
/* Count zero bytes in a region w/ or w/o a memcpy(), or in a "register" */

extern size_t gasneti_count0s_copy(void * GASNETI_RESTRICT dst,
                                   const void * GASNETI_RESTRICT src,
                                   size_t len);
extern size_t gasneti_count0s(const void * src, size_t len);


GASNETI_INLINE(gasneti_count0s_uint32_t) GASNETI_CONST
int gasneti_count0s_uint32_t(uint32_t x) {
  x |= (x >> 4); x |= (x >> 2); x |= (x >> 1);
  x &= 0x01010101UL;
  x += (x >> 16); x += (x >> 8);
  return sizeof(x) - (x & 0xf);
}
#if PLATFORM_ARCH_32
  GASNETI_INLINE(gasneti_count0s_uint64_t) GASNETI_CONST
  int gasneti_count0s_uint64_t(uint64_t x) {
    return gasneti_count0s_uint32_t(GASNETI_LOWORD(x)) + 
           gasneti_count0s_uint32_t(GASNETI_HIWORD(x));
  }
  #define gasneti_count0s_uintptr_t(x) gasneti_count0s_uint32_t(x)
#elif PLATFORM_ARCH_64
  GASNETI_INLINE(gasneti_count0s_uint64_t) GASNETI_CONST
  int gasneti_count0s_uint64_t(uintptr_t x) {
    x |= (x >> 4); x |= (x >> 2); x |= (x >> 1);
    x &= 0x0101010101010101UL;
    x += (x >> 32); x += (x >> 16); x += (x >> 8);
    return sizeof(x) - (x & 0xf);
  }
  #define gasneti_count0s_uintptr_t(x) gasneti_count0s_uint64_t(x)
#else
  #error "Unknown word size"
#endif

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
   See README-tools for usage information.
*/
#define _GASNETI_THREADKEY_MAGIC 0xFF00ABCDEF573921ULL

#if GASNETI_THREADS
  #if GASNETI_HAVE_TLS_SUPPORT /* use __thread, if available */
    #define _GASNETI_THREADKEY_USES_TLS 1
  #else
    #define _GASNETI_THREADKEY_USES_PTHREAD_GETSPECIFIC 1
  #endif
#else /* no threads */
    #define _GASNETI_THREADKEY_USES_NOOP 1
#endif

#if _GASNETI_THREADKEY_USES_PTHREAD_GETSPECIFIC
  typedef struct { 
    #if GASNET_DEBUG
      uint64_t magic;
      #define _GASNETI_THREADKEY_MAGIC_INIT _GASNETI_THREADKEY_MAGIC,
    #else
      #define _GASNETI_THREADKEY_MAGIC_INIT
    #endif
      gasneti_mutex_t initmutex;
      volatile int isinit;
      pthread_key_t value;
  } _gasneti_threadkey_t;
  #define _GASNETI_THREADKEY_INITIALIZER \
    { _GASNETI_THREADKEY_MAGIC_INIT      \
      GASNETI_MUTEX_INITIALIZER,         \
      0 /* value field left NULL */ }
#else
  typedef void *_gasneti_threadkey_t;
  #define _GASNETI_THREADKEY_INITIALIZER NULL
#endif

#if _GASNETI_THREADKEY_USES_PTHREAD_GETSPECIFIC
  #define GASNETI_THREADKEY_DECLARE(key) \
    extern _gasneti_threadkey_t key
  #define GASNETI_THREADKEY_DEFINE(key) \
    _gasneti_threadkey_t key = _GASNETI_THREADKEY_INITIALIZER
#elif _GASNETI_THREADKEY_USES_TLS
  #if GASNETI_CONFIGURE_MISMATCH
    #define GASNETI_THREADKEY_DECLARE(key)         \
      extern void *_gasneti_threadkey_get_##key(); \
      extern void _gasneti_threadkey_set_##key(void *_val)
  #else
    #define GASNETI_THREADKEY_DECLARE(key) \
      extern __thread _gasneti_threadkey_t _gasneti_threadkey_val_##key
  #endif
  #define GASNETI_THREADKEY_DEFINE(key)                    \
    GASNETI_THREADKEY_DECLARE(key);                        \
    extern void *_gasneti_threadkey_get_##key() {          \
      return gasneti_threadkey_get(key);                   \
    }                                                      \
    extern void _gasneti_threadkey_set_##key(void *_val) { \
      gasneti_threadkey_set(key, _val);                    \
    }                                                      \
    __thread _gasneti_threadkey_t _gasneti_threadkey_val_##key = _GASNETI_THREADKEY_INITIALIZER
#else /* _GASNETI_THREADKEY_USES_NOOP */
  #define GASNETI_THREADKEY_DECLARE(key) \
    extern _gasneti_threadkey_t _gasneti_threadkey_val_##key
  #define GASNETI_THREADKEY_DEFINE(key) \
    _gasneti_threadkey_t _gasneti_threadkey_val_##key = _GASNETI_THREADKEY_INITIALIZER
#endif

#if _GASNETI_THREADKEY_USES_PTHREAD_GETSPECIFIC
  /* struct prevents accidental direct access, magic provides extra safety checks */
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
#else /* _GASNETI_THREADKEY_USES_TLS, _GASNETI_THREADKEY_USES_NOOP */
  /* name shift to _gasneti_threadkey_val_##key prevents accidental direct access */
  #define gasneti_threadkey_init(key) ((void)0)
  #if _GASNETI_THREADKEY_USES_TLS && GASNETI_CONFIGURE_MISMATCH
    /* defined as __thread data storage, but current compiler doesn't support TLS 
       use an extern function call as conservative fall-back position
     */
    #define gasneti_threadkey_get_noinit(key) \
          (_gasneti_threadkey_get_##key())
    #define gasneti_threadkey_set_noinit(key, newvalue) \
          (_gasneti_threadkey_set_##key(newvalue))
  #else
    #define gasneti_threadkey_get_noinit(key) \
          (_gasneti_threadkey_val_##key)
    #define gasneti_threadkey_set_noinit(key, newvalue) \
         ((_gasneti_threadkey_val_##key) = (newvalue))
  #endif
  #define gasneti_threadkey_get gasneti_threadkey_get_noinit
  #define gasneti_threadkey_set gasneti_threadkey_set_noinit
#endif

/* ------------------------------------------------------------------------------------ */
/* environment support 
   see README-tools for usage information 
 */

extern char *gasneti_format_number(int64_t val, char *buf, size_t bufsz, int is_mem_size);
extern int64_t gasneti_parse_int(const char *str, uint64_t mem_size_multiplier);
extern void gasneti_setenv(const char *key, const char *value);
extern void gasneti_unsetenv(const char *key);

extern char *gasneti_getenv(const char *keyname);
extern char *gasneti_getenv_withdefault(const char *keyname, const char *defaultval);
extern int gasneti_getenv_yesno_withdefault(const char *keyname, int defaultval);
extern int64_t gasneti_getenv_int_withdefault(const char *keyname, int64_t defaultval, uint64_t mem_size_multiplier);
extern int gasneti_verboseenv();
extern void gasneti_envint_display(const char *key, int64_t val, int is_dflt, int is_mem_size);
extern void gasneti_envstr_display(const char *key, const char *val, int is_dflt);

/* Conduit-specific supplement to gasneti_getenv
 * If set to non-NULL this has precedence over gasneti_globalEnv.
 */
typedef char *(gasneti_getenv_fn_t)(const char *keyname);
extern gasneti_getenv_fn_t *gasneti_conduit_getenv;


/* ------------------------------------------------------------------------------------ */
/* Attempt to maximize allowable cpu and memory resource limits for this
 * process, silently ignoring any errors
 * return non-zero on success */
int gasnett_maximize_rlimits();
/* maximize a particular rlimit, and return non-zero on success.
   For portability, this should be called within an ifdef to ensure 
   the specified RLIMIT_ constant exists
 */
int gasnett_maximize_rlimit(int res, const char *lim_desc);

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
