/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/lapi-conduit/Attic/gasnet_extended_internal.h,v $
 *     $Date: 2005/02/14 12:42:44 $
 * $Revision: 1.12 $
 * Description: GASNet header for internal definitions in Extended API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_EXTENDED_INTERNAL_H
#define _GASNET_EXTENDED_INTERNAL_H

#include <gasnet.h>
#include <gasnet_handler.h>
#include <gasnet_internal.h>



/* =====================================================================
 * LAPI specific structures
 * =====================================================================
 */
#include <lapi.h>
/* no point in re-defining everything */
#include <gasnet_core_internal.h>

extern void** gasnete_remote_memset_hh;
extern void** gasnete_remote_barrier_hh;
extern void* gasnete_lapi_memset_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				    ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo);
extern void* gasnete_lapi_barrier_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				     ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo);

/* LAPI header handler argument for remote memset operation */
typedef struct {
    uintptr_t destLoc;
    int       value;
    size_t    nbytes;
} gasnete_memset_uhdr_t;

/* LAPI header handler argument for barrier operations */
typedef struct {
    int value;
    uint8_t wireflags;
} gasnete_barrier_uhdr_t;

#define GASNETE_WIREFLAGS_FLAGS(wf)    ((wf) & 0x3)
#define GASNETE_WIREFLAGS_PHASE(wf)    (((wf) & 0x4) >> 2)
#define GASNETE_WIREFLAGS_ISNOTIFY(wf) ((wf) & 0x8)

#define GASNETE_WIREFLAGS_SET(flags, phase, isnotify) \
    (  ((flags) & 0x3)       |                        \
      (((phase) & 0x1) << 2) |                        \
      (((isnotify) & 0x1) << 3) )

#define GASNETE_WIREFLAGS_SANITYCHECK() do {                                                    \
    gasneti_assert_always((GASNET_BARRIERFLAG_MISMATCH | GASNET_BARRIERFLAG_ANONYMOUS) == 0x3); \
  } while (0)

/* bypass LAPI for sending loopback AMSend messages in barrier
   saves some LAPI overhead, adds some of our own locking overhead
 */
#ifndef GASNETE_BARRIER_BYPASS_LOOPBACK_AMSEND
#define GASNETE_BARRIER_BYPASS_LOOPBACK_AMSEND 1
#endif

/* implement barrier synchronization using LAPI Gfence
   makes barrier_notify entirely blocking
 */
#ifndef GASNETE_BARRIER_USE_GFENCE
#define GASNETE_BARRIER_USE_GFENCE 1
#endif

/* ------------------------------------------------------------------------------------ */
typedef uint8_t gasnete_threadidx_t;

/* gasnet_handle_t is a void* pointer to a gasnete_op_t, 
   which is either a gasnete_eop_t or an gasnete_iop_t
*/
typedef struct _gasnete_op_t {
    uint8_t flags;                  /*  flags - type tag */
    gasnete_threadidx_t threadidx;  /*  thread that owns me */
} gasnete_op_t;

/* for compactness, eops address each other in the free list using a gasnete_eopaddr_t */ 
typedef union _gasnete_eopaddr_t {
  struct {
    uint8_t _bufferidx;
    uint8_t _eopidx;
  } compaddr;
  uint16_t fulladdr;
} gasnete_eopaddr_t;
#define bufferidx compaddr._bufferidx
#define eopidx compaddr._eopidx

#define gasnete_eopaddr_equal(addr1,addr2) ((addr1).fulladdr == (addr2).fulladdr)
#define gasnete_eopaddr_isnil(addr) ((addr).fulladdr == EOPADDR_NIL.fulladdr)

typedef struct _gasnete_eop_t {
    uint8_t flags;                  /*  state flags */
    gasnete_threadidx_t threadidx;  /*  thread that owns me */
    gasnete_eopaddr_t addr;         /*  next cell while in free list, my own eopaddr_t while in use */
    int          initiated_cnt;
    lapi_cntr_t  cntr;
} gasnete_eop_t;

typedef struct _gasnete_iop_t {
    uint8_t flags;                  /*  state flags */
    gasnete_threadidx_t threadidx;  /*  thread that owns me */
    uint16_t _unused;
    int initiated_get_cnt;     /*  count of get ops initiated */
    int initiated_put_cnt;     /*  count of put ops initiated */

    struct _gasnete_iop_t *next;    /*  next cell while in free list, deferred iop while being filled */

    /*  make sure the counters live on different cache lines for SMP's */
    uint8_t pad[MAX(8,(ssize_t)(GASNETI_CACHE_LINE_BYTES - sizeof(void*) - sizeof(int)))]; 

    lapi_cntr_t      get_cntr;
    lapi_cntr_t      put_cntr;
} gasnete_iop_t;

