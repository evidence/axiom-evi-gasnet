/* $Id: gasnet_core_internal.h,v 1.53 2004/01/05 16:20:01 bonachea Exp $
 * $Date: 2004/01/05 16:20:01 $
 * $Revision: 1.53 $
 * Description: GASNet gm conduit header for internal definitions in Core API
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_extended_internal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
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
  #include <sys/types.h> /* mmap on FreeBSD */
  #undef MIN
  #undef MAX 
  #endif
#include <sys/param.h>
#endif

extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes)     \
	    gasneti_boundscheck(node,ptr,nbytes,c)
#define gasnetc_alloca(nbytes) alloca(nbytes)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* -------------------------------------------------------------------------- */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)
/* -------------------------------------------------------------------------- */
/* Core locks */
extern gasneti_mutex_t	gasnetc_lock_gm;
extern gasneti_mutex_t	gasnetc_lock_reqpool;
extern gasneti_mutex_t	gasnetc_lock_amreq;
/* -------------------------------------------------------------------------- */
/* Core-specific AMs */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_am_medcopy		(GASNETC_HANDLER_BASE+0) 
#ifdef GASNETC_FIREHOSE
#define _hidx_gasnetc_firehose_move_reqh	(GASNETC_HANDLER_BASE+1) 
#define _hidx_gasnetc_firehose_move_reph	(GASNETC_HANDLER_BASE+2) 
#endif
#define _hidx_					(GASNETC_HANDLER_BASE+)

/* -------------------------------------------------------------------------- */
typedef struct gasnetc_bufdesc gasnetc_bufdesc_t;
typedef void (*gasnetc_handler_fn_t)();

gasnetc_bufdesc_t * 	gasnetc_AMRequestPool_block();

int	gasnetc_gm_nodes_compare(const void *, const void *);

void	gasnetc_AllocPinnedBufs();
void	gasnetc_DestroyPinnedBufs();

int	gasnetc_alloc_nodemap(int);
int	gasnetc_gmport_allocate(int *board, int *port);

/* 3 bootstrapping methods */
void	gasnetc_getconf_conffile();
void	gasnetc_getconf_mpiexec();
void	gasnetc_getconf();

uintptr_t 	gasnetc_getPhysMem();
void		gasnetc_am_medcopy(gasnet_token_t token, void *addr, 
				   size_t nbytes, void *dest);

int	gasnetc_AMReplyLongTrySend(gasnetc_bufdesc_t *bufd);
int	gasnetc_AMReplyLongAsyncM(gasnet_token_t token, gasnet_handler_t handler, 
				  void *source_addr, size_t nbytes, void *dest_addr, 
				  int numargs, ...);

void	gasnetc_GMSend_bufd(gasnetc_bufdesc_t *bufd);
void	gasnetc_GMSend_AMSystem(void *buf, size_t len, uint16_t id, 
				uint16_t port, void *context);
void	gasnetc_GMSend_AMRequest(void *, uint32_t, uint32_t, uint32_t, 
		gm_send_completion_callback_t, void *, uintptr_t);

void	gasnetc_dump_tokens();

void	gasnetc_bootstrapBarrier();
void	gasnetc_bootstrapExchange(void *src, size_t len, void *dest);

/* GM Callback functions */
void	gasnetc_callback_error(gm_status_t status, const char *msg);

