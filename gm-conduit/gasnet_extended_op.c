/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/Attic/gasnet_extended_op.c,v $
 * $Date: 2013/06/30 22:54:20 $
 * $Revision: 1.29 $
 * Description: GASNet Extended API OPs interface
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_tools.h>

/*
  Op management
  =============
*/
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }
/*  get a new op and mark it in flight */
gasnete_eop_t *
gasnete_eop_new(gasnete_threaddata_t * const thread)
{
	gasnete_eopaddr_t head = thread->eop_free;
	if_pt (!gasnete_eopaddr_isnil(head)) {
		gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
		thread->eop_free = eop->addr;
		eop->addr = head;
		gasneti_assert(!gasnete_eopaddr_equal(thread->eop_free,head));
		gasneti_assert(eop->threadidx == thread->threadidx);
		gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
		gasneti_assert(OPSTATE(eop) == OPSTATE_FREE);
		SET_OPSTATE(eop, OPSTATE_INFLIGHT);
		return eop;
	} 
	else { /*  free list empty - need more eops */
		int bufidx = thread->eop_num_bufs;
		gasnete_eop_t *buf;
		int i;

		gasnete_threadidx_t threadidx = thread->threadidx;
		if (bufidx == 256) 
			gasneti_fatalerror("GASNet Extended API: Ran out "
			    "of explicit handles (limit=65535)");
		thread->eop_num_bufs++;
		buf = (gasnete_eop_t *)
		gasneti_calloc(256, sizeof(gasnete_eop_t));
		gasneti_leak(buf);
		for (i=0; i < 256; i++) {
			gasnete_eopaddr_t addr;
			addr.bufferidx = bufidx;
			#if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
				#ifdef GASNETE_EOP_MOD
				addr.eopidx = (i+32) % 255;
				#else
					{ int k = i+32;
					addr.eopidx = k > 255 ? k - 255 : k;
					}
				#endif
			#else
				addr.eopidx = i+1;
			#endif
			buf[i].threadidx = threadidx;
			buf[i].addr = addr;
		}
		/*  add a list terminator */
		#if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
			#ifdef GASNETE_EOP_MOD
				buf[223].addr.eopidx = 255; 
				/* modular arithmetic messes up this one */
			#endif
			buf[255].addr = EOPADDR_NIL;
		#else
			buf[255].addr = EOPADDR_NIL;
		#endif
		thread->eop_bufs[bufidx] = buf;
		head.bufferidx = bufidx;
		head.eopidx = 0;
		thread->eop_free = head;

		#if GASNET_DEBUG
		{ /* verify new free list got built correctly */
			int i;
			int seen[256];
			gasnete_eopaddr_t addr = thread->eop_free;

                        gasneti_memcheck(thread->eop_bufs[bufidx]);
			memset(seen, 0, 256*sizeof(int));
			for (i=0;i<(bufidx==255?255:256);i++) {
				gasnete_eop_t *eop; 
				gasneti_assert(!gasnete_eopaddr_isnil(addr));
				eop = GASNETE_EOPADDR_TO_PTR(thread,addr);
				gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
				gasneti_assert(OPSTATE(eop) == OPSTATE_FREE);
				gasneti_assert(eop->threadidx == threadidx);
				gasneti_assert(eop->iop == NULL);
				gasneti_assert(addr.bufferidx == bufidx);
				/* see if we hit a cycle */
				gasneti_assert(!seen[addr.eopidx]);
				seen[addr.eopidx] = 1;
				addr = eop->addr;
			}
			gasneti_assert(gasnete_eopaddr_isnil(addr)); 
		}
		#endif
		/*  should succeed this time */
		return gasnete_eop_new(thread);
	}
}

gasnete_iop_t *
gasnete_iop_new(gasnete_threaddata_t * const thread)
{
	gasnete_iop_t *iop;

	if_pt (thread->iop_free) {
		iop = thread->iop_free;
		thread->iop_free = iop->next;
                gasneti_memcheck(iop);
		gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
		gasneti_assert(iop->threadidx == thread->threadidx);
		/* If using trace or stats, want meaningful counts when tracing NBI access regions */
		#if GASNETI_STATS_OR_TRACE
			iop->initiated_get_cnt = 0;
			iop->initiated_put_cnt = 0;
			gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
			gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
		#endif
	} else {
		iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
		gasneti_leak(iop);
		SET_OPTYPE((gasnete_op_t *)iop, OPTYPE_IMPLICIT);
		iop->threadidx = thread->threadidx;
		iop->initiated_get_cnt = 0;
		iop->initiated_put_cnt = 0;
		gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
		gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
	}
	iop->next = NULL;
        gasnete_iop_check(iop);
	return iop;
}

/*  query an eop for completeness */
int 
gasnete_eop_isdone(gasnete_eop_t *eop) 
{
	gasneti_assert(eop->threadidx == gasnete_mythread()->threadidx);
	gasnete_eop_check(eop);
	return GASNETE_EOP_DONE(eop);
}

/*  query an iop for completeness - this means both puts and gets */
int 
gasnete_iop_isdone(gasnete_iop_t *iop) 
{
	gasneti_assert(iop->threadidx == gasnete_mythread()->threadidx);
	gasnete_iop_check(iop);
	return (GASNETE_IOP_CNTDONE(iop,get) && GASNETE_IOP_CNTDONE(iop,put));
}

/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
	if (OPTYPE(op) == OPTYPE_EXPLICIT) {
		gasnete_eop_t *eop = (gasnete_eop_t *)op;
                gasnete_eop_check(eop);
		GASNETE_EOP_MARKDONE(eop);
	} else {
		gasnete_iop_t *iop = (gasnete_iop_t *)op;
                gasnete_iop_check(iop);
		if (isget) 
			gasneti_weakatomic_increment(&(iop->completed_get_cnt), 0);
		else 
			gasneti_weakatomic_increment(&(iop->completed_put_cnt), 0);
	}
}

/*  free an eop */
void gasnete_eop_free(gasnete_eop_t *eop) {
	gasnete_threaddata_t * const thread = gasnete_threadtable[eop->threadidx];
	gasnete_eopaddr_t addr = eop->addr;
        /* DOB: freelist is not lock-protected, hence gasnete_eop_free may
           ONLY be called from the owning thread!!! */
        gasneti_assert(thread == gasnete_mythread());
	gasnete_eop_check(eop);
	gasneti_assert(GASNETE_EOP_DONE(eop));
#if GASNET_DEBUG
	SET_OPSTATE(eop, OPSTATE_FREE);
#endif
	eop->addr = thread->eop_free;
	thread->eop_free = addr;
}

/*  free an iop */
void gasnete_iop_free(gasnete_iop_t *iop) {
	gasnete_threaddata_t * const thread = gasnete_threadtable[iop->threadidx];
        /* DOB: freelist is not lock-protected, hence gasnete_iop_free may
           ONLY be called from the owning thread!!! */
        gasneti_assert(thread == gasnete_mythread());
	gasnete_iop_check(iop);
	gasneti_assert(GASNETE_IOP_CNTDONE(iop,get));
	gasneti_assert(GASNETE_IOP_CNTDONE(iop,put));
	gasneti_assert(iop->next == NULL);
	iop->next = thread->iop_free;
	thread->iop_free = iop;
}
/* ------------------------------------------------------------------------------------ */
/* GASNET-Internal OP Interface */
gasneti_eop_t *gasneti_eop_create(GASNETE_THREAD_FARG_ALONE) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
  return (gasneti_eop_t *)op;
}
gasneti_iop_t *gasneti_iop_register(unsigned int noperations, int isget GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  gasnete_iop_check(op);
  if (isget) op->initiated_get_cnt += noperations;
  else       op->initiated_put_cnt += noperations;
  gasnete_iop_check(op);
  return (gasneti_iop_t *)op;
}
void gasneti_eop_markdone(gasneti_eop_t *eop) {
  gasnete_eop_t *op = (gasnete_eop_t *)eop;
  gasnete_eop_check(op);
  GASNETE_EOP_MARKDONE(op);
}
void gasneti_iop_markdone(gasneti_iop_t *iop, unsigned int noperations, int isget) {
  gasnete_iop_t *op = (gasnete_iop_t *)iop;
  gasneti_weakatomic_t * const pctr = (isget ? &(op->completed_get_cnt) : &(op->completed_put_cnt));
  gasnete_iop_check(op);
  if (gasneti_constant_p(noperations) && (noperations == 1))
      gasneti_weakatomic_increment(pctr, 0);
  else {
    #if defined(GASNETI_HAVE_WEAKATOMIC_ADD_SUB)
      gasneti_weakatomic_add(pctr, noperations, 0);
    #else /* yuk */
      while (noperations) {
        gasneti_weakatomic_increment(pctr, 0);
        noperations--;
      }
    #endif
  }
  gasnete_iop_check(op);
}
