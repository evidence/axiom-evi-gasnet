/* $Id: gasnet_core_internal.h,v 1.1 2002/06/10 07:54:52 csbell Exp $
 * $Date: 2002/06/10 07:54:52 $
 * $Revision: 1.1 $
 * Description: GASNet gm conduit header for internal definitions in Core API
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gm.h>
#include <asm/param.h> /* MAXHOSTNAMELEN */

extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {                               \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasneti_ErrorName(retcode), retcode);                \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-99 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)

#define GASNETC_AM_LEN	4096
#define GASNETC_AM_SIZE	12

typedef struct gasnetc_bufdesc gasnet_bufdesc_t;

/* Forward declaration for miscellaneous functions used by GM core */
gasnetc_bufdesc_t * 	gasnetc_AMRequestBuf_block();

void	gm_node_compare(const void *, const void *);
void	gasnetc_sendbuf_init();
void	gasnetc_sendbuf_finalize();
void	gasnetc_tokensend_AMRequest(void *, uint16_t, uint32_t, uint32_t, 
			gm_send_completion_callback_t, uint64_t);
void	gasnetc_gmpiconf_init();

/*
 * These are GM tokens, represented by the type
 * gasnetc_token_t (not to be mistaken with gasnet_token_t
 * which is the user interface to the AMReply opaque type)
 *
 */
typedef
struct {
	int	max;
	int	hi;
	int	lo;
	int	total;
}
gasnetc_token_t;

/* Buffer descriptor.  Each DMA-pinned AM buffer has one
 * of these attached to it. */
typedef
struct gasnetc_bufdesc {
	void	*sendbuf;	/* map to buffer */
	short	id;		/* reverse map in bufdesc list */

	/* AMReply/AMRequest fields */
	uint64_t	dest_addr;	/* directed_send address */
	off_t		rdma_off;	/* rdma_off for AMLong */

	/* AMReply only fields */
	gm_recv_event_t		*e;		/* GM receive event */
	short			called_reply;	/* AMRequest processing */
	uint16_t		len;		/* length for queued sends */
	struct	gasnetc_bufdesc	*next;		/* send FIFO queue */
}
gasnetc_bufdesc_t;

/* Gasnet GM node->id mapping */
struct gasnetc_gm_nodes {
	uint16_t	id;
	uint16_t	port;
} gasnetc_gm_nodes_t;

/* Gasnet GM id->node mapping */
struct gasnetc_gm_nodes_rev {
	uint16_t	id;	/* sort key */
	gasnet_node_t	node;
} gasnetc_gm_nodes_rev_t;

/* Global GM Core type */
static
struct _gm_core_global {
	int			recv_tokens;
	int			ReplyCount;
	gasnetc_gm_nodes_t	*gm_nodes;
	gasnetc_gm_nodes_rev_t	*gm_nodes_rev;

	gasnetc_bufdesc_t	*AMReplyBuf_next;
	gasnetc_bufdesc_t	*bd_ptr;
	int			bd_list_num;

	/* FIFO AMRequest send queue */
	int		*reqs_fifo;
	volatile int	reqs_fifo_cur;

	/* FIFO overflow send queue */
	gasnetc_bufdesc_t	*fifo_bd_head;
	gasnetc_bufdesc_t	*fifo_bd_tail;

	void		*reqsbuf;	/* DMAd portion of send buffers */
	struct gm_port	*p;		/* GM port structure */
}	
_gmc;

/* undefine for now. .assume GASNET_SEQ */
#define GASNETC_TOKEN_MUTEX_LOCK
#define GASNETC_TOKEN_MUTEX_UNLOCK

/*
 * Inline Function definitions for FIFO and TOKEN operations
 */

/* Helper MACROS for the acquire/relase inline functions
 * ** These should not be called from client code
 */
#define GASNETC_TOKEN_HI_NUM()	(_gmc.token.max-1 - _gmc.token.hi)
#define GASNETC_TOKEN_HI_AVAILABLE() \
				((_gmc.token.hi < _gmc.token.max-1) && \
				 (_gmc.token.total < _gmc.token.max))

