/* $Id: gasnet_core_receive.c,v 1.4 2002/06/13 10:09:33 csbell Exp $
 * $Date: 2002/06/13 10:09:33 $
 * $Revision: 1.4 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */
#include "gasnet_core_internal.h"

#define GASNETC_ASSIGN_RECV_BUF(e,buf,fast) 				    \
	do {	if ((fast)) buf = (uint8_t *) gm_ntohp((e)->recv.message);  \
		else buf = (uint8_t *) gm_ntohp((e)->recv.buffer); } while(0)
#ifdef DEBUG
#define GASNETC_BUFDESC_PTR(x) (((x) - _gmc.dma_bufs) % GASNETC_AM_LEN == 0 ? \
				&_gmc.bd_ptr[                                 \
				(((x) - _gmc.dma_bufs) >> GASNETC_AM_SIZE)] : \
				0)
#else
#define GASNETC_BUFDESC_PTR(x) (&_gmc.bd_ptr[                               \
				(((x) - _gmc.dma_bufs) >> GASNETC_AM_SIZE)])
#endif
#define GASNETC_TOKEN_PTR(x)	(gasnet_token_t) GASNETC_BUFDESC_PTR(x)

/* Three processing functions called from gasnetc_poll() */
void	gasnetc_process_AMRequest(gm_recv_event_t *, int);
void	gasnetc_process_AMReply(gm_recv_event_t *, int);
void	gasnetc_process_AMSystem(gm_recv_event_t *);

void	gasnetc_callback_error(gm_status_t status, gasnetc_bufdesc_t *bufd);

/* 
 * make progress in the receive queue
 */
void
gasnetc_poll()
{
	gm_recv_event_t	*e;
	int		fast = 0;
	void		*ptr = NULL;

	gasnetc_fifo_progress();

	GASNETC_GM_MUTEX_LOCK;
	e = gm_receive(_gmc.port);

	switch(gm_ntohc(e->recv.type)) {
		case GM_NO_RECV_EVENT:
			return;

		case GM_FAST_RECV_EVENT:
		case GM_FAST_HIGH_RECV_EVENT:
		case GM_FAST_PEER_RECV_EVENT:
		case GM_FAST_HIGH_PEER_RECV_EVENT:
			fast = 1;

		case GM_HIGH_RECV_EVENT:	/* handle AM_Reply */
			GASNETC_GM_MUTEX_UNLOCK;
			if (fast)
				ptr = gm_ntohp(e->recv.message);
			else
				ptr = gm_ntohp(e->recv.buffer);

			gasnetc_process_AMReply(e, fast);
			return;

		case GM_RECV_EVENT:
			GASNETC_GM_MUTEX_UNLOCK;
			if (fast)
				ptr = gm_ntohp(e->recv.message);
			else
				ptr = gm_ntohp(e->recv.buffer);

			if (*(uint8_t *)ptr & AM_SYSTEM)
				gasnetc_process_AMSystem(e);
			else
				gasnetc_process_AMRequest(e, fast);
			return;

		default:
			gm_unknown(_gmc.port, e);
	}
	GASNETC_GM_MUTEX_UNLOCK;

	gasnetc_fifo_progress();
}

/* 
 * Three processing functions called from gasnetc_receive 
 */

/*
 * From gasnetc_receive() to handle incoming AMRequests
 *      - <e> contains the event as returned by gm_receive()
 *      - <fast> tells if a message or buffer is available
 */

void
gasnetc_process_AMRequest(gm_recv_event_t *e, int fast)
{
	gasnetc_bufdesc_t	*bufd;
	int			buf_idx;
	void			*recv_buf;
	uint8_t			handler_idx, numargs;
	uint16_t		len;

	/* Get either 'message' or 'buffer' from recv'd event */
	GASNETC_ASSIGN_RECV_BUF(e, recv_buf, fast);

	bufd = (gasnetc_bufdesc_t *) 
		GASNETC_BUFDESC_PTR(gm_ntohp(e->recv.buffer));

	handler_idx = AM_INDEX((uint8_t *)recv_buf);
	numargs = AM_NUMARGS((uint8_t *)recv_buf);
	len = gm_ntoh_u32(e->recv.length);

	assert(bufd != NULL);
	assert(bufd->sendbuf == e->recv.buffer);
	assert(len >= 2); /* minimum AM message */

	switch (AM_TYPE((uint8_t *)recv_buf)) {
		case AM_SHORT:
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_SHORT_HEADER_LEN(numargs),
				numargs);
			break;
		case AM_LONG:
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_MEDIUM_HEADER_LEN(numargs),
				numargs, recv_buf, 
				len - GASNETC_AM_LONG_HEADER_LEN(numargs));
			break;
		case AM_MEDIUM:
			GASNETC_BUFDESC_FLAG_SET(bufd->flag, 
					FLAG_AMREQUEST_MEDIUM);
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_MEDIUM_HEADER_LEN(numargs),
				numargs, recv_buf, 
				len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs));
			break;
		default:
			abort();
	}

	/* Always give the buffer back if no AMReply was called */
	if (bufd->flag & FLAG_CALLED_REPLY) {
		if (bufd->flag & FLAG_AMREQUEST_MEDIUM)
			GASNETC_AMMEDIUM_REQUEST_MUTEX_UNLOCK;
		GASNETC_GM_MUTEX_LOCK;
		gm_provide_receive_buffer(_gmc.port, gm_ntohp(e->recv.buffer), 
			GASNETC_AM_SIZE, GM_LOW_PRIORITY);
		GASNETC_GM_MUTEX_UNLOCK;
	}
	GASNETC_BUFDESC_FLAG_RESET(bufd->flag);
	return;
}

