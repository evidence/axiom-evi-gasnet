/*  $Archive:: /Ti/GASNet/gasnet_internal.h                               $
 *     $Date: 2002/06/16 05:06:38 $
 * $Revision: 1.4 $
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
/* portable microsecond granularity wall-clock timer */
extern int64_t gasneti_getMicrosecondTimeStamp(void);
/* ------------------------------------------------------------------------------------ */
/* portable atomic increment */

#if defined(CRAYT3E)
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
#else
  #if defined(LINUX)
    #include <asm/atomic.h>
    typedef atomic_t gasneti_atomic_t;
    #define gasneti_atomic_increment(p) atomic_inc(p)
    #define gasneti_atomic_read(p)      atomic_read(p)
    #define gasneti_atomic_set(p,v)     atomic_set(p,v)
    #define gasneti_atomic_init(v)      ATOMIC_INIT(v)
  #elif defined(FREEBSD)
    #include <machine/atomic.h>
    typedef volatile u_int32_t gasneti_atomic_t;
    #define gasneti_atomic_increment(p) atomic_add_int((p),1)
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
  #elif defined(CYGWIN)
    #include <windows.h>
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) InterlockedIncrement((LONG *)&((p)->ctr))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
  #elif defined(AIX)
    #include <sys/atomic_op.h>
    typedef struct { volatile int ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (fetch_and_add((atomic_p)&((p)->ctr),1))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
  #elif defined(IRIX)
    #include <mutex.h>
    typedef __uint32_t gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (test_then_add32((p),1))
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
  #else
    #error Need to implement atomic increment for this platform...
  #endif
#endif
/* ------------------------------------------------------------------------------------ */

size_t gasneti_getSystemPageSize();

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


END_EXTERNC

#endif
