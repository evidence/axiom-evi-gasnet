/* $Id: gasnet_core_receive.c,v 1.8 2002/06/19 04:51:55 csbell Exp $
 * $Date: 2002/06/19 04:51:55 $
 * $Revision: 1.8 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */
#include "gasnet_core_internal.h"
#define AM_DUMP


#ifdef AM_DUMP
#define DEBUG_AM_DUMP(x)	do { printf x; fflush(stdout); } while (0)
#else
#define DEBUG_AM_DUMP(x)
#endif

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
#define GASNETC_ASSIGN_RECV_BUF(buf,e,fast) 				    \
	do {	if ((fast)) buf = (uint8_t *) gm_ntohp((e)->recv.message);  \
		else buf = (uint8_t *) gm_ntohp((e)->recv.buffer); } while(0)
#define GASNETC_GMNODE(x)	gasnetc_gm_nodes_search(gm_ntoh_u16((x)))

extern int gasnetc_init_done;

/* Three processing functions called from gasnetc_poll() */
void		 gasnetc_process_AMRequest(gm_recv_event_t *, int);
void		 gasnetc_process_AMReply(gm_recv_event_t *, int);
gasnetc_sysmsg_t gasnetc_process_AMSystem(gm_recv_event_t *, int, void *);

void	gasnetc_callback_error(gm_status_t status, gasnetc_bufdesc_t *bufd);

/* 
 * make progress in the receive queue
 */
int
gasnetc_AMPoll()
{
	gm_recv_event_t	*e;
	int		fast = 0;
	void		*ptr = NULL;

	gasnetc_fifo_progress();

	GASNETC_GM_MUTEX_LOCK;
	e = gm_receive(_gmc.port);

	switch (gm_ntohc(e->recv.type)) {
		case GM_NO_RECV_EVENT:
			return GASNET_OK;

		case GM_FAST_HIGH_RECV_EVENT:
		case GM_FAST_HIGH_PEER_RECV_EVENT:
			fast = 1;

		case GM_HIGH_RECV_EVENT:	/* handle AM_Reply */
			GASNETC_GM_MUTEX_UNLOCK;
			gasnetc_process_AMReply(e, fast);
			return GASNET_OK;

		case GM_FAST_RECV_EVENT:
		case GM_FAST_PEER_RECV_EVENT:
			fast = 1;
		case GM_RECV_EVENT:
			GASNETC_GM_MUTEX_UNLOCK;
			if (fast)
				ptr = gm_ntohp(e->recv.message);
			else
				ptr = gm_ntohp(e->recv.buffer);

			if (GASNETC_AM_TYPE(*(uint8_t *)ptr) & AM_SYSTEM)
				gasnetc_process_AMSystem(e, fast, NULL);
			else
				gasnetc_process_AMRequest(e, fast);
			return GASNET_OK;

		default:
			gm_unknown(_gmc.port, e);
	}
	GASNETC_GM_MUTEX_UNLOCK;

	gasnetc_fifo_progress();
	return GASNET_OK;
}

/* 
 * SysPoll is a specific AMPoll only used during bootstrap.  It does
 * not require any locking since the client guarentees that only one
 * thread calls the function.  Returned is the type of system message
 * delivered with context filled in with appropriate data if applicable
 * to the type of system message.  It is up to the client to treat the
 * received system message as expected or not (although some intelligence
 * is put into the processing call - see gasnetc_process_AMSystem
 */
gasnetc_sysmsg_t
gasnetc_SysPoll(void *context)
{
	uint8_t		*ptr;
	gm_recv_event_t	*e;
	int		fast = 0, error = 0;

	/* should register some GM alarm to make sure we wait for
	 * a bounded amount of time
	 */
	while (1) {
		e = gm_receive(_gmc.port);

		switch (gm_ntohc(e->recv.type)) {
			case GM_NO_RECV_EVENT:
				break;

			case GM_FAST_RECV_EVENT:
			case GM_FAST_PEER_RECV_EVENT:
			case GM_RECV_EVENT:
				error = 1;
				break;
			case GM_FAST_HIGH_RECV_EVENT:
			case GM_FAST_HIGH_PEER_RECV_EVENT:
				fast = 1;
			case GM_HIGH_RECV_EVENT:
				if (fast)
					ptr = (uint8_t *)
						gm_ntohp(e->recv.message);
				else
					ptr = (uint8_t *)
						gm_ntohp(e->recv.buffer);
				if (GASNETC_AM_TYPE(*ptr) & AM_SYSTEM)
					return gasnetc_process_AMSystem
						(e, fast, context);
				else
					error = 1;
			default:
				gm_unknown(_gmc.port, e);
				if ((char *) context == (char *) -1)
					return _NO_MSG;
		}
		if (error == 0)
			continue;
		else
			gasneti_fatalerror("gasnetc_SysPoll: unexpected "
				"Message %x of type %hd", *ptr,
				(uint16_t) gm_ntoh_u8(e->recv.type));
	}
}


