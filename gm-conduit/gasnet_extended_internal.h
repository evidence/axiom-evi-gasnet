/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_internal.h         $
 *     $Date: 2002/08/11 22:02:31 $
 * $Revision: 1.1 $
 * Description: GASNet header for internal definitions in Extended API
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _GASNET_EXTENDED_INTERNAL_H
#define _GASNET_EXTENDED_INTERNAL_H

#include <gasnet.h>
#include <gasnet_handler.h>
#include <gasnet_internal.h>

/* Firehose needs */
/*
 * explicit ops should contain. .
 * - node, dest, src, len, 
 *
 * transient ops should contain. .
 * - src, dest, len, peer_op, 
 *
 * implicit ops should remain as is in order to use the extended reference.
 *
 */
/* ------------------------------------------------------------------------------------ */
/*  reasonable upper-bound on L2 cache line size (don't make this too big) */
#define GASNETE_CACHE_LINE_BYTES  (128)

typedef uint8_t gasnete_threadidx_t;

/* gasnet_handle_t is a void* pointer to a gasnete_op_t, 
   which is either a gasnete_eop_t or an gasnete_iop_t
   */
typedef struct _gasnete_op_t {
	uint8_t			flags;		/*  flags - type tag */
	gasnete_threadidx_t	threadidx;	/*  thread that owns me */
} gasnete_op_t;

/* for compactness, eops address each other in the free list using a gasnete_eopaddr_t */ 
typedef struct _gasnete_eopaddr_t {
  uint8_t bufferidx;
  uint8_t eopidx;
} gasnete_eopaddr_t;

#define gasnete_eopaddr_equal(addr1,addr2) (*(uint16_t*)&(addr1) == *(uint16_t*)&(addr2))
#define gasnete_eopaddr_isnil(addr) (*(uint16_t*)&(addr) == *(uint16_t*)&(EOPADDR_NIL))

/* -------------------------------------------------------------------------- */
/* Explicit ops, used when operations are known to require only one send in
 * order to complete */
typedef struct _gasnete_eop_t {
	uint8_t		flags;	/*  state flags */

	gasnete_threadidx_t	threadidx;  /*  thread that owns me */
	gasnet_node_t		node;
	uintptr_t		dest;
	uintptr_t		src;
	uint32_t		len;
	uintptr_t		peer_op;	/* only used in transient ops */

	gasnete_eopaddr_t	addr;      /*  next cell while in free list, 
					       my own eopaddr_t while in use */
} gasnete_eop_t;

/* -------------------------------------------------------------------------- */
/* Transient ops, used by the target of an operation as a transient operation
 * in order to satisfy requests for sends out of a Reply handler
 */
typedef gasnete_eop_t gasnete_top_t;

/* -------------------------------------------------------------------------- */
/* Implicit ops, used by the extended reference implementation */
typedef struct _gasnete_iop_t {
  	uint8_t		flags;	/*  state flags */

	gasnete_threadidx_t	threadidx;  /*  thread that owns me */
	gasnet_node_t		node;
  	uintptr_t		dest;		/* remote RDMA addr */
	uintptr_t		src;		/* source addr */
	uint32_t		len;		/* length */

	int	initiated_get_cnt;     /*  count of get ops initiated */
	int	initiated_put_cnt;     /*  count of put ops initiated */

	struct _gasnete_iop_t *next;    /*  next cell while in free list, deferred 
				            iop while being filled */

	/*  make sure the counters live on different cache lines for SMP's */
	uint8_t pad[GASNETE_CACHE_LINE_BYTES - sizeof(void*) - sizeof(int)]; 

	gasneti_atomic_t completed_get_cnt;     /*  count of get ops completed */
	gasneti_atomic_t completed_put_cnt;     /*  count of put ops completed */
} gasnete_iop_t;

/* -------------------------------------------------------------------------- */
typedef struct _gasnete_threaddata_t {
	/* pointer reserved for use by the core */
	void			*gasnetc_threaddata;
	gasnete_threadidx_t	threadidx;

	int eop_num_bufs;	/*  number of valid buffer entries */
	gasnete_eop_t		*eop_bufs[256]; /*  buffers of eops */
	gasnete_eopaddr_t	eop_free;   /*  free list of eops */

	/*  stack of iops - head is active iop servicing new implicit ops */
	gasnete_iop_t *current_iop;  
	gasnete_iop_t *iop_free;      /*  free list of iops */
} gasnete_threaddata_t;
/* -------------------------------------------------------------------------- */