#define GASNETC_TOKEN_LO_NUM()	(_gmc.token.max-1 - _gmc.token.lo)
#define GASNETC_TOKEN_LO_AVAILABLE() \
				((_gmc.token.lo < _gmc.token.max-1) && \
				 (_gmc.token.total < _gmc.token.max))

GASNET_INLINE_MODIFIER(gasnetc_token_hi_acquire);
int
gasnetc_token_hi_acquire()
{
	if (GASNETC_TOKEN_HI_AVAILABLE()) {
		_gmc.token.hi += 1;
		_gmc.token.total += 1;
		return 1;
	}
	else {
		return 0;
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_hi_release)
void
gasnetc_token_hi_release()
{
	assert((_gmc.token.hi-1 >= 0) && (_gmc.token.total-1 >= 0));
	_gmc.token.hi -= 1;
	_gmc.token.total -= 1;
}

/* Return an index into the sendbuf */
GASNET_INLINE_MODIFIER(gasnetc_token_lo_acquire);
int
gasnetc_token_lo_acquire()
{
	int	reqs_fifo_idx;

	GASNETC_TOKEN_MUTEX_LOCK;
	if (GASNETC_TOKEN_LO_AVAILABLE()) {
		assert(reqs_fifo_cur == GASNETC_TOKEN_LO_AVAILABLE);
		reqs_fifo_idx = _gmc.reqs.fifo_bd[reqs_fifo_cur--];
		_gmc.token.lo += 1;
		_gmc.token.total += 1;
		GASNETC_TOKEN_MUTEX_UNLOCK;
		return reqs_fifo_idx;
	}
	else {
		GASNETC_TOKEN_MUTEX_UNLOCK;
		return 0;
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_lo_release)
void
gasnetc_token_lo_release()
{
	GASNETC_TOKEN_MUTEX_LOCK;
	assert((_gmc.token.lo-1 >= 0) && (_gmc.token.total-1 >= 0));
	_gmc.reqs_fifo_cur++;
	_gmc.token.lo -= 1;
	_gmc.token.total -= 1;
	GASNETC_TOKEN_MUTEX_UNLOCK;
}


#define gasnetc_fifo_head()	_gmc.bufdesc_head

GASNET_INLINE_MODIFIER(gasnetc_fifo_remove);
void
gasnetc_fifo_remove()
{
	gasnetc_bufdesc_t	*buf;

	assert(_gmc.bufdesc_head != NULL);
	assert(_gmc.bufdesc_tail != NULL);
	buf = _gmc.bufdesc_head;

	/* Handle AMLongReply, leave it in the queue in order to
	 * let the next send be the header send
	 */
	if (buf->rdma_off > 0) {
		buf->rdma_off = 0;
		return;
	}

	if (_gmc.bufdesc_head == _gmc.bufdesc_tail)
		_gmc.bufdesc_head = _gmc.bufdesc_tail = NULL;
	else 
		_gmc.bufdesc_head = buf->next;
}

GASNET_INLINE_MODIFIER(gasnetc_fifo_insert);
void
gasnetc_fifo_insert(gasnetc_bufdesc_t *buf)
{
	/* Insert at end of queue and update head/tail */
	assert(buf != NULL);
	buf->next = NULL:
	if (_gmc.bufdesc_head == NULL || _gmc.bufdesc_tail == NULL)
		_gmc.bufdesc_head = _gmc.bufdesc_tail = buf;
	else {
		(_gmc.bufdesc_tail)->next = buf;
		_gmc.bufdesc_tail = buf;
	}
	return;
}

/*
 * Here we relax the MUTEX requirement to sample the number of
 * tokens available.  If there are tokens available without entering
 * the critical section, hopes are that we'll be able to get one.
 */

GASNET_INLINE_MODIFIER(gasnetc_fifo_progress);
void
gasnetc_fifo_progress()
{
	while (gasnetc_fifo_head() && GASNETC_TOKEN_HI_AVAILABLE()) {
		GASNETC_GM_MUTEX_LOCK;
		if (gasnetc_token_hi_acquire()) {
			gasnetc_bufdesc_t *bufd = gasnetc_fifo_head();
			gasnetc_gm_send_AMReply(bufd);
			gasnetc_fifo_remove();
		}
		GASNETC_GM_MUTEX_UNLOCK;
	}
}

/* GM gm_send/gm_directed_send wrapper for AMReply */
GASNET_INLINE_MODIFIER(gasnetc_gm_send_AMReply)
int
gasnetc_gm_send_AMReply(gasnetc_bufdesc_t *bufd)
{
	assert(bufd != NULL);
	assert(bufd->sendbuf != NULL);
	assert(bufd->len > 0);
	assert(bufd->e != NULL);
	assert(bufd->e->recv.sender_node_id > 0);
	assert(bufd->e->recv.sender_port_id >= 0);

	if (bufd->rdma_off > 0) {
		gm_directed_send_with_callback(_gmc.port, 
			bufd->sendbuf + rdma_off,
			bufd->dest_addr,
			GASNETC_AM_SIZE,
			bufd->len - bufd->rdma_off, 
			GM_HIGH_PRIORITY,
			bufd->e->recv.sender_node_id,
			bufd->e->recv.sender_port_id,
			gasnetc_callback_AMReply_NOP,
			(void *) bufd);

		bufd->len = bufd->rdma_off;
	}
	else
		gm_send_with_callback(_gmc.port, 
			bufd->sendbuf,
			GASNETC_AM_SIZE,
			bufd->len, 
			GM_HIGH_PRIORITY,
			bufd->e->recv.sender_node_id,
			bufd->e->recv.sender_port_id,
			gasnetc_callback_AMReply,
			(void *) bufd);
}

/* GM gm_send/gm_directed_send wrapper for AMRequest */
GASNET_INLINE_MODIFIER(gasnetc_gm_send_AMRequest)
int
gasnetc_gm_send_AMRequest(gasnetc_void *buf, uint16_t len,
		uint32_t id, uint32_t port, 
		gm_send_completion_callback_t callback,
		uint64_t dest_addr)
{
	assert(buf != NULL);
	assert(len <= GASNETC_AM_LEN); 
	assert(id > 0);
	assert(port >= 0);
	assert(callack != NULL);

	if (dest_addr > 0)
		gm_directed_send_with_callback(_gmc.port, 
			buf,
			dest_addr,
			GASNETC_AM_SIZE,
			len,
			GM_LOW_PRIORITY,
			id,
			port,
			callback,
			(void *) bufd);
	else
		gm_send_with_callback(_gmc.port, 
			bufd->sendbuf,
			GASNETC_AM_SIZE,
			bufd->len, 
			GM_LOW_PRIORITY,
			id,
			port,
			callback,
			(void *) bufd);
}

/* 
 * This writes a Short sized buffer and returns the number of
 * bytes written in total to the buffer
 *
 * |header(1)|handler(1)|pad(2)|args(0..64)
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferShort)
uint16_t
gasnetc_write_AMBufferShort(	void *buf,
				gasnet_handler_t handler,
				int numargs, va_list argptr, int request)
{
	GASNETC_ASSERT_AMSMALL(buf, AM_SMALL, handler, numargs, request);

	GASNETC_AMHEADER_WRITE(buf, AM_SMALL, numargs, request);
	GASNETC_AMHANDLER_WRITE(buf+1, handler);
	GASNETC_ARGS_WRITE((uint32_t *)buf + 1, argptr, numargs);
	GASNETC_AMPAYLOAD_WRITE(buf_payload, source_addr, nbytes);

	assert(AM_SHORT_HEADER_LEN(numargs) <= GASNETC_AM_LEN);
	return AM_SHORT_HEADER_LEN(numargs);
}

/* 
 * This writes a Medium sized buffer and returns the number of
 * bytes written in total to the buffer
 *
 * |header(1)|handler(1)|len(2)|args(0..64)|payload(0..??)
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferMedium)
uint16_t
gasnetc_write_AMBufferMedium(	void *buf,
				gasnet_handler_t handler,
				int numargs, va_list argptr, 
				size_t nbytes,
				void *source_addr,
				int request)
{
	void	*buf_payload = (void *) buf + 
					AM_MEDIUM_HEADER_LEN(numargs);

	GASNETC_ASSERT_AMMEDIUM(buf, AM_MEDIUM, handler, numargs,
				request, nbytes, source_addr);

	GASNETC_AMHEADER_WRITE(buf, AM_MEDIUM, numargs, request);
	GASNETC_AMHANDLER_WRITE(buf+1, handler);
	GASNETC_AMLENGTH_WRITE((uint16_t *)buf + 1, nbytes);
	GASNETC_ARGS_WRITE((uint32_t *)buf + 1, argptr, numargs);
	GASNETC_AMPAYLOAD_WRITE(buf_payload, source_addr, nbytes);

	assert(AM_MEDIUM_HEADER_LEN(numargs) + nbytes  <= GASNETC_AM_LEN);
	return AM_MEDIUM_HEADER_LEN(numargs) + nbytes;
}

/* 
 * This writes a Long sized buffer header and returns the number of
 * bytes written 
 *
 * |header(1)|handler(1)|pad(2)|len(4)|dest_addr(8)|args(0..64)|payload(0..??)
 *
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferLong)
uint16_t
gasnetc_write_AMBufferLong(	void *buf,
				gasnet_handler_t handler,
				int numargs, va_list argptr, 
				size_t nbytes,
				void *source_addr,
				void *dest_addr,
				int request)
{
	GASNETC_ASSERT_AMLONG(buf, AM_LONG, handler, numargs,
				request, nbytes, source_addr, dest_addr);

	GASNETC_AMHEADER_WRITE(buf, AM_LONG, numargs, request);
	GASNETC_AMHANDLER_WRITE(buf+1, handler);

	GASNETC_AMLENGTH_WRITE4((uint32_t *)buf + 1, nbytes);
	GASNETC_AMDESTADDR_WRITE((uint64_t *)buf + 1, dest_addr); 
	GASNETC_ARGS_WRITE((uint32_t *)buf + 4, argptr, numargs);

	assert(AM_LONG_HEADER_LEN(numargs) <= GASNETC_AM_LEN);
	return AM_LONG_HEADER_LEN(numargs);
}

GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferBulk)
void
gasnetc_write_AMBufferBulk(void *dest, void *src, size_t nbytes)
{
	assert(nbytes >= 0);
	GASNETC_AMPAYLOAD_WRITE(dest, src, nbytes);
	return;
}

/* Functions to read from buffer descriptors */
GASNET_INLINE_MODIFIER(gasnetc_read_bufdesc_sender_node_id)
gasnet_node_t
gasnetc_read_bufdesc_sender_node_id(gasnetc_bufdesc_t *)
{
	uint16_t		sender_node_id;
	gasnetc_gm_nodes_recv_t	*gm_node;

	if (!bufd->e) GASNETI_RETURN_ERRR(BAD_ARG, "No GM receive event");
	if (!bufd->e.sender_node_id) GASNETI_RETURN_ERRR(BAD_ARG, 
						"No GM sender_node_id");
	
	sender_node_id = gm_ntoh_u16(bufd->e.sender_node_id);

	gm_node = (gasnet_node_t *)
		bsearch((void *) &sender_node_id, 
			(const void *) _gmc.gm_nodes_recv,
			(size_t) gasnetc_nodes,
			sizeof(gasnetc_gm_nodes_recv_t),
			gm_node_compare);

	assert(gm_node != NULL);
	return gmnode->node;
}

/* macro to access all of the buffer descriptor functions above */
#define gasnetc_read_bufdesc(source, bufd) gasnetc_read_bufdesc_##source(bufd)

#endif