/* 
 * Three processing functions called from gasnetc_receive 
 * From gasnetc_AMPoll()
 * <e> contains the event as returned by gm_receive()
 * <fast> tells if a message or buffer is available
 */

void
gasnetc_process_AMRequest(gm_recv_event_t *e, int fast)
{
	gasnetc_bufdesc_t	*bufd;
	uint8_t			*recv_buf;
	uint8_t			handler_idx, numargs;
	uintptr_t		dest_addr;
	uint16_t		len;

	/* Get either 'message' or 'buffer' from recv'd event */
	GASNETC_ASSIGN_RECV_BUF(recv_buf, e, fast);
	bufd = (gasnetc_bufdesc_t *) 
		GASNETC_BUFDESC_PTR(gm_ntohp(e->recv.buffer));
	assert(bufd->sendbuf == gm_ntohp(e->recv.buffer));
	assert(len >= 2); /* minimum AM message */
	assert(*recv_buf & 0x01);
	handler_idx = *(recv_buf + 1);
	numargs = GASNETC_AM_NUMARGS(*recv_buf);
	len = (uint16_t) gm_ntoh_u32(e->recv.length);

	DEBUG_AM_DUMP( ("R> AMRequest%s (%d:%d) args %d index %hd\n", 
			GASNETC_AM_TYPE_STRING(*recv_buf),
                	(uint32_t) gm_ntoh_u16(e->recv.sender_node_id),
                	(uint32_t) gm_ntoh_u8(e->recv.sender_port_id),
			GASNETC_AM_NUMARGS(*recv_buf), 
			(uint16_t) handler_idx) );

	switch (GASNETC_AM_TYPE(*recv_buf)) {
		case AM_SHORT:
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_SHORT_HEADER_LEN(numargs),
				numargs);
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
		case AM_LONG:
			GASNETC_AMDESTADDR_READ((uint32_t *)recv_buf + 2, 
					dest_addr);
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_MEDIUM_HEADER_LEN(numargs),
				numargs, dest_addr, 
				len - GASNETC_AM_LONG_HEADER_LEN(numargs));
			break;
		default:
			gasneti_fatalerror("AMRequest type unknown 0x%x, fatal",
				GASNETC_AM_TYPE(*recv_buf));
	}

	/* Unlock the AMMEDIUM_REQUEST lock if it was required */
	if ((bufd->flag & FLAG_CALLED_REPLY) && 
		(bufd->flag & FLAG_AMREQUEST_MEDIUM))
			GASNETC_AMMEDIUM_REQUEST_MUTEX_UNLOCK;
	/* Always give the buffer back if no AMReply was called */
	else {
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
	uintptr_t		dest_addr;
	uint16_t		len;

	/* processing an AM message includes extracting
	 * 1. Extracting Type
	 * 2. Assert that Reply bit is set
	 * 3. Extracting Arguments
	 * 4. Getting pointer if medium/long
	 * 5. Running Handler
	 */

	/* Get either 'message' or 'buffer' from recv'd event */
	GASNETC_ASSIGN_RECV_BUF(recv_buf, e, fast);
	bufd = (gasnetc_bufdesc_t *) 
		GASNETC_BUFDESC_PTR(gm_ntohp(e->recv.buffer));
	assert(bufd->sendbuf == gm_ntohp(e->recv.buffer));
	assert(len >= 2); /* minimum AM message */
	assert(!(*recv_buf & 0x01));
	handler_idx = *(recv_buf + 1);
	numargs = GASNETC_AM_NUMARGS(*recv_buf);
	len = (uint16_t) gm_ntoh_u32(e->recv.length);

        DEBUG_AM_DUMP( ("R> AMReply%s (%d:%d) args %d index %hd\n", 
			GASNETC_AM_TYPE_STRING(*recv_buf),
                	(uint32_t) gm_ntoh_u16(e->recv.sender_node_id),
                	(uint32_t) gm_ntoh_u8(e->recv.sender_port_id),
			GASNETC_AM_NUMARGS(*recv_buf), 
			(uint16_t) handler_idx) );

	switch (GASNETC_AM_TYPE(*recv_buf)) {
		case AM_SHORT:
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_SHORT_HEADER_LEN(numargs),
				numargs);
			break;
		case AM_MEDIUM:
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_MEDIUM_HEADER_LEN(numargs),
				numargs, recv_buf, 
				len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs));
			break;
		case AM_LONG:
			GASNETC_AMDESTADDR_READ((uint32_t *)recv_buf + 2, 
					dest_addr);
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler_idx],
				(void *) bufd, 
				recv_buf + GASNETC_AM_MEDIUM_HEADER_LEN(numargs),
				numargs, recv_buf, 
				len - GASNETC_AM_LONG_HEADER_LEN(numargs));
			break;
		default:
			gasneti_fatalerror("AMRequest type unknown 0x%x, fatal",
				GASNETC_AM_TYPE(*recv_buf));
	}
	GASNETC_GM_MUTEX_LOCK;
	gm_provide_receive_buffer(_gmc.port, gm_ntohp(e->recv.buffer), 
		GASNETC_AM_SIZE, GM_HIGH_PRIORITY);
	GASNETC_GM_MUTEX_UNLOCK;
	return;
}

