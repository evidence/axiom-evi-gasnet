/*  $Archive:: /Ti/GASNet/gasnet_internal.h                               $
 *     $Date: 2003/09/15 16:50:03 $
 * $Revision: 1.41 $
 * Description: GASNet header for internal definitions used in GASNet implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_INTERNAL_H
#define _GASNET_INTERNAL_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#include <gasnet.h>

BEGIN_EXTERNC

#ifdef __SUNPRO_C
  #pragma error_messages(off, E_END_OF_LOOP_CODE_NOT_REACHED)
#endif

extern int gasneti_init_done; /*  true after init */
extern int gasneti_attach_done; /*  true after attach */

/*  safe memory allocation/deallocation */
#ifdef DEBUG
  extern void *_gasneti_malloc(size_t nbytes, char *curloc);
  extern void _gasneti_free(void *ptr, char *curloc);
  extern void *_gasneti_calloc(size_t N, size_t S, char *curloc);
  #define gasneti_malloc(nbytes) _gasneti_malloc(nbytes, __FILE__ ":" _STRINGIFY(__LINE__))
  #define gasneti_calloc(N,S)    _gasneti_calloc(N,S, __FILE__ ":" _STRINGIFY(__LINE__))
  #define gasneti_free(ptr)	 _gasneti_free(ptr, __FILE__ ":" _STRINGIFY(__LINE__))
#else
  GASNET_INLINE_MODIFIER(gasneti_malloc)
  void *gasneti_malloc(size_t nbytes) {
    void *ret = NULL;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = malloc(nbytes);
    if_pf (ret == NULL) 
      gasneti_fatalerror("gasneti_malloc(%d) failed", nbytes);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(gasneti_free)
  void gasneti_free(void *ptr) {
    if_pf (ptr == NULL) return;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    free(ptr);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }
  GASNET_INLINE_MODIFIER(gasneti_calloc)
  void *gasneti_calloc(size_t N, size_t S) {
    size_t nbytes = N*S;
    void *ptr = gasneti_malloc(nbytes);
    memset(ptr,0,nbytes);
    return ptr;
  }
#endif
/* Beware - in debug mode, 
   gasneti_malloc/gasneti_calloc/gasneti_free are NOT
   compatible with malloc/calloc/free
   (freeing memory allocated from one using the other is likely to crash)
 */
#define malloc !!! ERROR: GASNet conduit code must use gasneti_malloc !!!
#define calloc !!! ERROR: GASNet conduit code must use gasneti_calloc !!!
#define free   !!! ERROR: GASNet conduit code must use gasneti_free   !!!
/* ------------------------------------------------------------------------------------ */
/* Version of strdup() which is compatible w/ gasneti_free(), instead of plain free() */
GASNET_INLINE_MODIFIER(gasneti_strdup)
char *gasneti_strdup(const char *s) {
  char *retval;

  if_pf (s == NULL) {
    /* special case to avoid strlen(NULL) */
    retval = (char *)gasneti_malloc(1);
    retval[0] = '\0';
  } else {
    size_t sz = strlen(s) + 1;
    retval = (char *)memcpy((char *)gasneti_malloc(sz), s, sz);
  }

  return retval;
}
/* Like gasneti_strdup, but copy is limited to at most n characters.
 * Note allocation is upto n+1 bytes, due to the '\0' termination.
 */
GASNET_INLINE_MODIFIER(gasneti_strndup)
char *gasneti_strndup(const char *s, size_t n) {
  char *retval;

  if_pf ((s == NULL) || (n == 0)) {
    /* special case to avoid strlen(NULL) */
    retval = (char *)gasneti_malloc(1);
    retval[0] = '\0';
  } else {
    size_t len = strlen(s);

    if (len > n) { len = n; }
    retval = gasneti_malloc(len + 1);
    retval[len] = '\0';  /* memcpy won't overwrite this byte */

    (void)memcpy(retval, s, len);
  }

  return retval;
}

/* ------------------------------------------------------------------------------------ */
/* page alignment macros */
#define GASNETI_ALIGNDOWN(p,P)    ((uintptr_t)(p)&~((uintptr_t)(P)-1))
#define GASNETI_ALIGNUP(p,P)     (GASNETI_ALIGNDOWN((uintptr_t)(p)+((P)-1),P))

#define GASNETI_PAGE_ALIGNDOWN(p) (GASNETI_ALIGNDOWN(p,GASNET_PAGESIZE))
#define GASNETI_PAGE_ALIGNUP(p)   (GASNETI_ALIGNUP(p,GASNET_PAGESIZE))
/* ------------------------------------------------------------------------------------ */

extern void gasneti_freezeForDebugger();
extern void gasneti_killmyprocess(int exitcode);

/* DEBUG_VERBOSE is set by configure to request job startup and general 
   status messages on stderr 
*/
#ifndef DEBUG_VERBOSE
  #define DEBUG_VERBOSE               0
#endif

/* ------------------------------------------------------------------------------------ */
/* memory segment registration and management */

typedef void (*gasneti_sighandlerfn_t)(int);
void gasneti_registerSignalHandlers(gasneti_sighandlerfn_t handler);
void gasneti_defaultSignalHandler(int sig);
gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp);

