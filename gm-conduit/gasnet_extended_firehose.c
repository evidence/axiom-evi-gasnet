#include <gasnet.h>
#ifdef GASNETC_FIREHOSE
#include <gasnet_extended_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>

#ifndef GASNETE_PUT_NON_DMA_CUTOFF
#define GASNETE_PUT_NON_DMA_CUTOFF	gasnet_AMMaxMedium()
#endif
#ifndef GASNETE_GET_NON_DMA_CUTOFF
#define GASNETE_GET_NON_DMA_CUTOFF	gasnet_AMMaxMedium()
#endif

extern uintptr_t	*gasnetc_firehose_buf;
extern size_t		 gasnetc_firehose_buf_num;
extern gasneti_mutex_t	 gasnetc_lock_fh_victim;

extern void	gasnetc_bucket_pin_by_addr(uintptr_t, size_t);
extern void	gasnetc_bucket_unpin_by_addr(uintptr_t, size_t);
extern int	gasnetc_firehose_build_list(gasnet_node_t, uintptr_t, size_t, 
					    size_t *, size_t *);
extern void	gasnetc_firehose_decrement_refcount(gasnet_node_t, uintptr_t, 
						    size_t);
/* ------------------------------------------------------------------------ */
/* FIFO operations */
gasneti_mutex_t	 gasnete_fifo_lock = GASNETI_MUTEX_INITIALIZER;
gasnete_eop_t	*gasnete_fifo_head = NULL;

GASNET_INLINE_MODIFIER(gasnete_fifo_enqueue)
void
gasnete_fifo_enqueue(gasnete_eop_t *eop)
{
	gasneti_mutex_lock(&gasnete_fifo_lock);
	eop->next = gasnete_fifo_head;
	gasnete_fifo_head = eop;
	gasneti_mutex_unlock(&gasnete_fifo_lock);
	return;
}

GASNET_INLINE_MODIFIER(gasnete_fifo_dequeue)
gasnete_eop_t *
gasnete_fifo_dequeue()
{
	gasnete_eop_t *eop;

	gasneti_mutex_lock(&gasnete_fifo_lock);
	eop = gasnete_fifo_head;
	gasnete_fifo_head = gasnete_fifo_head->next;
	gasneti_mutex_unlock(&gasnete_fifo_lock);
	return eop;
}

/* ------------------------------------------------------------------------ */
/* 
 * Extended AM Handlers 
 */
/*
 * AM Handler: Firehose move reply handler, called by the core Firehose request
 *             handler 
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_move_reph_inner)
void
gasnete_firehose_move_reph_inner(gasnet_token_t token, void *context)
{
	gasnete_eop_t	*pop = (gasnete_eop_t *) context;
	gasnete_fifo_enqueue(pop);
}
SHORT_HANDLER(gasnete_firehose_move_reph,1,2,
             (token, UNPACK(a0)     ),
             (token, UNPACK2(a0, a1)));
/*
 * AM Handler: Request to get into a pinned memory location
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_get_dma_reqh_inner)
void
gasnete_firehose_get_dma_reqh_inner(gasnet_token_t token, 
				    gasnet_handlerarg_t nbytes, 
				    void *dest, void *src, void *op)
{
	gasnetc_bucket_pin_by_addr((uintptr_t) src, (size_t) nbytes);
	GASNETE_SAFE(
	    LONGASYNC_REP(1,2, (token,
	    gasneti_handleridx(gasnete_firehose_get_dma_reph), src, nbytes,
	    dest, PACK(op))));
}
SHORT_HANDLER(gasnete_firehose_get_dma_reqh,4,7, 
    (token, a0, UNPACK(a1),     UNPACK(a2),     UNPACK(a3)    ),
    (token, a0, UNPACK2(a1,a2), UNPACK2(a3,a4), UNPACK2(a5, a6)));

/*
 * AM Handler: Reply to get into a pinned memory location
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_get_dma_reph_inner)
void
gasnete_firehose_get_dma_reph_inner(gasnet_token_t token, void *op)
{
	gasnete_eop_t	*pop;

	pop = (gasnete_eop_t *) op;
	assert(pop->src > 0 && pop->len > 0);
	gasnetc_bucket_unpin_by_addr(pop->src,pop->len);
	if (pop->iop != NULL)
		gasneti_atomic_increment(&(pop->iop->completed_get_cnt));
	gasnete_op_markdone((gasnete_op_t *) op, 1);
}
LONG_HANDLER(gasnete_firehose_get_dma_reph,1,2, 
    (token, UNPACK(a0)    ),
    (token, UNPACK2(a0,a1)));

/* ------------------------------------------------------------------------ */
/*
 * Firehose callback functions
 */

void
gasnete_firehose_callback_pop(struct gm_port *p, void *context, 
			      gm_status_t status)
{
	gasnete_eop_t	*pop = (gasnete_eop_t *) context;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(pop != NULL);
	assert(pop->node < gasnete_nodes);
	if_pf (status != GM_SUCCESS)
	    gasnetc_callback_error(status, NULL);
	gasnetc_firehose_decrement_refcount(pop->node, pop->dest, pop->len);
	gasnetc_token_lo_release();
	/* If this was associated to an iop, increment put completed count */
	if (pop->iop != NULL)
		gasneti_atomic_increment(&(pop->iop->completed_put_cnt));
	gasnete_op_markdone((gasnete_op_t *)pop, 1);
}

/* ------------------------------------------------------------------------ */
/*
 * Firehose put helper
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_put_using_directed)
void
gasnete_firehose_put_using_directed(gasnet_node_t node, uintptr_t dest, 
				    void *src, size_t nbytes, 
				    gasnete_eop_t *pop)
{
	assert(pop != NULL);
	assert(src != NULL);
	assert(node < gasnete_nodes);
	assert(nbytes > 0);
	gasnetc_token_lo_poll_lock();
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gm_directed_send_with_callback(
	    _gmc.port, src, (gm_remote_ptr_t) dest,
	    (unsigned long) nbytes, GM_LOW_PRIORITY,
	    _gmc.gm_nodes[node].id, _gmc.gm_nodes[node].port,
	    gasnete_firehose_callback_pop, (void *) pop);
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return;
}

/* ------------------------------------------------------------------------ */
/* 
 * Puts
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_put_bulk)
gasnet_handle_t
gasnete_firehose_put_bulk(gasnet_node_t node, void *dest, void *src, size_t nbytes,
			  gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	size_t		num_buckets, tot_buckets;
	size_t		new_buckets, old_buckets;
	gasnete_eop_t	*pop;

	num_buckets = GASNETI_PAGE_ROUNDUP(nbytes, GASNETC_BUCKET_SIZE) >> 
	    GASNETC_BUCKET_SHIFT;
	tot_buckets = num_buckets*2;
	/* XXX need better runtime support for cases where num firehoses > than
	 * we can support in a single medium
	 */
	assert(sizeof(uintptr_t)*tot_buckets < gasnet_AMMaxMedium());

	/* Pin locally, incrementing reference counts where necessary */
	gasnetc_bucket_pin_by_addr((uintptr_t) src, nbytes);

	gasneti_mutex_lock(&gasnetc_lock_fh_victim);
	/* May have to regrow the firehose buffer */
	if (gasnetc_firehose_buf_num < tot_buckets) {
		if (gasnetc_firehose_buf != NULL)
			free(gasnetc_firehose_buf);
		gasnetc_firehose_buf = (uintptr_t *)
		    gasneti_malloc(sizeof(uintptr_t) * tot_buckets);
	}
	pop = gasnete_eop_new(GASNETE_MYTHREAD);
	pop->src = (uintptr_t) src;
	pop->len = (uint32_t) nbytes;

	if (gasnetc_firehose_build_list(node, (uintptr_t)dest, num_buckets,
	    &old_buckets, &new_buckets)) {
		gasnete_eop_t	*pop;
		pop = gasnete_eop_new(GASNETE_MYTHREAD);
		pop->node = node;
		pop->dest = (uintptr_t) dest;
		pop->iop = iop;
		assert(gasneti_handleridx(gasnete_firehose_move_reph) > 0);
		MEDIUM_REQ(5, 6, 
		   (node, gasneti_handleridx(gasnetc_firehose_move_reqh),
		    (void *) gasnetc_firehose_buf, num_buckets + old_buckets,
		    gasneti_handleridx(gasnete_firehose_move_reph),
		    new_buckets, old_buckets, num_buckets, PACK((void *) pop)));
	}
	else {
		/* all firehoses are remote pinned buckets */
		gasnete_firehose_put_using_directed(node, 
		   (uintptr_t) dest, src, nbytes, pop);
	}
	/* If we were dealing with implicit put, increment the iop */
	if (pop->iop != NULL)
		iop->initiated_put_cnt++;
	gasneti_mutex_unlock(&gasnetc_lock_fh_victim);
	return ((gasnete_op_t *) pop);
}

extern gasnet_handle_t
gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, 
		     size_t nbytes GASNETE_THREAD_FARG)
{
	if (nbytes > GASNETE_PUT_NON_DMA_CUTOFF)
		return gasnete_firehose_put_bulk(node, dest, src, nbytes,
		    NULL GASNETE_THREAD_PASS);
	else 
		return gasnete_extref_put_nb_bulk(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
}

extern void
gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, 
		      size_t nbytes GASNETE_THREAD_FARG)
{
	gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
	gasnete_iop_t *iop = mythread->current_iop;

	if (nbytes > GASNETE_PUT_NON_DMA_CUTOFF) {
		gasnete_firehose_put_bulk(node, dest, src, nbytes,
		    iop GASNETE_THREAD_PASS);
		return;
	}
	else 
		return gasnete_extref_put_nbi_bulk(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
}

/*
 * In the typed version of put, we always need a source copy of the local data
 * before sending it off, which doesn't require a local bucket pin.  Also,
 * since we will be sending for relatively small sizes, it's not worth using
 * the firehose lookup.
 *
 * By using AMRequestLong, the core API will attempt to query if the
 * destination is pinned and will leverage DMAs if possible.  The difference
 * between AMRequestLong and put_bulk is that the latter will _try_ to have the
 * remote memory pinned before issuing the DMA while the former is simply a
 * lookup/fallback approach - if the destination is not pinned, Mediums are
 * used.
 */
extern gasnet_handle_t 
gasnete_put_nb (gasnet_node_t node, void *dest, void *src, 
		size_t nbytes GASNETE_THREAD_FARG)
{
	return gasnete_extref_put_nb(node, dest, src, 
	    nbytes GASNETE_THREAD_PASS);
}

extern void
gasnete_put_nbi(gasnet_node_t node, void *dest, void *src, 
		size_t nbytes GASNETE_THREAD_FARG)
{
	return gasnete_extref_put_nbi(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
}

/* ------------------------------------------------------------------------ */
/* Gets */
/*
 * Under firehose, a get from the requestors point of view does not include
 * looking up the remote node's firehose list: gets do not move the firehose.
 *
 * Upon a get call, if nbytes is within the GASNETE_GET_NON_DMA_CUTOFF, an
 * AMRequest for copy on the host side is sent.  For other cases, local buckets
 * are pinned and their reference count incremented.  At completion of the get
 * (during the callback), the reference count is decremented.
 *
 * The reason for using a GASNETE_GET_NON_DMA_CUTOFF is that GM still does not
 * support DMA gets, which means we must interrupt the host processor in order
 * for every get to succeed.  When using a DMA reversed put to complete the get
 * operation, an AMReply must still be sent in order to mark the get operation
 * complete (the host processor cannot know when a DMA operation is received).
 * It may be desirable to send payload with the get AMReply as an optimization
 * for smaller sizes.  This allows a get operation to be completed with two
 * sends as opposed to three (less GM tokens are used).
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_get_bulk)
gasnet_handle_t
gasnete_firehose_get_bulk(void *dest, gasnet_node_t node, void *src, 
			  size_t nbytes, gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	/* Request a Get in terms of a DMA put */
	gasnete_eop_t	*gop;

	gop = gasnete_eop_new(GASNETE_MYTHREAD);
	gop->dest = (uintptr_t) dest;
	gop->src = (uintptr_t) src;
	gop->len = nbytes;
	gasnetc_bucket_pin_by_addr((uintptr_t) src, nbytes);
	if (iop != NULL) {
		gop->iop = iop;
		iop->initiated_get_cnt++;
	}
	SHORT_REQ(4, 7,
	    (node, gasneti_handleridx(gasnete_firehose_get_dma_reqh), nbytes,
	     PACK(dest), PACK(src), PACK(gop)));
	return (gasnete_op_t *) gop;
}

extern gasnet_handle_t
gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, 
		     size_t nbytes GASNETE_THREAD_FARG)
{
	if (nbytes > GASNETE_GET_NON_DMA_CUTOFF)
		return gasnete_firehose_get_bulk(dest, node, src, nbytes,
		    NULL GASNETE_THREAD_PASS);
	else 
		return gasnete_extref_get_nb_bulk(dest, node, src, 
		    nbytes GASNETE_THREAD_PASS);
}

extern void
gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, 
		      size_t nbytes GASNETE_THREAD_FARG)
{
	gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
	gasnete_iop_t *iop = mythread->current_iop;

	if (nbytes > GASNETE_PUT_NON_DMA_CUTOFF) {
		gasnete_firehose_get_bulk(dest, node, src, nbytes,
		    iop GASNETE_THREAD_PASS);
		return;
	}
	else 
		return gasnete_extref_get_nbi_bulk(dest, node, src, 
		    nbytes GASNETE_THREAD_PASS);
}

/* The non-bulk get is similar to the bulk version */
/* gasnete_get_nb ...
 * gasnete_get_nbi ...
 */

/* ------------------------------------------------------------------------ */
extern void
gasnete_fifo_poll()	
{
	gasnete_eop_t	*eop;

	while ((eop = gasnete_fifo_dequeue()) != NULL) {
		if (!gasnetc_token_lo_acquire())
			return;
		gasnete_firehose_put_using_directed(eop->node, 
		    eop->dest, (void *)eop->src, eop->len, eop);
	}
}
/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
  gasneti_handler_tableentry_with_bits(gasnete_firehose_move_reph),
  gasneti_handler_tableentry_with_bits(gasnete_firehose_get_dma_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_firehose_get_dma_reph),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable() {
  return gasnete_handlers;
}

/* ------------------------------------------------------------------------------------ */
#endif