void	gasnetc_callback_system      (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_ambuffer    (struct gm_port *, void *, gm_status_t);

void	gasnetc_callback_lo          (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_lo_rdma     (struct gm_port *, void *, gm_status_t);

void	gasnetc_callback_hi          (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_hi_rdma     (struct gm_port *, void *, gm_status_t);


/* -------------------------------------------------------------------------- */
/*
 * These are GM tokens, represented by the type
 * gasnetc_token_t (not to be mistaken with gasnet_token_t
 * which is the user interface to the AMReply opaque type)
 *
 */
typedef
struct gasnetc_token {
	int	max;
	int	hi;
	int	lo;
	int	total;
}
gasnetc_token_t;

/* Buffer descriptor.  Each DMA-pinned AM buffer has one
 * of these attached to it. */
#define BUFD_REPLY	0x01
#define BUFD_PAYLOAD	0x02
#define BUFD_DMA	0x04
#define BUFD_REQMEDIUM	0x08

/* The buffer descriptor has various states, each describing where it is
 * located.  It can either be in the GM AMReceive queue, GM AMReply queue, GM
 * AMReceive send queue, or temporarily selected as the AMRequest Medium
 * buffer.
 */
#define BUFD_S_AMREQ	0x10
#define BUFD_S_GMREQ	0x20
#define BUFD_S_GMREPLY	0x30
#define BUFD_S_TEMPMED	0x40
#define BUFD_S_USED	0x50

#define BUFD_SETSTATE(b,s)	((b)->state = (s))
#define BUFD_ISSTATE(b)		((b)->state)

#define BUFD_ISSET(b,o)	((b)->flag & (o))
#define BUFD_SET(b,f)	((b)->flag |= (f))
#define BUFD_UNSET(b,f)	((b)->flag &= ~(f))
#define BUFD_RESET(b)	((b)->flag = 0)

struct gasnetc_bufdesc {
	void	*buf;		/* map to buffer */
	short	id;		/* reverse map in bufdesc list */
	uint8_t	flag;		/* bufdesc flags as defined above */
	uint8_t	state;

	/* Some AMs use firehose requests */
	const firehose_request_t	*local_req;
	const firehose_request_t	*remote_req;

	/* AMReply/AMRequest fields */
	gasnet_node_t	node;

	/* AMReply only fields */
	int		*ran_reply;
	int		*locked_AMMedBuf;

	uintptr_t	dest_addr;	/* directed_send address */
	uintptr_t	source_addr;	/* used only in Async AMs */
	uint16_t	gm_id;
	uint16_t	gm_port;
	off_t		payload_off;	/* payload offset for AMLong */
	uint32_t	payload_len;	/* payload length for AMLong */
	uint32_t	len;		/* length for queued sends */

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
	uint16_t	id;	/* sort key #1 */
	uint16_t	port;	/* sort key #2 */
	gasnet_node_t	node;
} gasnetc_gm_nodes_rev_t;

/* Global GM Core type */
typedef
struct _gasnetc_state {
	gasnetc_token_t		stoks;
	gasnetc_token_t		rtoks;

	gasnetc_handler_fn_t	handlers[GASNETC_AM_MAX_HANDLERS];
	gasnetc_gm_nodes_t	*gm_nodes;
	gasnetc_gm_nodes_rev_t	*gm_nodes_rev;

	gasnetc_bufdesc_t	*AMMedBuf;
	gasnetc_bufdesc_t	*bd_ptr;
	int			bd_list_num;
	void			*dma_bufs;	/* All DMA bufs */

	/* AMRequest send Pool */
	int		*reqs_pool;
	int		reqs_pool_max;
	volatile int	reqs_pool_cur;

	/* FIFO overflow send queue */
	gasnetc_bufdesc_t	*fifo_bd_head;
	gasnetc_bufdesc_t	*fifo_bd_tail;

	/* Bootstrap parameters for TCP-assisted bootstrap */
	unsigned int	master_port;	/* GM port for master */
	unsigned int	my_id;
	unsigned int	my_port;
	unsigned int	my_board;
	unsigned long	job_magic;	/* job magic */
	uintptr_t	pinnable_local;
	uintptr_t	pinnable_global;

	struct sockaddr_in	master_addr;
	struct sockaddr_in	slave_addr;


	void		*reqsbuf;	/* DMAd portion of send buffers */
	struct gm_port	*port;		/* GM port structure */

#ifdef GASNETC_FIREHOSE
	uintptr_t	fh_M;		/* size of M parameter */
	unsigned long	firehoses;	/* number of per-node firehoses */
	uintptr_t	fh_maxvictim;	/* size of MaxVictim parameter */
#endif

} gasnetc_state_t;	

extern gasnetc_state_t	_gmc;

/* -------------------------------------------------------------------------- */
GASNET_INLINE_MODIFIER(gasnetc_portid)
uint16_t
gasnetc_portid(gasnet_node_t node)
{
	gasneti_assert(node < gasnetc_nodes);
	return _gmc.gm_nodes[node].port;
}

GASNET_INLINE_MODIFIER(gasnetc_nodeid)
uint16_t
gasnetc_nodeid(gasnet_node_t node)
{
	gasneti_assert(node < gasnetc_nodes);
	return _gmc.gm_nodes[node].id;
}

/* -------------------------------------------------------------------------- */
/* The following function and macro definitions are related to token
 * operations on GM.  It is implicit that the caller *should* always
 * own the GM lock before calling them.
 */
#define GASNETC_TOKEN_HI_NUM()	(_gmc.stoks.max-1 - _gmc.stoks.hi)
#define GASNETC_TOKEN_HI_AVAILABLE() \
				((_gmc.stoks.hi < _gmc.stoks.max-1) && \
				 (_gmc.stoks.total < _gmc.stoks.max))
#define GASNETC_TOKEN_LO_NUM()	(_gmc.stoks.max-1 - _gmc.stoks.lo)
#define GASNETC_TOKEN_LO_AVAILABLE() \
				((_gmc.stoks.lo < _gmc.stoks.max-1) && \
				 (_gmc.stoks.total < _gmc.stoks.max))

/* HI TOKENS
 * acquire() version simply try to get a token, call must be locked around GM
 *           mutex
 * poll()    wraps acquire around a gasnetc_AMPoll loop and returns only when a
 *           hi token could be obtained
 */
GASNET_INLINE_MODIFIER(gasnetc_token_hi_acquire)
int
gasnetc_token_hi_acquire()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if (GASNETC_TOKEN_HI_AVAILABLE()) {
		GASNETI_TRACE_PRINTF(C, ("acquire token_hi = %d, tot=%d\n", 
			_gmc.stoks.hi, _gmc.stoks.total));
		gasneti_assert(_gmc.stoks.hi >= 0 && _gmc.stoks.total >= 0);
		_gmc.stoks.hi++;
		_gmc.stoks.total++;
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
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	GASNETI_TRACE_PRINTF(C, ("release token_hi = %d, tot=%d\n", _gmc.stoks.hi, _gmc.stoks.total));
	gasneti_assert((_gmc.stoks.hi-1 >= 0) && (_gmc.stoks.total-1 >= 0));
	_gmc.stoks.hi--;
	_gmc.stoks.total--;
}

GASNET_INLINE_MODIFIER(gasnetc_token_lo_acquire)
int
gasnetc_token_lo_acquire()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if (GASNETC_TOKEN_LO_AVAILABLE()) {
		_gmc.stoks.lo += 1;
		_gmc.stoks.total += 1;
		return 1;
	}
	else {
		return 0;
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_lo_poll)
void
gasnetc_token_lo_poll()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	while (1) {
		if (gasnetc_token_lo_acquire())
			return;

		gasneti_mutex_unlock(&gasnetc_lock_gm);
		gasnetc_AMPoll();
		gasneti_mutex_lock(&gasnetc_lock_gm);
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_lo_release)
void
gasnetc_token_lo_release()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gasneti_assert((_gmc.stoks.lo-1 >= 0) && (_gmc.stoks.total-1 >= 0));
	_gmc.stoks.lo -= 1;
	_gmc.stoks.total -= 1;
}

/* -------------------------------------------------------------------------- */
/*
 * Special case to handle AMReplies from AMRequestMedium, in which
 * case the original bufd must be substituted to the AMMedBuf.
 * This causes Replies originating from AMRequestMedium to be serialized
 * until they can be sent out.
 */
GASNET_INLINE_MODIFIER(gasnetc_bufdesc_from_token)
gasnetc_bufdesc_t *
gasnetc_bufdesc_from_token(gasnet_token_t token)
{
	gasnetc_bufdesc_t *bufd;

	bufd = (gasnetc_bufdesc_t *) token;
	gasneti_assert(bufd != NULL);
	gasneti_assert(bufd->gm_id > 0);

	gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_USED);
	/* Set the reply bit, so that the buffer isn't given back after the
	 * AMRequest is run */
	*(bufd->ran_reply) = 1;

	if (BUFD_ISSET(bufd, BUFD_REQMEDIUM)) {
		gasneti_mutex_lock(&gasnetc_lock_amreq);
		*(bufd->locked_AMMedBuf) = 1;

		GASNETI_TRACE_PRINTF(C, 
		    ("AMMedium LOCK: (bufd %p,id=%d -> AMMedBuf %p,id=%d)",
		    (void *) bufd, bufd->id, (void *) _gmc.AMMedBuf,
		    _gmc.AMMedBuf->id));

		_gmc.AMMedBuf->gm_id = bufd->gm_id;
		_gmc.AMMedBuf->gm_port = bufd->gm_port;
		BUFD_SETSTATE(_gmc.AMMedBuf, BUFD_S_USED);
		BUFD_RESET(_gmc.AMMedBuf);
		BUFD_SET(_gmc.AMMedBuf, BUFD_REPLY);
		return _gmc.AMMedBuf;
	}
	else {
		BUFD_SET(bufd, BUFD_REPLY);
		return bufd;
	}
}
/* -------------------------------------------------------------------------- */
/* GM provide receive buffer wrapper */
GASNET_INLINE_MODIFIER(gasnetc_relinquish_AMReply_token)
void
gasnetc_relinquish_AMReply_token()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	_gmc.rtoks.hi--;
	gasneti_assert(_gmc.rtoks.hi >= 0);
}
GASNET_INLINE_MODIFIER(gasnetc_relinquish_AMRequest_token)
void
gasnetc_relinquish_AMRequest_token()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	_gmc.rtoks.lo--;
	gasneti_assert(_gmc.rtoks.lo >= 0);
}

