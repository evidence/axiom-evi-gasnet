/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended.h                  $
 *     $Date: 2003/06/16 10:24:13 $
 * $Revision: 1.18 $
 * Description: GASNet Extended API Header
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_H
#define _GASNET_EXTENDED_H

#include <string.h>
#include <assert.h>

#include <gasnet_extended_help.h>

BEGIN_EXTERNC

/*  TODO: add debug code to enforce restrictions on SEQ and PARSYNC config */
/*        (only one thread calls, HSL's only locked by that thread - how to check without pthread_getspecific()?) */
/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* passes back a pointer to a handler table containing the handlers of
    the extended API, which the core should register on its behalf
    (the table is terminated with an entry where fnptr == NULL)
   all handlers will have an index in range 100-199 
   may be called before gasnete_init()
*/
extern gasnet_handlerentry_t const *gasnete_get_handlertable();

/* Initialize the Extended API:
   must be called by the core API at the end of gasnet_attach() before calls to extended API
     (this function may make calls to the core functions)
*/
extern void gasnete_init();

/* ------------------------------------------------------------------------------------ */
/* helper macros */
#define _GASNETI_RETURN_V  return
#define _GASNETI_RETURN_H  return GASNET_INVALID_HANDLE
#define GASNETI_CHECKZEROSZ_GET(traceid, rt) do {      \
    if_pf (nbytes == 0) {                              \
      GASNETI_TRACE_GET(traceid,dest,node,src,nbytes); \
      _GASNETI_RETURN_##rt;                            \
    } } while(0)
#define GASNETI_CHECKZEROSZ_PUT(traceid, rt) do {      \
    if_pf (nbytes == 0) {                              \
      GASNETI_TRACE_PUT(traceid,node,dest,src,nbytes); \
      _GASNETI_RETURN_##rt;                            \
    } } while(0)
#define GASNETI_CHECKZEROSZ_MEMSET(traceid, rt) do {      \
    if_pf (nbytes == 0) {                                 \
      GASNETI_TRACE_MEMSET(traceid,node,dest,val,nbytes); \
      _GASNETI_RETURN_##rt;                               \
    } } while(0)

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* put_nb       source memory is safe to modify on return
   put_nb_bulk  source memory is NOT safe to modify on return
 */
extern gasnet_handle_t gasnete_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG);
extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG);
extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);
extern gasnet_handle_t gasnete_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes   GASNETE_THREAD_FARG);