#ifdef HAVE_MMAP
  extern gasnet_seginfo_t gasneti_mmap_segment_search(uintptr_t maxsz);
  extern void gasneti_mmap_fixed(void *segbase, size_t segsize);
  extern void *gasneti_mmap(size_t segsize);
  extern void gasneti_munmap(void *segbase, size_t segsize);
#endif

#ifndef GASNETI_MAX_MALLOCSEGMENT_SZ
#define GASNETI_MAX_MALLOCSEGMENT_SZ (100*1048576) /* Max segment sz to use when mmap not avail */
#endif
#ifndef GASNETI_USE_HIGHSEGMENT
#define GASNETI_USE_HIGHSEGMENT 1  /* use the high end of mmap segments */
#endif

typedef void (*gasneti_bootstrapExchangefn_t)(void *src, size_t len, void *dest);
typedef void (*gasneti_bootstrapBroadcastfn_t)(void *src, size_t len, void *dest, int rootnode);

void gasneti_segmentInit(uintptr_t *MaxLocalSegmentSize, 
                         uintptr_t *MaxGlobalSegmentSize,
                         uintptr_t localSegmentLimit,
                         gasnet_node_t numnodes,
                         gasneti_bootstrapExchangefn_t exchangefn);
void gasneti_segmentAttach(uintptr_t segsize, uintptr_t minheapoffset,
                           gasnet_seginfo_t *seginfo,
                           gasneti_bootstrapExchangefn_t exchangefn);
void gasneti_setupGlobalEnvironment(gasnet_node_t numnodes, gasnet_node_t mynode,
                                     gasneti_bootstrapExchangefn_t exchangefn,
                                     gasneti_bootstrapBroadcastfn_t broadcastfn);