GASNET_INLINE_MODIFIER(gasnetc_reset_bufdesc)
void
gasnetc_reset_bufdesc(gasnetc_bufdesc_t *bufd)
{
	bufd->dest_addr = 0;
	bufd->source_addr = 0;
	bufd->payload_off = 0;
	bufd->payload_len = 0;
	bufd->len = 0;
	bufd->remote_req = NULL;
	bufd->local_req = NULL;

	BUFD_RESET(bufd);
	gasneti_assert(bufd->buf != NULL);
	return;
}

GASNET_INLINE_MODIFIER(gasnetc_provide_AMMedium)
void
gasnetc_provide_AMMedium(gasnetc_bufdesc_t *bufd)
{
	gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_USED);
	BUFD_SETSTATE(bufd, BUFD_S_TEMPMED);
	BUFD_RESET(bufd);

	GASNETI_TRACE_PRINTF(C, 
	    ("AMMedium UNLOCK: AMMedBuf=%p,id=%d,flags=0x%x",
	    (void *) bufd, bufd->id, bufd->flag));
	_gmc.AMMedBuf = bufd;
	gasneti_mutex_unlock(&gasnetc_lock_amreq);
}

GASNET_INLINE_MODIFIER(gasnetc_provide_AMReply)
void
gasnetc_provide_AMReply(gasnetc_bufdesc_t *bufd)
{
	void	*buf	= bufd->buf;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	GASNETC_ASSERT_BUFDESC_PTR(GASNETC_BUFDESC_PTR(buf),buf);
	GASNETI_TRACE_PRINTF(C, ("GM AMReply Receive buffer given %p,id=%d\n", 
				buf, bufd->id));
	BUFD_SETSTATE(bufd, BUFD_S_GMREPLY);
	gasnetc_reset_bufdesc(bufd);
	gm_provide_receive_buffer_with_tag(_gmc.port, buf, GASNETC_AM_SIZE,
			GM_HIGH_PRIORITY, 1);
	_gmc.rtoks.hi++;
	gasneti_assert(_gmc.rtoks.hi <= _gmc.rtoks.max);
}

