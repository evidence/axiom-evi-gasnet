/*  $Archive:: /Ti/GASNet/gasnet_internal.h                               $
 *     $Date: 2002/12/01 06:03:28 $
 * $Revision: 1.22 $
 * Description: GASNet header for internal definitions used in GASNet implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _GASNET_INTERNAL_H
#define _GASNET_INTERNAL_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

BEGIN_EXTERNC

#include <gasnet.h>

/*  safe memory allocation/deallocation */
GASNET_INLINE_MODIFIER(_gasneti_malloc)
void *_gasneti_malloc(size_t nbytes, char *curloc) {
  void *ret = NULL;
  gasnet_hold_interrupts();
  ret = malloc(nbytes);
  if_pf (ret == NULL)
    gasneti_fatalerror("malloc(%d) failed: %s", nbytes, 
      curloc == NULL ? "" : curloc);
  gasnet_resume_interrupts();
  return ret;
}
#ifdef DEBUG
#define gasneti_malloc(nbytes)	_gasneti_malloc(nbytes, __FILE__ ":" _STRINGIFY(__LINE__))
#else
#define gasneti_malloc(nbytes)	_gasneti_malloc(nbytes, NULL)
#endif
GASNET_INLINE_MODIFIER(gasneti_free)
void gasneti_free(void *ptr) {
  gasnet_hold_interrupts();
  free(ptr);
  gasnet_resume_interrupts();
}
GASNET_INLINE_MODIFIER(_gasneti_malloc_inhandler)
void *_gasneti_malloc_inhandler(size_t nbytes, char *curloc) {
  void *ret = NULL;
  ret = malloc(nbytes);
  if_pf (ret == NULL)
    gasneti_fatalerror("malloc_inhandler(%d) failed: %s", 
      nbytes, curloc == NULL ? "" : curloc);
  return ret;
}
#ifdef DEBUG
#define gasneti_malloc_inhandler(nbytes) \
         _gasneti_malloc_inhandler(nbytes, __FILE__ ":" _STRINGIFY(__LINE__))
#else
#define gasneti_malloc_inhandler(nbytes) _gasneti_malloc_inhandler(nbytes, NULL)
#endif
GASNET_INLINE_MODIFIER(gasneti_free_inhandler)
void gasneti_free_inhandler(void *ptr) {
  free(ptr);
}
/* ------------------------------------------------------------------------------------ */
/* page alignment macros */
#define GASNETI_PAGE_ALIGN(p,P) ((uintptr_t)(p)&~ ((uintptr_t)(P)-1))
#define GASNETI_PAGE_ROUNDUP(p,P) (GASNETI_PAGE_ALIGN((uintptr_t)(p)+((P)-1), P))
/* ------------------------------------------------------------------------------------ */
/* portable microsecond granularity wall-clock timer */
extern int64_t gasneti_getMicrosecondTimeStamp(void);

extern void gasneti_freezeForDebugger();
/* ------------------------------------------------------------------------------------ */
/* portable atomic increment */

#if defined(SOLARIS) || defined(CRAYT3E) || defined(__PGI) || \
    (defined(OSF) && !defined(__DECC))
  #define GASNETI_USE_GENERIC_ATOMICOPS /* TODO: no atomic ops on T3e? */
#endif

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  /* a very slow but portable implementation of atomic ops */
  extern gasnet_hsl_t gasneti_atomicop_lock;

  typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
  #define gasneti_atomic_read(p)      ((p)->ctr)
  #define gasneti_atomic_init(v)      { (v) }
  #define gasneti_atomic_set(p,v)     (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                       (p)->ctr = (v),                          \
                                       gasnet_hsl_unlock(&gasneti_atomicop_lock))
  #define gasneti_atomic_increment(p) (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                      ((p)->ctr)++,                             \
                                       gasnet_hsl_unlock(&gasneti_atomicop_lock))
  #define gasneti_atomic_decrement(p) (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                      ((p)->ctr)--,                             \
                                       gasnet_hsl_unlock(&gasneti_atomicop_lock))
