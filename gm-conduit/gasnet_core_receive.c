/* $Id: gasnet_core_receive.c,v 1.3 2002/06/11 04:24:26 csbell Exp $
 * $Date: 2002/06/11 04:24:26 $
 * $Revision: 1.3 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */
#include "gasnet_core_internal.h"

#define GASNETC_ASSIGN_RECV_BUF(e,buf,fast) 				\
	do {	if ((fast)) buf = gm_ntohp((e)->recv.message);		\
		else buf = gm_ntohp((e)->recv.buffer); } while(0)

/* Three processing functions called from gasnetc_poll() */
void	gasnetc_process_AMRequest(gm_recv_event_t *, int);
void	gasnetc_process_AMReply(gm_recv_event_t *, int);
void	gasnetc_process_AMSystem(gm_recv_event_t *);

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
	e = gm_receive(_state.port);

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

			gasnetc_process_AMReply(ptr);
			return;

		case GM_RECV_EVENT:
			GASNETC_GM_MUTEX_UNLOCK;
			if (fast)
				ptr = gm_ntohp(e->recv.message);
			else
				ptr = gm_ntohp(e->recv.buffer);

			if (AM_MSG_SYSTEM(ptr))
				gasnetc_process_AMSystem(ptr);
			else
				gasnetc_process_AMRequest(ptr, fast, NULL);
			return;

		default:
			gm_unknown(_state.port, e);
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
	gasnetc_bufdesc_t	*bufd, *bufd_temp;
	void			*recv_buf;
	uint8_t			handler_idx, numargs;
	uint16_t		len;

	/* Get either 'message' or 'buffer' from recv'd event */
	GASNETC_ASSIGN_RECV_BUF(e, recv_buf, fast);

	bufd = GASNETC_BUFDESC_PTR(e->recv.buffer);
	handler_idx = AM_INDEX(recv_buf);
	numargs = AM_NUMARGS(recv_buf);
	len = gm_ntoh_u32(e->recv.length);

	assert(bufd != NULL);
	assert(bufd->sendbuf == e->recv.buffer);
	assert(len >= 2); /* minimum AM message */

	switch (AM_TYPE(recv_buf)) {
		case AM_SMALL:
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers(handler_idx),
				(void *) bufd, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs));
			break;
		case AM_LONG:
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers(handler_idx),
				(void *) bufd, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs),
				recv_buf, 
				len - AM_LONG_HEADER_LEN(numargs));
			break;
		case AM_MEDIUM:
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers(handler_idx),
				(void *) bufd_temp, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs),
				recv_buf, 
				len - AM_MEDIUM_HEADER_LEN(numargs));
			break;
		default:
			abort();
	}

	/* Always give the buffer back if no AMReply was called */
	if (bufd->called_reply == 0) {
		GASNETC_GM_MUTEX_LOCK;
		gm_provide_receive_buffer(_gmc.port, gm_ntohp(e->recv.buffer), 
			GASNETC_AM_SIZE, GM_LOW_PRIORITY);
		GASNETC_GM_MUTEX_UNLOCK;
	}
	bufd->called_reply = 0;
	return;
}

void
gasnetc_process_AMReply(gm_recv_event_t *e, int fast)
{
	gasnetc_bufdesc_t	*bufd;
	void			*recv_buf;
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
	bufd = GASNETC_BUFDESC_PTR(e->recv.buffer);
	handler_idx = AM_INDEX(recv_buf);
	numargs = AM_NUMARGS(recv_buf);
	len = gm_ntoh_u32(e->recv.length);

	assert(bufd != NULL);
	assert(bufd->sendbuf == e->recv.buffer);
	assert(len >= 2); /* minimum AM message */

	switch (AM_TYPE(recv_buf)) {
		case AM_SMALL:
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers(handler_idx),
				(void *) bufd, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs));
			break;
		case AM_LONG:
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers(handler_idx),
				(void *) bufd, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs),
				recv_buf, 
				len - AM_LONG_HEADER_LEN(numargs));
			break;
		case AM_MEDIUM:
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers(handler_idx),
				(void *) bufd_temp, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs),
				recv_buf, 
				len - AM_MEDIUM_HEADER_LEN(numargs));
			if (bufd->called_reply)
				GASNET_AM_MEDIUM_REPLY_MUTEX_UNLOCK;
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
	(*handler)(...);
}

/*
 * Callback functions for hi and lo token bounded functions.  Since these
 * functions are called from gm_unknown(), they already own a GM lock
 */

GASNET_INLINE_MODIFIER(gasnetc_callback_generic);
void
gasnetc_callback_generic(struct gm_port *p, void *context, gm_status_t status)
{
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(p, status);

	if (_gmc.ReplyCount > 0)
		gm_provide_receive_buffer(_gmc.port, 
				((gasnetc_bufdesc_t *)context)->sendbuf,
				GASNETC_AM_SIZE,
				GM_LOW_PRIORITY);
	else {
		GASNETC_REQUEST_FIFO_MUTEX_LOCK;
		if_pt (_gmc.reqs_fifo_cur < _gmc.reqs_fifo_max-1) { 
			_gmc.reqs_fifo[++_gmc.reqs_fifo_cur] = 
				((gasnetc_bufdesc_t *)context)->id;
		}
		GASNETC_REQUEST_FIFO_MUTEX_UNLOCK;
	}
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