GASNET_INLINE_MODIFIER(gasnetc_provide_AMRequest)
void
gasnetc_provide_AMRequest(gasnetc_bufdesc_t *bufd)
{
	void	*buf	= bufd->buf;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	GASNETC_ASSERT_BUFDESC_PTR(GASNETC_BUFDESC_PTR(buf),buf);
	GASNETI_TRACE_PRINTF(C, 
	    ("GM AMRequest Receive buffer given %p,id=%d\n", buf, bufd->id));

	gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_USED);

	BUFD_SETSTATE(bufd, BUFD_S_GMREQ);
	gasnetc_reset_bufdesc(bufd);
	gm_provide_receive_buffer_with_tag(_gmc.port, buf, GASNETC_AM_SIZE,
			GM_LOW_PRIORITY, 2);
	_gmc.rtoks.lo++;
	gasneti_assert(_gmc.rtoks.lo <= _gmc.rtoks.max);
}

GASNET_INLINE_MODIFIER(gasnetc_provide_AMRequestPool)
void
gasnetc_provide_AMRequestPool(gasnetc_bufdesc_t *bufd)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_USED);
	BUFD_SETSTATE(bufd, BUFD_S_AMREQ);
	gasnetc_reset_bufdesc(bufd);

	gasneti_mutex_lock(&gasnetc_lock_reqpool);
	gasneti_assert(_gmc.reqs_pool_cur < _gmc.reqs_pool_max);
	_gmc.reqs_pool_cur++;
	_gmc.reqs_pool[_gmc.reqs_pool_cur] = bufd->id;
	gasneti_mutex_unlock(&gasnetc_lock_reqpool);

	GASNETI_TRACE_PRINTF(C, 
	    ("GM AMRequestPool buffer %d given %p,id=%d\n", 
	     _gmc.reqs_pool_cur, bufd->buf, bufd->id));

}
/* -------------------------------------------------------------------------- */
/* FIFO related operations for sending AMReplies */
#define gasnetc_fifo_head()	_gmc.fifo_bd_head