/* ------------------------------------------------------------------------------------ */
/* macros for returning errors that allow verbose error tracking */
extern int gasneti_VerboseErrors;
#define GASNETI_RETURN_ERR(type) do {                                        \
  if (gasneti_VerboseErrors) {                                                 \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n" \
      "  at %s:%i\n"                                                         \
      ,GASNETI_CURRENT_FUNCTION                                              \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), __FILE__, __LINE__);     \
    fflush(stderr);                                                          \
    }                                                                        \
  return GASNET_ERR_ ## type;                                                \
  } while (0)
#define GASNETI_RETURN_ERRF(type, fromfn) do {                                     \
  if (gasneti_VerboseErrors) {                                                     \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"      \
      "  from function %s\n"                                                       \
      "  at %s:%i\n"                                                               \
      ,GASNETI_CURRENT_FUNCTION                                                    \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), #fromfn, __FILE__, __LINE__);  \
    fflush(stderr);                                                                \
    }                                                                              \
  return GASNET_ERR_ ## type;                                                      \
  } while (0)
#define GASNETI_RETURN_ERRR(type, reason) do {                                             \
  if (gasneti_VerboseErrors) {                                                             \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"              \
      "  at %s:%i\n"                                                                       \
      "  reason: %s\n"                                                                     \
      ,GASNETI_CURRENT_FUNCTION                                                            \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), __FILE__, __LINE__, reason);           \
    fflush(stderr);                                                                        \
    }                                                                                      \
  return GASNET_ERR_ ## type;                                                              \
  } while (0)
#define GASNETI_RETURN_ERRFR(type, fromfn, reason) do {                                    \
  if (gasneti_VerboseErrors) {                                                             \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"              \
      "  from function %s\n"                                                               \
      "  at %s:%i\n"                                                                       \
      "  reason: %s\n"                                                                     \
      ,GASNETI_CURRENT_FUNCTION                                                            \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), #fromfn, __FILE__, __LINE__, reason);  \
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
      , gasnet_ErrorName(val), gasnet_ErrorDesc(val), __FILE__, __LINE__);   \
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
#ifndef GASNETI_FORCE_TRUE_MUTEXES
  /* GASNETI_FORCE_TRUE_MUTEXES will force gasneti_mutex_t to always
     use true locking (even under GASNET_SEQ config), 
     for inherently multi-threaded conduits such as lapi-conduit
   */
  #define GASNETI_FORCE_TRUE_MUTEXES 0
#endif
#ifdef DEBUG
  #define GASNETI_MUTEX_NOOWNER       -1
  #ifndef GASNETI_THREADIDQUERY
    /* allow conduit override of thread-id query */
    #if defined(GASNET_PAR) || GASNETI_FORCE_TRUE_MUTEXES
      #define GASNETI_THREADIDQUERY()   ((uintptr_t)pthread_self())
    #else
      #define GASNETI_THREADIDQUERY()   (0)
    #endif
  #endif
  #if defined(GASNET_PAR) || GASNETI_FORCE_TRUE_MUTEXES
    #include <pthread.h>
    typedef struct {
      pthread_mutex_t lock;
      uintptr_t owner;
    } gasneti_mutex_t;
    #if defined(PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP)
      /* These are faster, though less "featureful" than the default
       * mutexes on linuxthreads implementations which offer them.
       */
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP, (uintptr_t)GASNETI_MUTEX_NOOWNER }
    #else
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, (uintptr_t)GASNETI_MUTEX_NOOWNER }
    #endif
    #define gasneti_mutex_lock(pl) do {                                \
              int retval;                                              \
              assert((pl)->owner != GASNETI_THREADIDQUERY());          \
              retval = pthread_mutex_lock(&((pl)->lock));              \
              assert(!retval);                                         \
              assert((pl)->owner == (uintptr_t)GASNETI_MUTEX_NOOWNER); \
              (pl)->owner = GASNETI_THREADIDQUERY();                   \
            } while (0)
    #define gasneti_mutex_unlock(pl) do {                     \
              int retval;                                     \
              assert((pl)->owner == GASNETI_THREADIDQUERY()); \
              (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER; \
              retval = pthread_mutex_unlock(&((pl)->lock));   \
              assert(!retval);                                \
            } while (0)
    #define gasneti_mutex_init(pl) do {                       \
              pthread_mutex_init(&((pl)->lock),NULL);         \
             (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER; \
            } while (0)
    #define gasneti_mutex_destroy(pl)  pthread_mutex_destroy(&((pl)->lock))
  #else
    typedef struct {
      volatile int owner;
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
    #define gasneti_mutex_init(pl) do {                       \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;            \
            } while (0)
    #define gasneti_mutex_destroy(pl)
  #endif
  #define gasneti_mutex_assertlocked(pl)    assert((pl)->owner == GASNETI_THREADIDQUERY())
  #define gasneti_mutex_assertunlocked(pl)  assert((pl)->owner != GASNETI_THREADIDQUERY())
#else
  #if defined(GASNET_PAR) || GASNETI_FORCE_TRUE_MUTEXES
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
    #define gasneti_mutex_lock(pl)  pthread_mutex_lock(pl)
    #define gasneti_mutex_unlock(pl)  pthread_mutex_unlock(pl)
    #define gasneti_mutex_init(pl)  pthread_mutex_init((pl),NULL)
    #define gasneti_mutex_destroy(pl)  pthread_mutex_destroy(pl)
  #else
    typedef char           gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER '\0'
    #define gasneti_mutex_lock(pl)    
    #define gasneti_mutex_unlock(pl)  
    #define gasneti_mutex_init(pl)
    #define gasneti_mutex_destroy(pl)
  #endif
  #define gasneti_mutex_assertlocked(pl)
  #define gasneti_mutex_assertunlocked(pl)
#endif
/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif
