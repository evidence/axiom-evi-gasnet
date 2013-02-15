#ifdef GASNETC_FIREHOSE
#include <gasnet_core_internal.h>
#include <gasnet_extended.h>
/*
 * Firehose callback functions
 */

void
gasnete_firehose_callback_pop(struct gm_port *p, void *context, 
			      gm_status_t status)
{
	gasnete_eop_t	*pop = (gasnete_eop_t *) context;

	assert(pop != NULL);
	assert(pop->node >= 0 && pop->node < gasnete_nodes);
	assert(pop->dest > 0 && pop->len > 0);
	if_pf (status != GM_SUCCESS)
	    gasnetc_callback_error(status, NULL);
	gasnetc_firehose_decrement_refcount(pop->node, pop->dest, pop->len);
	gasnete_op_markdone((gasnete_op_t *)pop);
	gasnetc_token_lo_release();
	/* If this was associated to an iop, increment put completed count */
	if (pop->iop != NULL)
		pop->iop->completed_put_cnt++;
}

void
gasnete_firehose_callback_top(struct gm_port *p, void *context, 
			      gm_status_t status)
{
	gasnete_eop_t	*top = (gasnete_eop_t *) context;

	assert(top != NULL);
	assert(top->node >= 0 && top->node < gasnete_nodes);
	assert(top->src > 0 && top->len > 0);
	if_pf (status != GM_SUCCESS)
	    gasnetc_callback_error(status, NULL);
	gasnetc_bucket_unpin_by_addr(top->src, top->len);
	gasnete_op_free((gasnete_op_t *)top);
	gasnetc_token_hi_release();
}

/* ------------------------------------------------------------------------ */
/*
 * Firehose get helpers
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_put_get_using_directed)
void
gasnete_firehose_put_get_using_directed(gasnet_node_t node, uintptr_t dest, 
					void *src, size_t nbytes, 
					gasnete_top_t *top)
{
	assert(top != NULL);
	assert(dest > 0 && src != NULL);
	assert(node >= 0 && node < gasnete_nodes);
	assert(nbytes > 0);

	while (!gasnetc_token_hi_acquire());
	gm_directed_send_with_callback(
	    _gmc.port, src, (gm_remote_ptr_t) dest,
	    (unsigned long) nbytes, GM_HIGH_PRIORITY,
	    _gmc.gm_nodes[node].id, _gmc.gm_nodes[node].port,
	    gasnete_firehose_callback_top, (void *) top);
}

GASNET_INLINE_MODIFIER(gasnete_firehose_put_using_directed)
void
gasnete_firehose_put_using_directed(gasnet_node_t node, uintptr_t dest, 
				    void *src, size_t nbytes, 
				    gasnete_eop_t *pop)
{
	assert(pop != NULL);
	assert(dest > 0 && src != NULL);
	assert(node >= 0 && node < gasnete_nodes);
	assert(nbytes > 0);

	while (!gasnetc_token_lo_acquire());
	gm_directed_send_with_callback(
	    _gmc.port, src, (gm_remote_ptr_t) dest,
	    (unsigned long) nbytes, GM_LOW_PRIORITY,
	    _gmc.gm_nodes[node].id, _gmc.gm_nodes[node].port,
	    gasnete_firehose_callback_pop, (void *) pop);
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
	gasnetc_eop_t	*pop = (gasnete_eop_t *) context;
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
	gasnete_eop_t	*top;
	gasnet_node_t	node;

	gasnetc_AMGetMsgSource(token, &node);
	top = gasnete_eop_new(GASNETE_MYTHREAD);
	top->node = node;
	top->dest = dest;
	top->peer_op = op;
	top->src = src;
	top->len = nbytes;
	bucket_pin_byaddr(src, (size_t) nbytes);
	gasnete_fifo_enqueue(top);
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
	bucket_unpin_by_addr(pop->src,pop->len);
	if (pop->iop != NULL)
		iop->completed_get_cnt++;
	gasnete_op_markdone((gasnete_op_t) op, 1);
}
SHORT_HANDLER(gasnete_firehose_get_dma_reph,1,2, 
    (token, UNPACK(a0)    ),
    (token, UNPACK2(a0,a1)));
/* ------------------------------------------------------------------------ */
/* 
 * Puts
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_put_bulk)
gasnete_handle_t
gasnete_firehose_put_bulk(gasnet_node_t node, void *dest, void *src, size_t nbytes,
			  gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	uintptr_t	*new_bucket_buf, *old_bucket_buf;
	unsigned int	new_buckets, old_buckets;
	unsigned int	max_buckets, buf_len;
	gasnete_eop_t	*pop;

	max_buckets = PAGE_ROUNDUP(nbytes, BUCKET_SIZE) >> (BUCKET_SHIFT-1);
	buf_len = old_buckets = max_buckets>>2;
	assert(sizeof(uintptr_t)*max_buckets < AMMaxMedium());
	/* Pin locally, incrementing reference counts where necessary */
	gasnetc_bucket_pin_by_addr(src, nbytes);
	/* allocate the max number of buckets in bucket buf */
	new_bucket_buf = (uintptr_t *)
	    gasneti_malloc(sizeof(uintptr_t) * max_buckets);
	old_bucket_buf = new_bucket_buf + old_buckets;
	pop = gasnete_eop_new(GASNETE_MYTHREAD);
	pop->src = (uintptr_t) src;
	pop->len = (uint32_t) nbytes;
	/* If we had to move one or more firehoses, send an AMRequest */
	if (gasnetc_firehose_build_list(node,(uintptr_t)dest,max_buckets,
	    &old_buckets,&new_buckets,old_bucket_buf,new_bucket_buf)) {
		gasnete_eop_t	*pop;
		pop = gasnete_eop_new(GASNETE_MYTHREAD);
		pop->node = node;
		pop->dest = (uintptr_t) dest;
		pop->op_peer = NULL;
		pop->iop = iop;
		gasnetc_AMRequestMedium(node,
		    gasneti_handler_idx(gasnetc_firehose_move_reqh),
		    gasneti_handler_idx(gasnete_firehose_move_reph),
		    (void *) new_bucket_buf, buf_len + old_buckets, 
		    new_buckets, old_buckets, buf_len,
		    (void *) pop);
	}
	else {
		/* all firehoses are remote pinned buckets */
		gasnete_firehose_put_using_directed(node, 
		   (uintptr_t) dest, src, nbytes, pop);
	}
	/* If we were dealing with implicit put, increment the iop */
	if (pop->iop != NULL)
		iop->initiated_put_cnt++;
	gasneti_free(new_bucket_buf);
	return ((gasnete_op_t *) pop);
}