/* This process function may either be called from SysPoll or
 * from AMPoll.  The former handles messages during bootstrap
 * while the latter handles post-bootstrap messages that may
 * be received asynchronously, such as kill messages or possibly
 * ulterior optimized barrier messages
 */
gasnetc_sysmsg_t
gasnetc_process_AMSystem(gm_recv_event_t *e, int fast, void *context)
{
	uint8_t			*recv_buf;
	gasnetc_sysmsg_t	sysmsg;

	GASNETC_ASSIGN_RECV_BUF(recv_buf, e, fast);
	GASNETC_SYSHEADER_READ(recv_buf, sysmsg);

	if_pf (!sysmsg || sysmsg >= _LAST_ONE)
		gasneti_fatalerror("AMSystem: unknown message %x", recv_buf);

	if_pf (gasnetc_sysmsg_types[sysmsg].len != gm_ntoh_u32(e->recv.length))
		gasneti_fatalerror("AMSystem: message %x (%s) has length %d "
				   "instead of %d", recv_buf,
				   gasnetc_sysmsg_types[sysmsg].msg,
				   gm_ntoh_u32(e->recv.length),
				   gasnetc_sysmsg_types[sysmsg].len);

	switch (sysmsg) {
		case SBRK_TOP:
			if_pf (gasnetc_init_done || gasnet_mynode != 0)
				gasneti_fatalerror("AMSystem SBRK_TOP: already"
					" initialized or mynode is not 0");
			if (context != NULL) 
				*((uintptr_t *)context) = 
					*((uintptr_t *) recv_buf + 1);

			DEBUG_AM_DUMP( ("SBRK_TOP %4hd = 0x%x\n",
				GASNETC_GMNODE(e->recv.sender_node_id),
				*((uintptr_t *) context)) );
			break;
		case SBRK_BASE:
			if_pf (gasnetc_init_done || gasnetc_mynode == 0)
				gasneti_fatalerror("AMSystem SBRK_BASE: already"
					" initialized\n");
			if (context != NULL)
				*((uintptr_t *)context) = 
					*((uintptr_t *) recv_buf + 1);
			DEBUG_AM_DUMP( ("SBRK_BASE 0x%x\n",
				*((uintptr_t *) context)) );
			break;
		case BARRIER_GATHER:
			if_pf (gasnetc_mynode != 0)
				gasneti_fatalerror("AMSystem BARRIER_GATHER "
					"can only be done at node 0");
			if (context != NULL)
				(*((int *) context))++;
			DEBUG_AM_DUMP( ("BARRIER_GATHER %4hd = %d\n",
				GASNETC_GMNODE(e->recv.sender_node_id),
				*((int *) context)) );
			break;
		case BARRIER_NOTIFY:
			if_pf (gasnetc_mynode == 0)
				gasneti_fatalerror("AMSystem BARRIER_NOTIFY "
					"can only be done from node 0\n");
			break;
		default:
			/* should not get here */
			gasneti_fatalerror("AMSystem process fatal!");
	}
	return sysmsg;
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
		if_pf (_gmc.reqs_fifo_cur < _gmc.reqs_fifo_max-1)
			gasneti_fatalerror(
				"gasnetc_callback: Send FIFO overflow (?)\n");
		GASNETC_REQUEST_FIFO_MUTEX_LOCK;
		if_pt (_gmc.reqs_fifo_cur < _gmc.reqs_fifo_max-1) 
			_gmc.reqs_fifo[++_gmc.reqs_fifo_cur] = bufd->id;
		GASNETC_REQUEST_FIFO_MUTEX_UNLOCK;
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

	if (bufd != NULL && bufd->e != NULL)
		snprintf(dest_msg, 63, "AMReply to %hd port %hd",
				(uint16_t)
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
	gasneti_fatalerror("gasnetc_callback: %s", reason);
	return;
}
			


void
gasnetc_callback_AMRequest(struct gm_port *p, void *context, gm_status_t status)
{
	gasnetc_callback_generic(p, context, status);
	gasnetc_token_lo_release();
}

void
gasnetc_callback_AMRequest_NOP(struct gm_port *p, void *c, gm_status_t status)
{
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, NULL);
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
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, NULL);
	gasnetc_token_hi_release();
}
