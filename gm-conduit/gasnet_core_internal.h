/* $Id: gasnet_core_internal.h,v 1.5 2002/06/14 03:40:38 csbell Exp $
 * $Date: 2002/06/14 03:40:38 $
 * $Revision: 1.5 $
 * Description: GASNet gm conduit header for internal definitions in Core API
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>
#include "gasnet_core_types.h"
#if defined(__i386__) && !defined(i386)	/* fix gm. cpu detection */
#define i386
#endif
#ifdef __GNUC__
  #define inline __inline__
  #include <gm.h>
  #undef inline
#else
  #include <gm.h>
#endif
#ifdef LINUX
#include <asm/param.h> /* MAXHOSTNAMELEN */
#else
 #ifdef FREEBSD	 /* sys/param.h defines its own min/max */
 #undef MIN
 #undef MAX
 #endif
#include <sys/param.h>
#endif
#if !defined(GASNET_SEQ) && !defined(FREEBSD)
#include <pthread.h>
#endif

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

typedef struct gasnetc_bufdesc gasnetc_bufdesc_t;

/* Forward declaration for miscellaneous functions used by GM core */
gasnetc_bufdesc_t * 	gasnetc_AMRequestBuf_block();
void			gasnetc_tokensend_AMRequest(void *, uint16_t, uint32_t, 
				uint32_t, gm_send_completion_callback_t, 
				void *, uint64_t);
int			gasnetc_gm_nodes_compare(const void *, const void *);
void			gasnetc_sendbuf_init();
void			gasnetc_sendbuf_finalize();
int			gasnetc_gmpiconf_init();