GASNET_INLINE_MODIFIER(gasnetc_fifo_remove)
void
gasnetc_fifo_remove()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gasneti_assert(_gmc.fifo_bd_head != NULL);
	gasneti_assert(_gmc.fifo_bd_tail != NULL);

	if (_gmc.fifo_bd_head == _gmc.fifo_bd_tail)
		_gmc.fifo_bd_head = _gmc.fifo_bd_tail = NULL;
	else 
		_gmc.fifo_bd_head = _gmc.fifo_bd_head->next;
}

GASNET_INLINE_MODIFIER(gasnetc_fifo_insert)
void
gasnetc_fifo_insert(gasnetc_bufdesc_t *bufd)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gasneti_assert(bufd != NULL);
	gasneti_assert(bufd->gm_id > 0);
	bufd->next = NULL;
	if ((_gmc.fifo_bd_head == NULL) || (_gmc.fifo_bd_tail == NULL))
		_gmc.fifo_bd_head = _gmc.fifo_bd_tail = bufd;
	else {
		_gmc.fifo_bd_tail->next = bufd;
		_gmc.fifo_bd_tail = bufd;
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
	gasnetc_bufdesc_t *bufd;
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	while (gasnetc_fifo_head() && GASNETC_TOKEN_HI_AVAILABLE()) {
                gasnetc_bufdesc_t *bufd;
          
		gasnetc_token_hi_acquire();
		bufd = gasnetc_fifo_head();

		gasneti_assert(bufd->gm_id > 0);

		gasnetc_GMSend_bufd(bufd);

		/* If there was a payload, leave the bufdesc in the fifo and
		 * let the next loop iteration send the header
		 */
		if (BUFD_ISSET(bufd, BUFD_PAYLOAD))
			BUFD_UNSET(bufd, BUFD_PAYLOAD | BUFD_DMA);
		else
			gasnetc_fifo_remove();
	}
}

/* -------------------------------------------------------------------------- */
/* AM buffer preparation functions */
/* 
 * This writes a Short sized buffer and returns the number of
 * bytes written in total to the buffer
 *
 * |header(1)|handler(1)|pad(2)|args(0..64)
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferShort)
uint32_t
gasnetc_write_AMBufferShort(	void *buf,
				gasnet_handler_t handler, int numargs, 
				va_list argptr, int req)
{
	uint8_t *pbuf = (uint8_t *)buf;

	GASNETC_ASSERT_AMSHORT(buf, GASNETC_AM_SHORT, handler, numargs, req);
	GASNETC_AMHEADER_WRITE(pbuf, GASNETC_AM_SHORT, numargs, req);
	GASNETC_AMHANDLER_WRITE(&pbuf[1], handler);
	GASNETC_ARGS_WRITE(&pbuf[GASNETC_AM_SHORT_ARGS_OFF], argptr, numargs);
	gasneti_assert(GASNETC_AM_SHORT_HEADER_LEN(numargs) <= GASNETC_AM_PACKET);
	return GASNETC_AM_SHORT_HEADER_LEN(numargs);
}

/* 
 * This writes a Medium sized buffer and returns the number of
 * bytes written in total to the buffer
 *
 * |header(1)|handler(1)|len(2)|args(0..64)|pad(0/4)|payload(0..?)
 *
 * pad depends on the number of arguments.  If even, the pad will be
 * 4, or else 0.
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferMedium)
uint32_t
gasnetc_write_AMBufferMedium(	void *buf,
				gasnet_handler_t handler,
				int numargs, va_list argptr, 
				size_t nbytes,
				void *source_addr,
				int req)
{
	uint8_t *pbuf = (uint8_t *)buf;

	GASNETC_ASSERT_AMMEDIUM(buf, GASNETC_AM_MEDIUM, handler, numargs,
				req, nbytes, source_addr);

	GASNETC_AMHEADER_WRITE(pbuf, GASNETC_AM_MEDIUM, numargs, req);
	GASNETC_AMHANDLER_WRITE(&pbuf[1], handler);
	GASNETC_AMLENGTH_WRITE(&pbuf[2], (uint16_t) nbytes);
	GASNETC_ARGS_WRITE(&pbuf[GASNETC_AM_MEDIUM_ARGS_OFF], argptr, numargs);
	GASNETC_AMPAYLOAD_WRITE(&pbuf[GASNETC_AM_MEDIUM_HEADER_LEN(numargs)], 
	    source_addr, nbytes);

	gasneti_assert(GASNETC_AM_MEDIUM_HEADER_LEN(numargs)+nbytes <= 
			GASNETC_AM_PACKET);
	return GASNETC_AM_MEDIUM_HEADER_LEN(numargs)+nbytes;
}

/* 
 * This writes a Medium sized Medcopy-header into a buffer (typically
 * a large buffer).  It handles both 32-bit and 64-bit pointers differently
 * as we are packing the 'destination' pointer in 1 or 2 AM args.
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferMediumMedcopy)
uint32_t
gasnetc_write_AMBufferMediumMedcopy(	
		void *buf,
		void *source_addr,
		size_t nbytes,
		void *dest_addr,
		int req)
{
	uint8_t *pbuf = (uint8_t *)buf;
	gasnet_handler_t handler = gasneti_handleridx(gasnetc_am_medcopy);
	size_t numargs = GASNETC_ARGPTR_NUM;

	GASNETC_ASSERT_AMMEDIUM(buf, GASNETC_AM_MEDIUM, handler, numargs, req,
		nbytes, source_addr);

	GASNETC_AMHEADER_WRITE(pbuf, GASNETC_AM_MEDIUM, numargs, req);
	GASNETC_AMHANDLER_WRITE(&pbuf[1], handler);
	GASNETC_AMLENGTH_WRITE(&pbuf[2], (uint16_t) nbytes);
	GASNETC_ARGPTR(&pbuf[GASNETC_AM_MEDIUM_ARGS_OFF], dest_addr);
	GASNETC_AMPAYLOAD_WRITE(&pbuf[GASNETC_AM_MEDIUM_HEADER_LEN(numargs)], 
	    source_addr, nbytes);

	gasneti_assert(GASNETC_AM_MEDIUM_HEADER_LEN(numargs)+nbytes <= 
			GASNETC_AM_PACKET);
	return GASNETC_AM_MEDIUM_HEADER_LEN(numargs)+nbytes;
}

/* 
 * This writes a Long sized buffer header and returns the number of
 * bytes written 
 *
 * |header(1)|handler(1)|pad(2)|len(4)|dest_addr(4/8)|args(0..64)|payload(0..?)
 *
 * pad depends on the number of arguments.  If even, the pad will be
 * 4, or else 0.
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferLong)
uint32_t
gasnetc_write_AMBufferLong(void *buf, gasnet_handler_t handler, int numargs, 
		va_list argptr, size_t nbytes, void *source_addr, 
		uintptr_t dest_addr, int req)
{
	uint8_t *pbuf = (uint8_t *)buf;

	GASNETC_ASSERT_AMLONG(buf, GASNETC_AM_LONG, handler, numargs,
			req, nbytes, source_addr, dest_addr);
	GASNETC_AMHEADER_WRITE(pbuf, GASNETC_AM_LONG, numargs, req);
	GASNETC_AMHANDLER_WRITE(&pbuf[1], handler);
	GASNETC_AMLENGTH_WRITE4(&pbuf[4], nbytes);
	GASNETC_AMDESTADDR_WRITE(&pbuf[8], dest_addr); 
	GASNETC_ARGS_WRITE(&pbuf[GASNETC_AM_LONG_ARGS_OFF], argptr, numargs);

	gasneti_assert(GASNETC_AM_LONG_HEADER_LEN(numargs) <= GASNETC_AM_PACKET);
	return GASNETC_AM_LONG_HEADER_LEN(numargs);
}

GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferBulk)
void
gasnetc_write_AMBufferBulk(void *dest, void *src, size_t nbytes)
{
	gasneti_assert(nbytes >= 0);
	GASNETC_AMPAYLOAD_WRITE(dest, src, nbytes);
	return;
}
/* -------------------------------------------------------------------------- */
/* Private access to ReplyLongAsync */
#define gasnetc_AMReplyLongAsync0(token, handler, source_addr, nbytes, token_addr) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 0)
#define gasnetc_AMReplyLongAsync1(token, handler, source_addr, nbytes, token_addr, a0) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 1, (gasnet_handlerarg_t)a0)
#define gasnetc_AMReplyLongAsync2(token, handler, source_addr, nbytes, token_addr, a0, a1) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetc_AMReplyLongAsync3(token, handler, source_addr, nbytes, token_addr, a0, a1, a2) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetc_AMReplyLongAsync4(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetc_AMReplyLongAsync5(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetc_AMReplyLongAsync6(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetc_AMReplyLongAsync7(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetc_AMReplyLongAsync8(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetc_AMReplyLongAsync9 (token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetc_AMReplyLongAsync10(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetc_AMReplyLongAsync11(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetc_AMReplyLongAsync12(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetc_AMReplyLongAsync13(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetc_AMReplyLongAsync14(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetc_AMReplyLongAsync15(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetc_AMReplyLongAsync16(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)

#endif