/* gasnete_op_t flags field */
#define OPTYPE_EXPLICIT		0x00  /* gasnete_eop_new() relies on this value */
#define OPTYPE_CORE		0x80
#define OPTYPE(op)		((op)->flags & 0x80)
GASNET_INLINE_MODIFIER(SET_OPTYPE)
void SET_OPTYPE(gasnete_op_t *op, uint8_t type) {
	op->flags = (op->flags & 0x7F) | (type & 0x80);
	assert(OPTYPE(op) == type);
}

/*  state */
#define OPSTATE_FREE		0   /* gasnete_eop_new() relies on this value */
#define OPSTATE_INFLIGHT	1
#define OPSTATE_COMPLETE	3
#define OPSTATE(op)		((op)->flags & 0x03) 
GASNET_INLINE_MODIFIER(SET_OPSTATE)
void SET_OPSTATE(gasnete_eop_t *op, uint8_t state) {
	op->flags = (op->flags & 0xFC) | (state & 0x03);
	assert(OPSTATE(op) == state);
}

/* New op creation, gets new op and marks it in flight */
gasnete_eop_t	*gasnete_eop_new(gasnete_threaddata_t *thread);
gasnete_iop_t	*gasnete_iop_new(gasnete_threaddata_t *thread);

/*  query an eop for completeness */
int		gasnete_op_isdone(gasnete_op_t *op);

/*  mark an op done - isget ignored for explicit ops */
void		gasnete_op_markdone(gasnete_op_t *op, int isget);
void		gasnete_op_free(gasnete_op_t *op);

#define GASNETE_EOPADDR_TO_PTR(threaddata, eopaddr)            \
      (assert(threaddata),                                     \
       assert((eopaddr).bufferidx<(threaddata)->eop_num_bufs), \
       assert(!gasnete_eopaddr_isnil(eopaddr)),                \
       (threaddata)->eop_bufs[(eopaddr).bufferidx] + (eopaddr).eopidx)

/* 1 = scatter newly allocated eops across cache lines to 
 *     reduce false sharing */
#define GASNETE_SCATTER_EOPS_ACROSS_CACHELINES    1 

/* -------------------------------------------------------------------------- */

/* make a GASNet call - if it fails, print error message and abort */
#define GASNETE_SAFE(fncall) do {                                           \
   int retcode = (fncall);                                                  \
   if_pf (retcode != GASNET_OK) {                                           \
     gasneti_fatalerror("\nGASNet encountered an error: %s(%i)\n"           \
        "  while calling: %s\n"                                             \
        "  at %s",                                                          \
        gasneti_ErrorName(retcode), retcode, #fncall, gasneti_current_loc); \
   }                                                                        \
 } while (0)

#define GASNETE_HANDLER_BASE  100 /* reserve 100-199 for the extended API */
#define _hidx_gasnete_extref_barrier_notify_reqh	(GASNETE_HANDLER_BASE+0) 
#define _hidx_gasnete_extref_barrier_done_reqh		(GASNETE_HANDLER_BASE+1)
#define _hidx_gasnete_extref_get_reqh			(GASNETE_HANDLER_BASE+2)
#define _hidx_gasnete_extref_get_reph			(GASNETE_HANDLER_BASE+3)
#define _hidx_gasnete_extref_put_reqh			(GASNETE_HANDLER_BASE+4)
#define _hidx_gasnete_extref_putlong_reqh		(GASNETE_HANDLER_BASE+5)
#define _hidx_gasnete_extref_memset_reqh		(GASNETE_HANDLER_BASE+6)
#define _hidx_gasnete_extref_markdone_reph		(GASNETE_HANDLER_BASE+7)
#define _hidx_						(GASNETE_HANDLER_BASE+)
/* add new extended API handlers here and to the bottom of gasnet_extended.c */

#endif