/* GM Callback functions */
void	gasnetc_callback_AMRequest(struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_AMRequest_NOP(struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_AMReply(struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_AMReply_NOP(struct gm_port *, void *, gm_status_t);

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
#define FLAG_CALLED_REPLY	0x01
#define FLAG_AMREQUEST_MEDIUM	0x02
#define GASNETC_BUFDESC_FLAG_SET(b,f)	((b) | (f)) 
#define GASNETC_BUFDESC_FLAG_RESET(b)	((b) & 0x00)

struct gasnetc_bufdesc {
	void	*sendbuf;	/* map to buffer */
	short	id;		/* reverse map in bufdesc list */
	uint8_t	flag;		/* bufdesc flags as defined above */

	/* AMReply/AMRequest fields */
	uint64_t	dest_addr;	/* directed_send address */
	off_t		rdma_off;	/* rdma_off for AMLong */

	/* AMReply only fields */
	gm_recv_event_t		*e;		/* GM receive event */
	uint16_t		len;		/* length for queued sends */
	struct	gasnetc_bufdesc	*next;		/* send FIFO queue */
};

/* Gasnet GM node->id mapping */
typedef
struct gasnetc_gm_nodes {
	uint16_t	id;
	uint16_t	port;
} gasnetc_gm_nodes_t;

/* Gasnet GM id->node mapping */
typedef
struct gasnetc_gm_nodes_rev {
	uint16_t	id;	/* sort key */
	gasnet_node_t	node;
} gasnetc_gm_nodes_rev_t;

/* Global GM Core type */
static
struct _gm_core_global {
	gasnetc_token_t		token;
	int			ReplyCount;
	gasnetc_handler_fn_t	handlers[GASNETC_AM_MAX_HANDLERS];
	gasnetc_gm_nodes_t	*gm_nodes;
	gasnetc_gm_nodes_rev_t	*gm_nodes_rev;

	gasnetc_bufdesc_t	*AMReplyBuf;
	gasnetc_bufdesc_t	*bd_ptr;
	int			bd_list_num;
	void			*dma_bufs;	/* All DMA bufs */

	/* FIFO AMRequest send queue */
	int		*reqs_fifo;
	int		reqs_fifo_max;
	volatile int	reqs_fifo_cur;

	/* FIFO overflow send queue */
	gasnetc_bufdesc_t	*fifo_bd_head;
	gasnetc_bufdesc_t	*fifo_bd_tail;

	void		*reqsbuf;	/* DMAd portion of send buffers */
	struct gm_port	*port;		/* GM port structure */
}	
_gmc;

GASNET_INLINE_MODIFIER(gasnetc_portid)
uint16_t
gasnetc_portid(gasnet_node_t node)
{
	assert(node < gasnetc_nodes);
	return _gmc.gm_nodes[node].port;
}

GASNET_INLINE_MODIFIER(gasnetc_nodeid)
uint16_t
gasnetc_nodeid(gasnet_node_t node)
{
	assert(node < gasnetc_nodes);
	return _gmc.gm_nodes[node].id;
}

/* undefine for now. .assume GASNET_SEQ */
#ifdef GASNET_PAR
extern pthread_mutex_t	_gasnetc_lock_gm;
extern pthread_mutex_t	_gasnetc_lock_reqfifo;
extern pthread_mutex_t	_gasnetc_lock_amreq;
#define _GASNETC_MUTEX_LOCK(m) do { \
		int ret = pthread_mutex_lock(&m); assert(!ret); } while (0)
#define _GASNETC_MUTEX_UNLOCK(m) do { \
		int ret = pthread_mutex_unlock(&m); assert(!ret); } while (0)
#define GASNETC_GM_MUTEX_LOCK	_GASNETC_MUTEX_LOCK(_gasnetc_lock_gm);
#define GASNETC_GM_MUTEX_UNLOCK _GASNETC_MUTEX_UNLOCK(_gasnetc_lock_gm);
#define GASNETC_REQUEST_FIFO_MUTEX_LOCK \
		_GASNETC_MUTEX_LOCK(_gasnetc_lock_reqfifo);
#define GASNETC_REQUEST_FIFO_MUTEX_UNLOCK \
		_GASNETC_MUTEX_UNLOCK(_gasnetc_lock_reqfifo);
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_LOCK \
		_GASNETC_MUTEX_LOCK(_gasnetc_lock_amreq);
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_UNLOCK \
		_GASNETC_MUTEX_UNLOCK(_gasnetc_lock_amreq);
#else
#define GASNETC_GM_MUTEX_LOCK
#define GASNETC_GM_MUTEX_UNLOCK
#define GASNETC_REQUEST_FIFO_MUTEX_LOCK
#define GASNETC_REQUEST_FIFO_MUTEX_UNLOCK
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_LOCK
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_UNLOCK
#endif 

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

GASNET_INLINE_MODIFIER(gasnetc_token_hi_acquire)
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

GASNET_INLINE_MODIFIER(gasnetc_token_lo_release)
void
gasnetc_token_lo_release()
{
	assert((_gmc.token.lo-1 >= 0) && (_gmc.token.total-1 >= 0));
	_gmc.token.lo -= 1;
	_gmc.token.total -= 1;
}

/* GM gm_send/gm_directed_send wrapper for AMReply */
GASNET_INLINE_MODIFIER(gasnetc_gm_send_AMReply)
void
gasnetc_gm_send_AMReply(gasnetc_bufdesc_t *bufd)
{
	assert(bufd != NULL);
	assert(bufd->sendbuf != NULL);
	assert(bufd->len > 0);
	assert(bufd->e != NULL);
	assert(gm_ntoh_u16(bufd->e->recv.sender_node_id) > 0);

	if (bufd->rdma_off > 0) {
		gm_directed_send_with_callback(_gmc.port, 
			bufd->sendbuf + bufd->rdma_off,
			bufd->dest_addr,
			bufd->len - bufd->rdma_off, 
			GM_HIGH_PRIORITY,
			(uint32_t) gm_ntoh_u16(bufd->e->recv.sender_node_id),
			(uint32_t) gm_ntoh_u8(bufd->e->recv.sender_port_id),
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
			(uint32_t) gm_ntoh_u16(bufd->e->recv.sender_node_id),
			(uint32_t) gm_ntoh_u8(bufd->e->recv.sender_port_id),
			gasnetc_callback_AMReply,
			(void *) bufd);
}

/* GM gm_send/gm_directed_send wrapper for AMRequest */
GASNET_INLINE_MODIFIER(gasnetc_gm_send_AMRequest)
void
gasnetc_gm_send_AMRequest(void *buf, uint16_t len,
		uint32_t id, uint32_t port, 
		gm_send_completion_callback_t callback,
		void *callback_ptr,
		uint64_t dest_addr)
{
	assert(buf != NULL);
	assert(len <= GASNETC_AM_LEN); 
	assert(id > 0);
	assert(port >= 0);
	assert(callback != NULL);

	if (dest_addr > 0)
		gm_directed_send_with_callback(_gmc.port, 
			buf,
			dest_addr,
			len,
			GM_LOW_PRIORITY,
			id,
			port,
			callback,
			callback_ptr);
	else
		gm_send_with_callback(_gmc.port, 
			buf,
			GASNETC_AM_SIZE,
			len,
			GM_LOW_PRIORITY,
			id,
			port,
			callback,
			callback_ptr);
}

#define gasnetc_fifo_head()	_gmc.fifo_bd_head

GASNET_INLINE_MODIFIER(gasnetc_fifo_remove)
void
gasnetc_fifo_remove()
{
	gasnetc_bufdesc_t	*buf;

	assert(_gmc.fifo_bd_head != NULL);
	assert(_gmc.fifo_bd_tail != NULL);
	buf = _gmc.fifo_bd_head;

	/* Handle AMLongReply, leave it in the queue in order to
	 * let the next send be the header send
	 */
	if (buf->rdma_off > 0) {
		buf->rdma_off = 0;
		return;
	}

	if (_gmc.fifo_bd_head == _gmc.fifo_bd_tail)
		_gmc.fifo_bd_head = _gmc.fifo_bd_tail = NULL;
	else 
		_gmc.fifo_bd_head = buf->next;
}

GASNET_INLINE_MODIFIER(gasnetc_fifo_insert)
void
gasnetc_fifo_insert(gasnetc_bufdesc_t *buf)
{
	/* Insert at end of queue and update head/tail */
	assert(buf != NULL);
	buf->next = NULL;
	if (_gmc.fifo_bd_head == NULL || _gmc.fifo_bd_tail == NULL)
		_gmc.fifo_bd_head = _gmc.fifo_bd_tail = buf;
	else {
		(_gmc.fifo_bd_tail)->next = buf;
		_gmc.fifo_bd_tail = buf;
	}
	return;
}

/*
 * Here we relax the MUTEX requirement to sample the number of
 * tokens available.  If there are tokens available without entering
 * the critical section, hopes are that we'll be able to get one.
 */

GASNET_INLINE_MODIFIER(gasnetc_fifo_progress)
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
				int numargs, va_list argptr, int req)
{
	GASNETC_ASSERT_AMSHORT(buf, AM_SHORT, handler, numargs, req);

	GASNETC_AMHEADER_WRITE(buf, AM_SHORT, numargs, req);
	GASNETC_AMHANDLER_WRITE(buf+1, handler);
	GASNETC_ARGS_WRITE((uint32_t *)buf + 1, argptr, numargs);

	assert(GASNETC_AM_SHORT_HEADER_LEN(numargs) <= GASNETC_AM_LEN);
	return GASNETC_AM_SHORT_HEADER_LEN(numargs);
}

/* 
 * This writes a Medium sized buffer and returns the number of
 * bytes written in total to the buffer
 *
 * |header(1)|handler(1)|len(2)|args(0..64)|payload(0..?)
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
					GASNETC_AM_MEDIUM_HEADER_LEN(numargs);

	GASNETC_ASSERT_AMMEDIUM(buf, AM_MEDIUM, handler, numargs,
				request, nbytes, source_addr);

	GASNETC_AMHEADER_WRITE(buf, AM_MEDIUM, numargs, request);
	GASNETC_AMHANDLER_WRITE(buf+1, handler);
	GASNETC_AMLENGTH_WRITE((uint16_t *)buf + 1, nbytes);
	GASNETC_ARGS_WRITE((uint32_t *)buf + 1, argptr, numargs);
	GASNETC_AMPAYLOAD_WRITE(buf_payload, source_addr, nbytes);

	assert(GASNETC_AM_MEDIUM_HEADER_LEN(numargs)+ nbytes <= GASNETC_AM_LEN);
	return GASNETC_AM_MEDIUM_HEADER_LEN(numargs) + nbytes;
}

/* 
 * This writes a Long sized buffer header and returns the number of
 * bytes written 
 *
 * |header(1)|handler(1)|pad(2)|len(4)|dest_addr(8)|args(0..64)|payload(0..?)
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
	GASNETC_AMDESTADDR_WRITE((uint32_t *)buf + 2, dest_addr); 
	GASNETC_ARGS_WRITE((uint32_t *)buf + 4, argptr, numargs);

	assert(GASNETC_AM_LONG_HEADER_LEN(numargs) <= GASNETC_AM_LEN);
	return GASNETC_AM_LONG_HEADER_LEN(numargs);
}

GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferBulk)
void
gasnetc_write_AMBufferBulk(void *dest, void *src, size_t nbytes)
{
	assert(nbytes >= 0);
	GASNETC_AMPAYLOAD_WRITE(dest, src, nbytes);
	return;
}

GASNET_INLINE_MODIFIER(gasnetc_gm_nodes_search)
gasnet_node_t
gasnetc_gm_nodes_search(gasnetc_bufdesc_t *bufd)
{
	uint16_t		sender_node_id;
	gasnetc_gm_nodes_rev_t	*gm_node;

	if (!bufd->e) GASNETI_RETURN_ERRR(BAD_ARG, "No GM receive event");
	sender_node_id = gm_ntoh_u16(bufd->e->recv.sender_node_id);
	if (!sender_node_id) GASNETI_RETURN_ERRR(BAD_ARG, 
						"No GM sender_node_id");
	gm_node = (gasnetc_gm_nodes_rev_t *)
		bsearch((void *) &sender_node_id, 
			(const void *) _gmc.gm_nodes_rev,
			(size_t) gasnetc_nodes,
			sizeof(gasnetc_gm_nodes_rev_t),
			gasnetc_gm_nodes_compare);
	if_pf(gm_node == NULL)
		GASNETI_RETURN_ERRR(RESOURCE, "GM id unknown to job! fatal");
	return gm_node->node;
}

#endif