void
gasnetc_process_AMReply(gm_recv_event_t *e, int fast)
{
	gasnetc_bufdesc_t	*bufd;
	uint8_t			*recv_buf;
	uint8_t			handler_idx, numargs;
	uint16_t		len;

	/* processing an AM message includes extracting
	 * 1. Extracting Type
	 * 2. Assert that Reply bit is set
	 * 3. Extracting Arguments
	 * 4. Getting pointer if medium/long
	 * 5. Running Handler
	 */

	/* Get either 'message' or 'buffer' from recv'd event */
	GASNETC_ASSIGN_RECV_BUF(e, recv_buf, fast);
	bufd = (gasnetc_bufdesc_t *) 
		GASNETC_BUFDESC_PTR(gm_ntohp(e->recv.buffer));
	handler_idx = AM_INDEX(recv_buf);
	numargs = AM_NUMARGS(recv_buf);
	len = gm_ntoh_u32(e->recv.length);

	assert(bufd != NULL);
	assert(bufd->sendbuf == gm_ntohp(e->recv.buffer));
	assert(len >= 2); /* minimum AM message */

	switch (AM_TYPE(recv_buf)) {
		case AM_SHORT:
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_SHORT_HEADER_LEN(numargs),
				numargs);
			break;
		case AM_LONG:
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_MEDIUM_HEADER_LEN(numargs),
				numargs, recv_buf, 
				len - GASNETC_AM_LONG_HEADER_LEN(numargs));
			break;
		case AM_MEDIUM:
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_MEDIUM_HEADER_LEN(numargs),
				numargs, recv_buf, 
				len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs));
			break;
		default:
			abort();
	}
	GASNETC_GM_MUTEX_LOCK;
	gm_provide_receive_buffer(_gmc.port, gm_ntohp(e->recv.buffer), 
		GASNETC_AM_SIZE, GM_HIGH_PRIORITY);
	GASNETC_GM_MUTEX_UNLOCK;
	return;
}

/* Undefined for the moment. . */
void
gasnetc_process_AMSystem(gm_recv_event_t *e)
{
	/* handler-specific code which is system-specific */
	return;
}

/*
 * Callback functions for hi and lo token bounded functions.  Since these
 * functions are called from gm_unknown(), they already own a GM lock
 */

GASNET_INLINE_MODIFIER(gasnetc_callback_generic)
void
gasnetc_callback_generic(struct gm_port *p, void *context, gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd = (gasnetc_bufdesc_t *) context;

	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, bufd);

	bufd->e = NULL;
	bufd->dest_addr = 0;
	bufd->rdma_off = 0;
	GASNETC_BUFDESC_FLAG_RESET(bufd->flag);

	if (_gmc.ReplyCount > 0)
		gm_provide_receive_buffer(_gmc.port, bufd->sendbuf,
				GASNETC_AM_SIZE, GM_LOW_PRIORITY);
	else {
		if (_gmc.reqs_fifo_cur < _gmc.reqs_fifo_max-1) {
			GASNETC_REQUEST_FIFO_MUTEX_LOCK;
			if_pt (_gmc.reqs_fifo_cur < _gmc.reqs_fifo_max-1) 
				_gmc.reqs_fifo[++_gmc.reqs_fifo_cur] = bufd->id;
			GASNETC_REQUEST_FIFO_MUTEX_UNLOCK;
		}
	}
}
/* For now, we wont support any resending for GM_SEND_TIMED_OUT,
 * so this function is limited to simply printing error messages
 * and aborting.  Interpretation of these messages comes from GM's
 * mpich code
 */
void
gasnetc_callback_error(gm_status_t status, gasnetc_bufdesc_t *bufd)
{
	char reason[128];
	char dest_msg[64];

	assert(status != GM_SUCCESS);	/* function is for errors only */

	if (bufd->e != NULL)
		snprintf(dest_msg, 63, "AMReply to %hd port %hd",
				gm_ntoh_u16(bufd->e->recv.sender_node_id),
				(uint16_t) 
				gm_ntoh_u8(bufd->e->recv.sender_port_id));
	else
		snprintf(dest_msg, 63, "AMRequest failed");

	switch (status) {
		case GM_SEND_TIMED_OUT:
			snprintf(reason, 127, "GM timed out. . %s", dest_msg);
		case GM_SEND_TARGET_PORT_CLOSED:
			snprintf(reason, 127, 
				"Target node is down or exited. .%s",
				dest_msg);
		case GM_SEND_TARGET_NODE_UNREACHABLE:
			snprintf(reason, 127, 
				"Target unknown. Check mapper/cables. . %s",
				dest_msg);
		case GM_SEND_REJECTED:
		case GM_SEND_DROPPED:
			snprintf(reason, 127,
				"Target node rejected the send. . %s",
				dest_msg);
		default:
			snprintf(reason, 127,
				"Unknown GM error. . %s", dest_msg);
	}

	fprintf(stderr, "%s\n", reason);
	fflush(stderr);
	abort();
}
			


void
gasnetc_callback_AMRequest(struct gm_port *p, void *context, gm_status_t status)
{
	gasnetc_callback_generic(p, context, status);
	gasnetc_token_lo_release();
}

void
gasnetc_callback_AMRequest_NOP(struct gm_port *p, void *context, 
				gm_status_t status)
{
	gasnetc_callback_generic(p, context, status);
	gasnetc_token_lo_release();
}

void
gasnetc_callback_AMReply(struct gm_port *p, void *context, gm_status_t status)
{

	gasnetc_callback_generic(p, context, status);
	gasnetc_token_hi_release();
}

void
gasnetc_callback_AMReply_NOP(struct gm_port *p, void *c, gm_status_t status)
{
	gasnetc_token_hi_release();
}
