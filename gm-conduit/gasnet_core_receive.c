/* $Id: gasnet_core_receive.c,v 1.2 2002/06/11 00:18:50 csbell Exp $
 * $Date: 2002/06/11 00:18:50 $
 * $Revision: 1.2 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */
#include "gasnet_core_internal.h"

#define GASNETC_ASSIGN_RECV_BUF(e,buf,fast) 				\
	do {	if ((fast)) buf = gm_ntohp((e)->recv.message);		\
		else buf = gm_ntohp((e)->recv.buffer); } while(0)


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
	GASNETC_GM_MUTEX_UNLOCK;

	switch(gm_ntohc(e->recv.type)) {
		case GM_NO_RECV_EVENT:
			return;

		case GM_FAST_RECV_EVENT:
		case GM_FAST_HIGH_RECV_EVENT:
		case GM_FAST_PEER_RECV_EVENT:
		case GM_FAST_HIGH_PEER_RECV_EVENT:
			fast = 1;

		case GM_HIGH_RECV_EVENT:	/* handle AM_Reply */
			if (fast)
				ptr = gm_ntohp(e->recv.message);
			else
				ptr = gm_ntohp(e->recv.buffer);

			gasnetc_process_AMReply(ptr);
			return;

		case GM_RECV_EVENT:
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
			GASNETC_GM_MUTEX_LOCK;
			gm_unknown(_state.port, e);
			GASNETC_GM_MUTEX_UNLOCK;
	}

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
	len = ...; 	/* parse length */

	assert(bufd != NULL);
	assert(bufd->sendbuf == e->recv.buffer);
	assert(gm_ntoh_u32(e->recv.length) >= 2); /* minimum AM message */

	/* parse actual message, short/med/long */
	/* XXX <insert code here> */
	if (AM_TYPE(recv_buf) & AM_MEDIUM) {
		/* need a transient buffer - serialize all threads in Reply */
		GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers(handler_idx),
				(void *) bufd, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs),
				recv_buf, len);
	} else if (AM_TYPE(recv_buf) & AM_SMALL) {
		GASNETC_RUN_HANDLER_SHORT(_gmc.handlers(handler_idx),
				(void *) bufd, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs));
	} else if (AM_TYPE(recv_buf) & AM_LONG) {
		GASNETC_RUN_HANDLER_LONG(_gmc.handlers(handler_idx),
				(void *) bufd, numargs,
				recv_buf + AM_MEDIUM_HEADER_LEN(numargs),
				recv_buf, len);
	} else {
		abort();
	}

	/* 
	 * handler-specific code which possibly runs gasnetc_AM_Reply() 
	 *
	 * If AM_Reply runs:
	 *   1. sets the bufd->called_reply bit
	 *   2. uses bufd->sendbuf to issue next send
	 *   3. send uses gasnetc_callback_AM(buf)
	 *
	 * When gasnetc_callback_AM() runs:
	 *   if _gmc.ReplyCount > 0
	 *   	gm_provide_buffer(_gmc.port, ..., bufd->sendbuf);
	 *   else
	 *   	gasnetc_token_lo_release(bufd);
	 *
	 */

	/* Handler completes */
	GASNETC_GM_MUTEX_LOCK;
	if_pf (is_med) {
		if_pt (bufd->called_reply) {
			_gmc.ReplyCount++;
			_gmc.Transientbuf = bufd;
		} else {
			gm_provide_receive_buffer(_gmc.port, e->recv.buffer, 
					GASNETC_AM_SIZE, GM_HIGH_PRIORITY);
			_gmc.TransientBuf = bufd_temp;
		}
	}
	else {
		if_pt (bufd->called_reply) 
			_gmc.ReplyCount++;
		else 
			gm_provide_receive_buffer(_gmc.port, e->recv.buffer, 
				GASNETC_AM_SIZE, GM_HIGH_PRIORITY);
	}
	GASNETC_GM_MUTEX_UNLOCK;
}

void
gasnetc_process_AMReply(gm_recv_event_t *e, int fast)
{
	unsigned char	*buf;

	/* processing an AM message includes extracting
	 * 1. Extracting Type
	 * 2. Assert that Reply bit is set
	 * 3. Extracting Arguments
	 * 4. Getting pointer if medium/long
	 * 5. Running Handler
	 */


	/* handler-specific code which simply returns */
	(*handler)(...);
}

/* Undefined for the moment. . */
void
gasnetc_process_AMSystem(gm_recv_event_t *e)
{
	/* handler-specific code which is system-specific */
	(*handler)(...);
}

/*
 * Callback functions for hi and lo token bounded functions
 */
gasnetc_callback_AMReply(..., void *context)
{
	if (_gmc.ReplyCount > 0)
		gm_provide_receive_buffer(_gmc.port, 
				((gasnetc_bufdesc_t *)context)->sendbuf,
				GASNETC_AM_SIZE,
				GM_LOW_PRIORITY);
	else
		gasnetc_token_hi_release();
}

gasnetc_callback_AMReply_NOP(..., void *context)
{
	gasnetc_token_hi_release();
}

gasnetc_callback_AMRequest(..., void *context)
{
	if (_gmc.ReplyCount > 0)
		gm_provide_receive_buffer(_gmc.port, 
				((gasnetc_bufdesc_t *)context)->sendbuf,
				GASNETC_AM_SIZE,
				GM_LOW_PRIORITY);
	else
		gasnetc_token_lo_release(
			((gasnetc_bufdesc_t *)context)->sendbuf);
}