#else
  #if defined(LINUX)
    #ifdef BROKEN_LINUX_ASM_ATOMIC_H
      /* some versions of the linux kernel ship with a broken atomic.h
         this code based on a non-broken version of the header */
      #include <linux/config.h>
      #ifdef CONFIG_SMP
        #define GASNETI_LOCK "lock ; "
      #else
        #define GASNETI_LOCK ""
      #endif

      #ifndef __i386__
        #error you have broken Linux system headers and a broken CPU. barf...
      #endif

      typedef struct { volatile int counter; } gasneti_atomic_t;
      #define gasneti_atomic_read(p)      ((p)->counter)
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_set(p,v)     ((p)->counter = (v))
      static __inline__ void 
        gasneti_atomic_increment(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "incl %0"
                :"=m" (v->counter)
                :"m" (v->counter));
      }
      static __inline__ void 
        gasneti_atomic_decrement(gasneti_atomic_t *v){
        __asm__ __volatile__(
                GASNETI_LOCK "decl %0"
                :"=m" (v->counter)
                :"m" (v->counter));
      }
    #else
      #ifdef __alpha__
        /* work-around for a puzzling header bug in alpha Linux */
        #define extern static
      #endif
      #include <asm/atomic.h>
      #ifdef __alpha__
        #undef extern
      #endif
      typedef atomic_t gasneti_atomic_t;
      #define gasneti_atomic_increment(p) atomic_inc(p)
      #define gasneti_atomic_decrement(p) atomic_dec(p)
      #define gasneti_atomic_read(p)      atomic_read(p)
      #define gasneti_atomic_set(p,v)     atomic_set(p,v)
      #define gasneti_atomic_init(v)      ATOMIC_INIT(v)
    #endif
  #elif defined(FREEBSD)
    #include <machine/atomic.h>
    typedef volatile u_int32_t gasneti_atomic_t;
    #define gasneti_atomic_increment(p) atomic_add_int((p),1)
    #define gasneti_atomic_decrement(p) atomic_subtract_int((p),1)
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
  #elif defined(CYGWIN)
    #include <windows.h>
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) InterlockedIncrement((LONG *)&((p)->ctr))
    #define gasneti_atomic_decrement(p) InterlockedDecrement((LONG *)&((p)->ctr))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
  #elif defined(AIX)
    #include <sys/atomic_op.h>
    typedef struct { volatile int ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (fetch_and_add((atomic_p)&((p)->ctr),1))
    #define gasneti_atomic_decrement(p) (fetch_and_add((atomic_p)&((p)->ctr),-1))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
  #elif defined(OSF)
   #if 1
    #include <sys/machine/builtins.h>
    typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (__ATOMIC_INCREMENT_LONG(&((p)->ctr)))
    #define gasneti_atomic_increment(p) (__ATOMIC_DECREMENT_LONG(&((p)->ctr)))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
   #else
    #include <sys/systm.h>
    typedef struct { volatile int ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (atomic_incl(&((p)->ctr)))
    #define gasneti_atomic_increment(p) (atomic_decl(&((p)->ctr)))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
   #endif
  #elif defined(IRIX)
    #include <mutex.h>
    typedef __uint32_t gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (test_then_add32((p),1))
    #define gasneti_atomic_decrement(p) (test_then_add32((p),(uint32_t)-1))
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
  #elif 0 && defined(SOLARIS)
    /* $%*(! Solaris has atomic functions in the kernel but refuses to expose them
       to the user... after all, what application would be interested in performance? */
    #include <sys/atomic.h>
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (atomic_add_32((uint32_t *)&((p)->ctr),1))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
    #if 0
      /* according to the "Alpha Architecture Handbook", the following code 
         should work on alpha, but the assembler chokes on the load-locked 
         with "unknown opcode", for both GNU and Sun assemblers. Alpha sucks.
       */
      static __inline__ void atomic_add_32(int32_t *v, int32_t op) {
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t temp;
        __asm__ __volatile__( 
          "retry: \n"
          "ldl_l %0, %1\n" 
          "addl %0, %2, %0\n"
          "stl_c %0, %1\n"
          "beq %0, retry\n"
          "nop\n"
          : "=&r" (temp)          /* outputs */
          : "r" (addr), "r" (op)  /* inputs */
          : "memory", "cc");      /* kills */
        assert(temp);
      }
    #endif
  #else
    #error Need to implement atomic increment/decrement for this platform...
  #endif
#endif
/* ------------------------------------------------------------------------------------ */
/* memory segment registration and management */

size_t gasneti_getSystemPageSize();

typedef void (*gasneti_sighandlerfn_t)(int);
void gasneti_registerSignalHandlers(gasneti_sighandlerfn_t handler);
void gasneti_defaultSignalHandler(int sig);

#ifdef HAVE_MMAP
  extern gasnet_seginfo_t gasneti_mmap_segment_search(uintptr_t maxsz);
  extern void gasneti_mmap_fixed(void *segbase, size_t segsize);
  extern void gasneti_munmap(void *segbase, size_t segsize);
#endif

#ifndef GASNETI_MAX_MALLOCSEGMENT_SZ
#define GASNETI_MAX_MALLOCSEGMENT_SZ (100*1048576) /* Max segment sz to use when mmap not avail */
#endif
#ifndef GASNETI_USE_HIGHSEGMENT
#define GASNETI_USE_HIGHSEGMENT 1  /* use the high end of mmap segments */
#endif

typedef void (*gasneti_bootstrapExchangefn_t)(void *src, size_t len, void *dest);

void gasneti_segmentInit(uintptr_t *MaxLocalSegmentSize, 
                         uintptr_t *MaxGlobalSegmentSize,
                         uintptr_t localSegmentLimit,
                         gasnet_node_t numnodes,
                         gasneti_bootstrapExchangefn_t exchangefn);
void gasneti_segmentAttach(uintptr_t segsize, uintptr_t minheapoffset,
                           gasnet_seginfo_t *seginfo,
                           gasneti_bootstrapExchangefn_t exchangefn);
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasneti_ErrorName)
char *gasneti_ErrorName(int errval) {
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
GASNET_INLINE_MODIFIER(gasneti_ErrorDesc)
char *gasneti_ErrorDesc(int errval) {
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

/* ------------------------------------------------------------------------------------ */
/* macros for returning errors that allow verbose error tracking */
extern int gasneti_VerboseErrors;
#define GASNETI_RETURN_ERR(type) do {                                        \
  if (gasneti_VerboseErrors) {                                                 \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n" \
      "  at %s:%i\n"                                                         \
      ,GASNETI_CURRENT_FUNCTION                                              \
      , #type, gasneti_ErrorDesc(GASNET_ERR_##type), __FILE__, __LINE__);    \
    fflush(stderr);                                                          \
    }                                                                        \
  return GASNET_ERR_ ## type;                                                \
  } while (0)
#define GASNETI_RETURN_ERRF(type, fromfn) do {                                     \
  if (gasneti_VerboseErrors) {                                                       \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"       \
      "  from function %s\n"                                                       \
      "  at %s:%i\n"                                                               \
      ,GASNETI_CURRENT_FUNCTION                                                    \
      , #type, gasneti_ErrorDesc(GASNET_ERR_##type), #fromfn, __FILE__, __LINE__); \
    fflush(stderr);                                                                \
    }                                                                              \
  return GASNET_ERR_ ## type;                                                      \
  } while (0)
#define GASNETI_RETURN_ERRR(type, reason) do {                                    \
  if (gasneti_VerboseErrors) {                                                               \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"               \
      "  at %s:%i\n"                                                                       \
      "  reason: %s\n"                                                                     \
      ,GASNETI_CURRENT_FUNCTION                                                            \
      , #type, gasneti_ErrorDesc(GASNET_ERR_##type), __FILE__, __LINE__, reason); \
    fflush(stderr);                                                                        \
    }                                                                                      \
  return GASNET_ERR_ ## type;                                                              \
  } while (0)
#define GASNETI_RETURN_ERRFR(type, fromfn, reason) do {                                    \
  if (gasneti_VerboseErrors) {                                                               \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"               \
      "  from function %s\n"                                                               \
      "  at %s:%i\n"                                                                       \
      "  reason: %s\n"                                                                     \
      ,GASNETI_CURRENT_FUNCTION                                                            \
      , #type, gasneti_ErrorDesc(GASNET_ERR_##type), #fromfn, __FILE__, __LINE__, reason); \
    fflush(stderr);                                                                        \
    }                                                                                      \
  return GASNET_ERR_ ## type;                                                              \
  } while (0)

/* return a possible error */
#define GASNETI_RETURN(val) do {                                             \
  if (gasneti_VerboseErrors && val != GASNET_OK) {                           \
    fprintf(stderr, "GASNet %s returning an error code: %s (%s)\n"           \
      "  at %s:%i\n"                                                         \
      ,GASNETI_CURRENT_FUNCTION                                              \
      , gasneti_ErrorName(val), gasneti_ErrorDesc(val), __FILE__, __LINE__); \
    fflush(stderr);                                                          \
    }                                                                        \
  return val;                                                                \
  } while (0)

/* ------------------------------------------------------------------------------------ */
/* Error checking system mutexes -
     wrapper around pthread mutexes that provides extra support for 
     error checking when DEBUG is defined
   gasneti_mutex_lock(&lock)/gasneti_mutex_unlock(&lock) - 
     lock and unlock (checks for recursive locking errors)
   gasneti_mutex_assertlocked(&lock)/gasneti_mutex_assertunlocked(&lock) - 
     allow functions to assert a given lock is held / not held by the current thread
 */
#ifdef DEBUG
  #define GASNETI_MUTEX_NOOWNER       -1
  #ifndef GASNETI_THREADIDQUERY
    /* allow conduit override of thread-id query */
    #ifdef GASNET_PAR
      #define GASNETI_THREADIDQUERY()   ((uintptr_t)pthread_self())
    #else
      #define GASNETI_THREADIDQUERY()   (0)
    #endif
  #endif
  #ifdef GASNET_PAR
    #include <pthread.h>
    typedef struct {
      pthread_mutex_t lock;
      uintptr_t owner;
    } gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, GASNETI_MUTEX_NOOWNER }
    #define gasneti_mutex_lock(pl) do {                       \
              int retval;                                     \
              assert((pl)->owner != GASNETI_THREADIDQUERY()); \
              retval = pthread_mutex_lock(&((pl)->lock));     \
              assert(!retval);                                \
              assert((pl)->owner == GASNETI_MUTEX_NOOWNER);   \
              (pl)->owner = GASNETI_THREADIDQUERY();          \
            } while (0)
    #define gasneti_mutex_unlock(pl) do {                     \
              int retval;                                     \
              assert((pl)->owner == GASNETI_THREADIDQUERY()); \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;            \
              retval = pthread_mutex_unlock(&((pl)->lock));   \
              assert(!retval);                                \
            } while (0)
  #else
    typedef struct {
      int owner;
    } gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER   { GASNETI_MUTEX_NOOWNER }
    #define gasneti_mutex_lock(pl) do {                     \
              assert((pl)->owner == GASNETI_MUTEX_NOOWNER); \
              (pl)->owner = GASNETI_THREADIDQUERY();        \
            } while (0)
    #define gasneti_mutex_unlock(pl) do {                     \
              assert((pl)->owner == GASNETI_THREADIDQUERY()); \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;            \
            } while (0)
  #endif
  #define gasneti_mutex_assertlocked(pl)    assert((pl)->owner == GASNETI_THREADIDQUERY())
  #define gasneti_mutex_assertunlocked(pl)  assert((pl)->owner != GASNETI_THREADIDQUERY())
#else
  #ifdef GASNET_PAR
    #include <pthread.h>
    typedef pthread_mutex_t           gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
    #if 1
      #define gasneti_mutex_lock(pl)  do {        \
          int retval = pthread_mutex_trylock(pl); \
          if (!retval) break;                     \
          assert(retval == EBUSY);                \
        } while (1)
    #else
      #define gasneti_mutex_lock(pl)  pthread_mutex_lock(pl)
    #endif
    #define gasneti_mutex_unlock(pl)  pthread_mutex_unlock(pl)
  #else
    typedef char           gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER '\0'
    #define gasneti_mutex_lock(pl)    
    #define gasneti_mutex_unlock(pl)  
  #endif
  #define gasneti_mutex_assertlocked(pl)
  #define gasneti_mutex_assertunlocked(pl)
#endif
/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif
