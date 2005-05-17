/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_fwd.h,v $
 *     $Date: 2005/05/17 20:42:38 $
 * $Revision: 1.27 $
 * Description: GASNet header for vapi conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

/* At least one VAPI MPI does '#define VAPI 1'.
 * This will clobber our GASNET_CORE_NAME.
 * Grumble, grumble.
 */
#ifdef VAPI
  #undef VAPI
#endif


#define GASNET_CORE_VERSION      1.4
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         VAPI
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_VAPI      1

/* This is the limit on the LID space... */
#define GASNET_MAXNODES	16384

/* Explicitly set some types because we depend on their sizes when encoding them */
#define _GASNET_NODE_T
typedef uint16_t gasnet_node_t;
#define _GASNET_HANDLER_T
typedef uint8_t gasnet_handler_t;

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define GASNETC_CONDUIT_STATS(CNT,VAL,TIME)       \
        CNT(C, AMREQUEST_SYS, cnt)                \
        CNT(C, AMREPLY_SYS, cnt)                  \
        CNT(C, AMREQUEST_SYS_HANDLER, cnt)        \
        CNT(C, AMREPLY_SYS_HANDLER, cnt)          \
        VAL(C, RDMA_PUT_INLINE, bytes)            \
        VAL(C, RDMA_PUT_BOUNCE, bytes)            \
        VAL(C, RDMA_PUT_ZEROCP, bytes)            \
        VAL(C, RDMA_GET_BOUNCE, bytes)            \
        VAL(C, RDMA_GET_ZEROCP, bytes)            \
        CNT(C, ALLOC_AM_SPARE, cnt)	          \
        VAL(C, SND_AM_CREDITS, piggybacked credits) \
        VAL(C, RCV_AM_CREDITS, piggybacked credits) \
        CNT(C, GET_AMREQ_CREDIT, cnt)             \
	TIME(C, GET_AMREQ_CREDIT_STALL, stalled time) \
	TIME(C, GET_AMREQ_BUFFER_STALL, stalled time) \
	TIME(C, AM_ROUNDTRIP_TIME, time) \
	TIME(C, RCV_THREAD_WAKE, time awake)      \
	CNT(C, GET_BBUF, cnt)                     \
	TIME(C, GET_BBUF_STALL, stalled time)     \
	CNT(C, ALLOC_SBUF, cnt)                   \
	VAL(C, POST_SR, segments)                 \
	CNT(C, POST_INLINE_SR, cnt)               \
	TIME(C, POST_SR_STALL_CQ, stalled time)   \
	TIME(C, POST_SR_STALL_SQ, stalled time)   \
	VAL(C, SND_POST_LIST, requests)           \
	VAL(C, POST_SR_LIST, requests)            \
	VAL(C, SND_REAP, reaped)                  \
	VAL(C, RCV_REAP, reaped)                  \
	TIME(C, FIREHOSE_MOVE, processing time)   \
	VAL(C, FIREHOSE_PIN, pages)               \
	VAL(C, FIREHOSE_UNPIN, pages)

/*
 * The VAPI conduit may have a network progress thread, even for GASNET_SEQ
 */
#if GASNETC_VAPI_RCV_THREAD
  #define GASNETI_CONDUIT_THREADS 1
#endif

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
/* #define GASNETC_USE_INTERRUPTS 1 */

#endif