/* ------------------------------------------------------------------------------------ */
typedef struct _gasnete_threaddata_t {
    void *gasnetc_threaddata;     /* pointer reserved for use by the core */
    void *gasnete_coll_threaddata;/* pointer reserved for use by the collectives */

    gasnete_threadidx_t threadidx;

    gasnete_eop_t *eop_bufs[256]; /*  buffers of eops for memory management */
    int eop_num_bufs;             /*  number of valid buffer entries */
    gasnete_eopaddr_t eop_free;   /*  free list of eops */

    /*  stack of iops - head is active iop servicing new implicit ops */
    gasnete_iop_t *current_iop;  

    gasnete_iop_t *iop_free;      /*  free list of iops */

    struct _gasnet_valget_op_t *valget_free; /* free list of valget cells */
} gasnete_threaddata_t;
/* ------------------------------------------------------------------------------------ */

/* gasnete_op_t flags field */
#define OPTYPE_EXPLICIT               0x00  /*  gasnete_eop_new() relies on this value */
#define OPTYPE_IMPLICIT               0x80
#define OPTYPE(op) ((op)->flags & 0x80)
GASNET_INLINE_MODIFIER(SET_OPTYPE)
    void SET_OPTYPE(gasnete_op_t *op, uint8_t type) {
    op->flags = (op->flags & 0x7F) | (type & 0x80);
}

/*  state - only valid for explicit ops */
#define OPSTATE_FREE      0   /*  gasnete_eop_new() relies on this value */
#define OPSTATE_INFLIGHT  1
#define OPSTATE_COMPLETE  2
#define OPSTATE(op) ((op)->flags & 0x03) 
GASNET_INLINE_MODIFIER(SET_OPSTATE)
    void SET_OPSTATE(gasnete_eop_t *op, uint8_t state) {
    op->flags = (op->flags & 0xFC) | (state & 0x03);
  /* RACE: If we are marking the op COMPLETE, don't assert for completion
   * state as another thread spinning on the op may already have changed
   * the state. */
    gasneti_assert(state == OPSTATE_COMPLETE ? 1 : OPSTATE(op) == state);
}

/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t *thread);
gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t *thread);
/*  query an eop for completeness */
int gasnete_op_isdone(gasnete_op_t *op);
/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget);
/*  free an op */
void gasnete_op_free(gasnete_op_t *op);

#define GASNETE_EOPADDR_TO_PTR(threaddata, eopaddr)                      \
      (gasneti_memcheck(threaddata),                                     \
       gasneti_assert(!gasnete_eopaddr_isnil(eopaddr)),                  \
       gasneti_assert((eopaddr).bufferidx < (threaddata)->eop_num_bufs), \
       gasneti_memcheck((threaddata)->eop_bufs[(eopaddr).bufferidx]),    \
       (threaddata)->eop_bufs[(eopaddr).bufferidx] + (eopaddr).eopidx)

#if GASNET_DEBUG
  /* check an in-flight/complete eop */
  #define gasnete_eop_check(eop) do {                                \
    gasnete_threaddata_t * _th;                                      \
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);                  \
    gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT ||               \
                   OPSTATE(eop) == OPSTATE_COMPLETE);                \
    _th = gasnete_threadtable[(eop)->threadidx];                     \
    gasneti_assert(GASNETE_EOPADDR_TO_PTR(_th, (eop)->addr) == eop); \
  } while (0)
  #define gasnete_iop_check(iop) do {                                             \
    int _temp;                                                                    \
    gasneti_memcheck(iop);                                                        \
    if ((iop)->next != NULL) _gasnete_iop_check((iop)->next);                     \
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);                               \
    gasneti_assert((iop)->threadidx < gasnete_numthreads);                        \
    gasneti_memcheck(gasnete_threadtable[(iop)->threadidx]);                      \
    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&((iop)->get_cntr),&_temp)); \
    gasneti_assert(_temp <= (iop)->initiated_get_cnt);                            \
    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&((iop)->put_cntr),&_temp)); \
    gasneti_assert(_temp <= (iop)->initiated_put_cnt);                            \
  } while (0)
  extern void _gasnete_iop_check(gasnete_iop_t *iop);
#else
  #define gasnete_eop_check(eop)   ((void)0)
  #define gasnete_iop_check(iop)   ((void)0)
#endif

/*  1 = scatter newly allocated eops across cache lines to reduce false sharing */
#define GASNETE_SCATTER_EOPS_ACROSS_CACHELINES    1 

/* ------------------------------------------------------------------------------------ */

/* make a GASNet call - if it fails, print error message and abort */
#define GASNETE_SAFE(fncall) do {                                           \
   int retcode = (fncall);                                                  \
   if_pf (retcode != GASNET_OK) {                                           \
     gasneti_fatalerror("\nGASNet encountered an error: %s(%i)\n"           \
        "  while calling: %s\n"                                             \
        "  at %s",                                                          \
        gasnet_ErrorName(retcode), retcode, #fncall, gasneti_current_loc);  \
   }                                                                        \
 } while (0)

#define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
/* add new extended API handlers here and to the bottom of gasnet_extended.c */

#endif