#if GASNETI_DIRECT_GET_NB
  extern gasnet_handle_t gasnete_get_nb (void *dest, gasnet_node_t node, void *src,
                                         size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_get_nb gasnete_get_nb_bulk
#endif

GASNET_INLINE_MODIFIER(_gasnet_get_nb)
gasnet_handle_t _gasnet_get_nb      (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(GET_NB_LOCAL,H);
  gasnete_boundscheck(node, src, nbytes);
  gasnete_aligncheck(src, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_GET(GET_NB_LOCAL,dest,node,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    return GASNET_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_GET(GET_NB,dest,node,src,nbytes);
    return gasnete_get_nb(dest, node, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_get_nb(dest,node,src,nbytes) \
       _gasnet_get_nb(dest,node,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_put_nb)
gasnet_handle_t _gasnet_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(PUT_NB_LOCAL,H);
  gasnete_boundscheck(node, dest, nbytes);
  gasnete_aligncheck(src, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_NB_LOCAL,node,dest,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasneti_memsync();
    return GASNET_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_PUT(PUT_NB,node,dest,src,nbytes);
    return gasnete_put_nb(node, dest, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put_nb(node,dest,src,nbytes) \
       _gasnet_put_nb(node,dest,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_get_nb_bulk)
gasnet_handle_t _gasnet_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(GET_NB_BULK_LOCAL,H);
  gasnete_boundscheck(node, src, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_GET(GET_NB_BULK_LOCAL,dest,node,src,nbytes);
    GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes);
    return GASNET_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_GET(GET_NB_BULK,dest,node,src,nbytes);
    return gasnete_get_nb_bulk(dest, node, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_get_nb_bulk(dest,node,src,nbytes) \
       _gasnet_get_nb_bulk(dest,node,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_put_nb_bulk)
gasnet_handle_t _gasnet_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(PUT_NB_BULK_LOCAL,H);
  gasnete_boundscheck(node, dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_NB_BULK,node,dest,src,nbytes);
    GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes);
    gasneti_memsync();
    return GASNET_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_PUT(PUT_NB_BULK_LOCAL,node,dest,src,nbytes);
    return gasnete_put_nb_bulk(node, dest, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put_nb_bulk(node,dest,src,nbytes) \
       _gasnet_put_nb_bulk(node,dest,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_memset_nb)
gasnet_handle_t   _gasnet_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_MEMSET(MEMSET_NB_LOCAL,H);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_MEMSET(MEMSET_NB_LOCAL,node,dest,val,nbytes);
    memset(dest, val, nbytes);
    gasneti_memsync();
    return GASNET_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_MEMSET(MEMSET_NB,node,dest,val,nbytes);
    return gasnete_memset_nb(node, dest, val, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_memset_nb(node,dest,val,nbytes) \
       _gasnet_memset_nb(node,dest,val,nbytes GASNETE_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

extern int gasnete_try_syncnb(gasnet_handle_t handle);
extern int gasnete_try_syncnb_some(gasnet_handle_t *phandle, size_t numhandles);
extern int gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles);

GASNET_INLINE_MODIFIER(gasnet_try_syncnb)
int  gasnet_try_syncnb(gasnet_handle_t handle) {
  int result = GASNET_OK;
  if_pt (handle != GASNET_INVALID_HANDLE) 
    result = gasnete_try_syncnb(handle);
  GASNETI_TRACE_TRYSYNC(TRY_SYNCNB,result);
  return result;
}

GASNET_INLINE_MODIFIER(gasnet_try_syncnb_some)
int gasnet_try_syncnb_some(gasnet_handle_t *phandle, size_t numhandles) {
  int result = gasnete_try_syncnb_some(phandle,numhandles);
  GASNETI_TRACE_TRYSYNC(TRY_SYNCNB_SOME,result);
  return result;
}

GASNET_INLINE_MODIFIER(gasnet_try_syncnb_all)
int gasnet_try_syncnb_all(gasnet_handle_t *phandle, size_t numhandles) {
  int result = gasnete_try_syncnb_all(phandle,numhandles);
  GASNETI_TRACE_TRYSYNC(TRY_SYNCNB_ALL,result);
  return result;
}

#if GASNETI_DIRECT_WAIT_SYNCNB 
  extern void gasnete_wait_syncnb(gasnet_handle_t handle);
#else
  #define gasnete_wait_syncnb(handle) do {                                      \
      gasnet_handle_t _handle = (handle);                                       \
      if_pt (_handle != GASNET_INVALID_HANDLE)                                  \
        gasnete_waitwhile(gasnete_try_syncnb(_handle) == GASNET_ERR_NOT_READY); \
    } while(0)
#endif

GASNET_INLINE_MODIFIER(gasnet_wait_syncnb)
void gasnet_wait_syncnb(gasnet_handle_t handle) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_syncnb(handle);
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNB);
}

#if GASNETI_DIRECT_WAIT_SYNCNB_SOME
  extern void gasnete_wait_syncnb_some(gasnet_handle_t *phandle, size_t numhandles);
#else
  #define gasnete_wait_syncnb_some(phandle, numhandles)                                      \
    gasnete_waitwhile(gasnete_try_syncnb_some(phandle, numhandles) == GASNET_ERR_NOT_READY)
#endif

GASNET_INLINE_MODIFIER(gasnet_wait_syncnb_some)
void gasnet_wait_syncnb_some(gasnet_handle_t *phandle, size_t numhandles) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_syncnb_some(phandle, numhandles);
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNB_SOME);
}

#if GASNETI_DIRECT_WAIT_SYNCNB_ALL
  extern void gasnete_wait_syncnb_all(gasnet_handle_t *phandle, size_t numhandles);
#else
  #define gasnete_wait_syncnb_all(phandle, numhandles)                                      \
    gasnete_waitwhile(gasnete_try_syncnb_all(phandle, numhandles) == GASNET_ERR_NOT_READY)
#endif

GASNET_INLINE_MODIFIER(gasnet_wait_syncnb_all)
void gasnet_wait_syncnb_all(gasnet_handle_t *phandle, size_t numhandles) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_syncnb_all(phandle, numhandles);
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNB_ALL);
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
*/
/* put_nbi       source memory is safe to modify on return
   put_nbi_bulk  source memory is NOT safe to modify on return
 */
extern void gasnete_put_nbi      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG);
extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG);
extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);
extern void gasnete_memset_nbi   (gasnet_node_t node, void *dest, int val,   size_t nbytes GASNETE_THREAD_FARG);