extern gasnete_handle_t
gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, 
		     size_t nbytes GASNETE_THREAD_FARG)
{
	if (nbytes > PUT_NON_DMA_CUTOFF)
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
	gasnete_iop_t *op = mythread->current_iop;

	if (nbytes > PUT_NON_DMA_CUTOFF) {
		gasnete_firehose_put_bulk(node, dest, src, nbytes,
		    op GASNETE_THREAD_PASS);
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
extern gasnete_handle_t 
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
 * Upon a get call, if nbytes is within the GET_NON_DMA_CUTOFF, an AMRequest
 * for copy on the host side is sent.  For other cases, local buckets are
 * pinned and their reference count incremented.  At completion of the get
 * (during the callback), the reference count is decremented.
 *
 * The reason for using a GET_NON_DMA_CUTOFF is that GM still does not support
 * DMA gets, which means we must interrupt the host processor in order for
 * every get to succeed.  When using a DMA reversed put to complete the get
 * operation, an AMReply must still be sent in order to mark the get operation
 * complete (the host processor cannot know when a DMA operation is received).
 * It may be desirable to send payload with the get AMReply as an optimization
 * for smaller sizes.  This allows a get operation to be completed with two
 * sends as opposed to three (less GM tokens are used).
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_get_bulk)
gasnete_handle_t
gasnete_firehose_get_bulk(void *dest, gasnet_node_t node, uintptr_t src, 
			  size_t nbytes, gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	/* Request a Get in terms of a DMA put */
	gasnete_eop_t	*gop;

	gop = gasnete_eop_new(GASNETE_MYTHREAD);
	gop->dest = (uintptr_t) dest;
	gop->src = src;
	gop->len = nbytes;
	gop->peer_op = NULL;
	gasnetc_bucket_pin_by_addr(src, nbytes);
	if (iop != NULL) {
		gop->iop = iop;
		iop->initiated_get_cnt++;
	}
	AMRequestShort(node, handler_idx(gasnete_firehose_get_dma_reqh),
		       nbytes, dest, src, gop);
	return (gasnet_op_t *) gop;
}

extern gasnete_handle_t
gasnete_get_nb_bulk (gasnet_node_t node, void *dest, void *src, 
		     size_t nbytes GASNETE_THREAD_FARG)
{
	if (nbytes > GET_NON_DMA_CUTOFF)
		return gasnete_firehose_get_bulk(node, dest, src, nbytes,
		    NULL GASNETE_THREAD_PASS);
	else 
		return gasnete_extref_get_nb_bulk(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
}

extern void
gasnete_get_nbi_bulk (gasnet_node_t node, void *dest, void *src, 
		      size_t nbytes GASNETE_THREAD_FARG)
{
	gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
	gasnete_iop_t *op = mythread->current_iop;

	if (nbytes > PUT_NON_DMA_CUTOFF) {
		gasnete_firehose_get_bulk(node, dest, src, nbytes,
		    op GASNETE_THREAD_PASS);
		return;
	}
	else 
		return gasnete_extref_get_nbi_bulk(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
}

/* The non-bulk get is similar to the bulk version */
/* gasnete_get_nb ...
 * gasnete_get_nbi ...
 */

/* ------------------------------------------------------------------------ */
/* FIFO operations */
extern void
gasnete_fifo_poll()	
{
	while (op = gasnete_fifo_head()) {
		if (OP_IS_TRANSIENT(op)) {	/* get operation */
			gasnete_eop_t	*top = (gasnete_eop_t *) op;
			assert(top->peer_op != NULL);
			gasnete_firehose_put_get_using_directed(top->node,
			    top->dest, top->src, top->len, top);
			GASNETE_SAFE(
			    SHORT_REP(1,2,(token, 
			        gasneti_handleridx(fh_move_firehose_reph),
				top->peer_op)));
		}
		else {				/* put operation */
			gasnete_eop_t	*pop = (gasnete_eop_t *) op;
			gasnete_firehose_put_using_directed(pop->node, 
			    pop->dest, pop->src, pop);
		}
		gasnete_fifo_dequeue();
	}
}
#endif