#if GASNETI_DIRECT_GET_NBI
  extern void gasnete_get_nbi (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_get_nbi gasnete_get_nbi_bulk
#endif

GASNET_INLINE_MODIFIER(_gasnet_get_nbi)
void _gasnet_get_nbi      (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(GET_NBI_LOCAL,V);
  gasnete_boundscheck(node, src, nbytes);
  gasnete_aligncheck(src, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_GET(GET_NBI_LOCAL,dest,node,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
  } else {
    GASNETI_TRACE_GET(GET_NBI,dest,node,src,nbytes);
    gasnete_get_nbi(dest, node, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_get_nbi(dest,node,src,nbytes) \
       _gasnet_get_nbi(dest,node,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_put_nbi)
void _gasnet_put_nbi      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(PUT_NBI_LOCAL,V);
  gasnete_boundscheck(node, dest, nbytes);
  gasnete_aligncheck(src, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_NBI_LOCAL,node,dest,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_PUT(PUT_NBI,node,dest,src,nbytes);
    gasnete_put_nbi(node, dest, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put_nbi(node,dest,src,nbytes) \
       _gasnet_put_nbi(node,dest,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_get_nbi_bulk)
void _gasnet_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(GET_NBI_BULK_LOCAL,V);
  gasnete_boundscheck(node, src, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_GET(GET_NBI_BULK_LOCAL,dest,node,src,nbytes);
    GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes);
  } else {
    GASNETI_TRACE_GET(GET_NBI_BULK,dest,node,src,nbytes);
    gasnete_get_nbi_bulk(dest, node, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_get_nbi_bulk(dest,node,src,nbytes) \
       _gasnet_get_nbi_bulk(dest,node,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_put_nbi_bulk)
void _gasnet_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(PUT_NBI_BULK_LOCAL,V);
  gasnete_boundscheck(node, dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_NBI_BULK_LOCAL,node,dest,src,nbytes);
    GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_PUT(PUT_NBI_BULK,node,dest,src,nbytes);
    gasnete_put_nbi_bulk(node, dest, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put_nbi_bulk(node,dest,src,nbytes) \
       _gasnet_put_nbi_bulk(node,dest,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_memset_nbi)
void   _gasnet_memset_nbi   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_MEMSET(MEMSET_NBI_LOCAL,V);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_MEMSET(MEMSET_NBI_LOCAL,node,dest,val,nbytes);
    memset(dest, val, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_MEMSET(MEMSET_NBI,node,dest,val,nbytes);
    gasnete_memset_nbi(node, dest, val, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_memset_nbi(node,dest,val,nbytes) \
       _gasnet_memset_nbi(node,dest,val,nbytes GASNETE_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE);
extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE);

GASNET_INLINE_MODIFIER(_gasnet_try_syncnbi_gets)
int _gasnet_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  int retval;
  gasnet_AMPoll();
  retval = gasnete_try_syncnbi_gets(GASNETE_THREAD_PASS_ALONE);
  GASNETI_TRACE_TRYSYNC(TRY_SYNCNBI_GETS,retval);
  return retval;
}
#define gasnet_try_syncnbi_gets()   \
       _gasnet_try_syncnbi_gets(GASNETE_THREAD_GET_ALONE)

GASNET_INLINE_MODIFIER(_gasnet_try_syncnbi_puts)
int _gasnet_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  int retval;
  gasnet_AMPoll();
  retval = gasnete_try_syncnbi_puts(GASNETE_THREAD_PASS_ALONE);
  GASNETI_TRACE_TRYSYNC(TRY_SYNCNBI_PUTS,retval);
  return retval;
}
#define gasnet_try_syncnbi_puts()   \
       _gasnet_try_syncnbi_puts(GASNETE_THREAD_GET_ALONE)

#if GASNETI_DIRECT_TRY_SYNCNBI_ALL
  extern int gasnete_try_syncnbi_all(GASNETE_THREAD_FARG_ALONE);
#else
  #define gasnete_try_syncnbi_all                                               \
   (gasnete_try_syncnbi_gets(GASNETE_THREAD_PASS_ALONE) == GASNET_OK ?          \
    gasnete_try_syncnbi_puts(GASNETE_THREAD_PASS_ALONE) : GASNET_ERR_NOT_READY) \
    GASNETE_THREAD_SWALLOW
#endif

GASNET_INLINE_MODIFIER(_gasnet_try_syncnbi_all)
int _gasnet_try_syncnbi_all(GASNETE_THREAD_FARG_ALONE) {
  int retval;
  gasnet_AMPoll();
  retval = gasnete_try_syncnbi_all(GASNETE_THREAD_PASS_ALONE);
  GASNETI_TRACE_TRYSYNC(TRY_SYNCNBI_ALL,retval);
  return retval;
}
#define gasnet_try_syncnbi_all()   \
       _gasnet_try_syncnbi_all(GASNETE_THREAD_GET_ALONE)

#if GASNETI_DIRECT_WAIT_SYNCNBI_GETS
  extern void gasnete_wait_syncnbi_gets(GASNETE_THREAD_FARG_ALONE);
#else
  #define gasnete_wait_syncnbi_gets \
    gasnete_pollwhile(gasnete_try_syncnbi_gets(GASNETE_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY) \
    GASNETE_THREAD_SWALLOW
#endif

#define gasnet_wait_syncnbi_gets() do {                                                          \
  GASNETI_TRACE_WAITSYNC_BEGIN();                                                                \
  gasnet_AMPoll(); /* ensure at least one poll */                                                \
  gasnete_wait_syncnbi_gets(GASNETE_THREAD_GET_ALONE);                                           \
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNBI_GETS);                                                 \
  } while (0)

#if GASNETI_DIRECT_WAIT_SYNCNBI_PUTS
  extern void gasnete_wait_syncnbi_puts(GASNETE_THREAD_FARG_ALONE);
#else
  #define gasnete_wait_syncnbi_puts \
    gasnete_pollwhile(gasnete_try_syncnbi_puts(GASNETE_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY) \
    GASNETE_THREAD_SWALLOW
#endif

#define gasnet_wait_syncnbi_puts() do {                                                          \
  GASNETI_TRACE_WAITSYNC_BEGIN();                                                                \
  gasnet_AMPoll(); /* ensure at least one poll */                                                \
  gasnete_wait_syncnbi_puts(GASNETE_THREAD_GET_ALONE);                                           \
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNBI_PUTS);                                                 \
  } while (0)

#if GASNETI_DIRECT_WAIT_SYNCNBI_ALL
  extern void gasnete_wait_syncnbi_all(GASNETE_THREAD_FARG_ALONE);
#else
  #define gasnete_wait_syncnbi_all do {                                                     \
    gasnete_pollwhile(gasnete_try_syncnbi_gets(GASNETE_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY); \
    gasnete_pollwhile(gasnete_try_syncnbi_puts(GASNETE_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY); \
  } while (0) GASNETE_THREAD_SWALLOW
#endif

#define gasnet_wait_syncnbi_all() do {                                                           \
  GASNETI_TRACE_WAITSYNC_BEGIN();                                                                \
  gasnet_AMPoll(); /* ensure at least one poll */                                                \
  gasnete_wait_syncnbi_all(GASNETE_THREAD_GET_ALONE);                                            \
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNBI_ALL);                                                  \
  } while (0)
        
/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
extern void            gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG);
extern gasnet_handle_t gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE);

#define gasnet_begin_nbi_accessregion() gasnete_begin_nbi_accessregion(0 GASNETE_THREAD_GET)
#define gasnet_end_nbi_accessregion()   gasnete_end_nbi_accessregion(GASNETE_THREAD_GET_ALONE)

/* ------------------------------------------------------------------------------------ */
/*
  Blocking memory-to-memory transfers
  ===================================
*/

#if GASNETI_DIRECT_GET
  extern void gasnete_get (void *dest, gasnet_node_t node, void *src,
                           size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_get gasnete_get_bulk
#endif

#if GASNETI_DIRECT_GET_BULK
  extern void gasnete_get_bulk (void *dest, gasnet_node_t node, void *src,
                                size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_get_bulk(dest, node, src, nbytesTI) \
    gasnete_wait_syncnb(gasnete_get_nb_bulk(dest, node, src, nbytesTI))
#endif

#if GASNETI_DIRECT_PUT
  extern void gasnete_put (gasnet_node_t node, void* dest, void *src,
                           size_t nbytes GASNETE_THREAD_FARG);
  #define gasnete_putTI gasnete_put
#else
  /* only valid because this is blocking put */
  #define gasnete_put   gasnete_put_bulk
  #ifdef GASNETI_THREADINFO_OPT
    #define gasnete_putTI(node,dest,src,nbytes,ti) gasnete_put_bulk(node,dest,src,nbytes GASNETE_THREAD_PASS)
  #else
    #define gasnete_putTI gasnete_put_bulk
  #endif
#endif

#if GASNETI_DIRECT_PUT_BULK
  extern void gasnete_put_bulk (gasnet_node_t node, void* dest, void *src,
                                size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_put_bulk(node, dest, src, nbytesTI) \
    gasnete_wait_syncnb(gasnete_put_nb_bulk(node, dest, src, nbytesTI))
#endif

#if GASNETI_DIRECT_MEMSET
  extern void gasnete_memset(gasnet_node_t node, void *dest, int val,
			     size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_memset(node, dest, val, nbytesTI) \
    gasnete_wait_syncnb(gasnete_memset_nb(node, dest, val, nbytesTI))
#endif

GASNET_INLINE_MODIFIER(_gasnet_get)
void _gasnet_get (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(GET_LOCAL,V);
  gasnete_boundscheck(node, src, nbytes);
  gasnete_aligncheck(src, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_GET(GET_LOCAL,dest,node,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
  } else {
    GASNETI_TRACE_GET(GET,dest,node,src,nbytes);
    gasnete_get(dest, node, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_get(dest,node,src,nbytes) \
       _gasnet_get(dest,node,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_get_bulk)
void _gasnet_get_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(GET_BULK_LOCAL,V);
  gasnete_boundscheck(node, src, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_GET(GET_BULK_LOCAL,dest,node,src,nbytes);
    GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes);
  } else {
    GASNETI_TRACE_GET(GET_BULK,dest,node,src,nbytes);
    gasnete_get_bulk(dest, node, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_get_bulk(dest,node,src,nbytes) \
       _gasnet_get_bulk(dest,node,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_put)
void _gasnet_put (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(PUT_LOCAL,V);
  gasnete_boundscheck(node, dest, nbytes);
  gasnete_aligncheck(src, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_LOCAL,node,dest,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_PUT(PUT,node,dest,src,nbytes);
    gasnete_put(node, dest, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put(node,dest,src,nbytes) \
       _gasnet_put(node,dest,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_put_bulk)
void _gasnet_put_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(PUT_BULK_LOCAL,V);
  gasnete_boundscheck(node, dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_BULK_LOCAL,node,dest,src,nbytes);
    GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_PUT(PUT_BULK,node,dest,src,nbytes);
    gasnete_put_bulk(node, dest, src, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put_bulk(node,dest,src,nbytes) \
       _gasnet_put_bulk(node,dest,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(_gasnet_memset)
void  _gasnet_memset (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_MEMSET(MEMSET_LOCAL,V);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_MEMSET(MEMSET_LOCAL,node,dest,val,nbytes);
    memset(dest, val, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_MEMSET(MEMSET,node,dest,val,nbytes);
    gasnete_memset(node, dest, val, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_memset(node,dest,val,nbytes) \
       _gasnet_memset(node,dest,val,nbytes GASNETE_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Value Put
  =========
*/

#if GASNETI_DIRECT_PUT_VAL
  extern void gasnete_put_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_put_val(node, dest, value, nbytesTI) do {                    \
    gasnet_register_value_t src = value;                                       \
    gasnete_putTI(node, dest, GASNETE_TISTARTOFBITS(&src,nbytesTI), nbytesTI); \
  } while (0)
#endif

GASNET_INLINE_MODIFIER(_gasnet_put_val)
void _gasnet_put_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG) {
  assert(nbytes > 0 && nbytes <= sizeof(gasnet_register_value_t));
  gasnete_boundscheck(node, dest, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_VAL_LOCAL,node,dest,&value,nbytes);
    GASNETE_VALUE_ASSIGN(dest, value, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_PUT(PUT_VAL,node,dest,GASNETE_STARTOFBITS(&value,nbytes),nbytes);
    gasnete_put_val(node, dest, value, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put_val(node,dest,value,nbytes) \
       _gasnet_put_val(node,dest,value,nbytes GASNETE_THREAD_GET)

#if GASNETI_DIRECT_PUT_NB_VAL
  extern gasnet_handle_t gasnete_put_nb_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG);
#endif

GASNET_INLINE_MODIFIER(_gasnet_put_nb_val)
gasnet_handle_t _gasnet_put_nb_val (gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG) {
  assert(nbytes > 0 && nbytes <= sizeof(gasnet_register_value_t));
  gasnete_boundscheck(node, dest, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_NB_VAL_LOCAL,node,dest,&value,nbytes);
    GASNETE_VALUE_ASSIGN(dest, value, nbytes);
    gasneti_memsync();
    return GASNET_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_PUT(PUT_NB_VAL,node,dest,GASNETE_STARTOFBITS(&value,nbytes),nbytes);
    #if GASNETI_DIRECT_PUT_NB_VAL
      return gasnete_put_nb_val(node, dest, value, nbytes GASNETE_THREAD_PASS);
    #else
      { gasnet_register_value_t src = value;
        return gasnete_put_nb(node, dest, GASNETE_STARTOFBITS(&src,nbytes), nbytes GASNETE_THREAD_PASS);
      }
    #endif
  }
}
#define gasnet_put_nb_val(node,dest,value,nbytes) \
       _gasnet_put_nb_val(node,dest,value,nbytes GASNETE_THREAD_GET)

#if GASNETI_DIRECT_PUT_NBI_VAL
  extern void gasnete_put_nbi_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG);
#else
  #define gasnete_put_nbi_val(node, dest, value, nbytesTI) do {                  \
    gasnet_register_value_t src = value;                                         \
    gasnete_put_nbi(node, dest, GASNETE_TISTARTOFBITS(&src,nbytesTI), nbytesTI); \
  } while (0)
#endif

GASNET_INLINE_MODIFIER(_gasnet_put_nbi_val)
void _gasnet_put_nbi_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG) {
  assert(nbytes > 0 && nbytes <= sizeof(gasnet_register_value_t));
  gasnete_boundscheck(node, dest, nbytes);
  gasnete_aligncheck(dest, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_PUT(PUT_NBI_VAL_LOCAL,node,dest,&value,nbytes);
    GASNETE_VALUE_ASSIGN(dest, value, nbytes);
    gasneti_memsync();
  } else {
    GASNETI_TRACE_PUT(PUT_NBI_VAL,node,dest,GASNETE_STARTOFBITS(&value,nbytes),nbytes);
    gasnete_put_nbi_val(node, dest, value, nbytes GASNETE_THREAD_PASS);
  }
}
#define gasnet_put_nbi_val(node,dest,value,nbytes) \
       _gasnet_put_nbi_val(node,dest,value,nbytes GASNETE_THREAD_GET)
/* ------------------------------------------------------------------------------------ */
/*
  Blocking Value Get
  ==================
*/

#if GASNETI_DIRECT_GET_VAL
  extern gasnet_register_value_t gasnete_get_val (gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);
#endif

GASNET_INLINE_MODIFIER(_gasnet_get_val)
gasnet_register_value_t _gasnet_get_val (gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  assert(nbytes > 0 && nbytes <= sizeof(gasnet_register_value_t));
  gasnete_boundscheck(node, src, nbytes);
  gasnete_aligncheck(src, nbytes);
  if (gasnete_islocal(node)) {
    GASNETI_TRACE_GET(GET_VAL_LOCAL,NULL,node,src,nbytes);
    switch (nbytes) {
      case sizeof(uint8_t):  return (gasnet_register_value_t)*((uint8_t  *)(src)); 
     OMIT_WHEN_MISSING_16BIT(
      case sizeof(uint16_t): return (gasnet_register_value_t)*((uint16_t *)(src)); 
     )
      case sizeof(uint32_t): return (gasnet_register_value_t)*((uint32_t *)(src)); 
      case sizeof(uint64_t): return (gasnet_register_value_t)*((uint64_t *)(src)); 
      default: { /* no such native nbytes integral type */               
        gasnet_register_value_t result = 0;                                  
        memcpy(GASNETE_STARTOFBITS(&result,nbytes), src, nbytes);          
        return result;
      }                                                                  
    }
  }
  else {
    GASNETI_TRACE_GET(GET_VAL,NULL,node,src,nbytes);
    #if GASNETI_DIRECT_GET_VAL
      return gasnete_get_val(node, src, nbytes GASNETE_THREAD_PASS);
    #else
      { gasnet_register_value_t val = 0;
        gasnete_get(GASNETE_STARTOFBITS(&val,nbytes), node, src, nbytes GASNETE_THREAD_PASS);
        return val;
      }
    #endif
  }
}
#define gasnet_get_val(node,src,nbytes) \
       _gasnet_get_val(node,src,nbytes GASNETE_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Non-Blocking Value Get (explicit-handle)
  ========================================
*/

struct _gasnet_valget_op_t;
typedef struct _gasnet_valget_op_t *gasnet_valget_handle_t;

extern gasnet_valget_handle_t gasnete_get_nb_val(gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);
extern gasnet_register_value_t gasnete_wait_syncnb_valget(gasnet_valget_handle_t handle);

GASNET_INLINE_MODIFIER(_gasnet_get_nb_val)
gasnet_valget_handle_t _gasnet_get_nb_val (gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_TRACE_GET(GET_NB_VAL,NULL,node,src,nbytes);     
  return gasnete_get_nb_val(node,src,nbytes GASNETE_THREAD_PASS); 
}
#define gasnet_get_nb_val(node,src,nbytes) \
       _gasnet_get_nb_val(node,src,nbytes GASNETE_THREAD_GET)

GASNET_INLINE_MODIFIER(gasnet_wait_syncnb_valget)
gasnet_register_value_t gasnet_wait_syncnb_valget (gasnet_valget_handle_t handle) {
  gasnet_register_value_t val;
  GASNETI_TRACE_WAITSYNC_BEGIN();
  val = gasnete_wait_syncnb_valget(handle); 
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNB_VALGET);   
  return val;
}

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
*/

extern void gasnete_barrier_notify(int id, int flags);
extern int gasnete_barrier_wait(int id, int flags);
extern int gasnete_barrier_try(int id, int flags);

#define gasnet_barrier_notify  gasnete_barrier_notify
#define gasnet_barrier_wait    gasnete_barrier_wait
#define gasnet_barrier_try     gasnete_barrier_try

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif
