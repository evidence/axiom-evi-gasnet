/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core_fwd.h $
 * Description: GASNet header for AXIOM conduit core (forward definitions)
 *
 * Copyright (C) 2016, Evidence Srl.
 * Terms of use are as specified in COPYING
 *
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#include <stdint.h>

/*
 * if defined _BLOCKING_MODE (safest)
 * the axiom device is open in blocking mode so all the axiom user api calls are blocking i.e. if the operation can not be executed (usually caused by low resources)
 * the calling thread is blocked until the operation can be made
 *
 * if defined _NOT_BLOCKING_MODE
 * all the axiom api calls are not blocking and so if the operation can not be execute immedialty a resource_not_available error is returned (and internally managed by the conduit implementation)
 */
//#define _BLOCKING_MODE
#define _NOT_BLOCKING_MODE

/*
 * if defined _ASYNC_RDMA_MODE
 * activate the async rdma request so AMRequestLongSync use a async request
 *
 * if defined _NOT_ASYNC_RDMA_MODE (safest)
 * the AMRequestLongSync is not used and a AMRequestLong is used instead
 */
//#define _ASYNC_RDMA_MODE
#define _NOT_ASYNC_RDMA_MODE

/*
 * if defined _NOT_BLOCK_ON_LOOP
 * the standard behaviour of GASNET_BLOCKUNTIL/gasneti_pollwhile is used
 *
 * if define _BLOCK_ON_LOOP_CONDWAIT
 * the gasneti_pollwhile id modified to block using pthread_condwait (and a fast internal polling/blocking thread)
 *
 * if define _BLOCK_ON_LOOP_EPOLL
 * the gasneti_pollwhile id modified to block using linux epoll and eventfd
 *
 */
//#define _NOT_BLOCK_ON_LOOP
#define _BLOCK_ON_LOOP_CONDWAIT
//#define _BLOCK_ON_LOOP_EPOLL

//
//
//

#if defined(_BLOCKING_MODE)
#define _PRIMITIVES "blocking"
#elif defined(_NOT_BLOCKING_MODE)
#define _PRIMITIVES "not_blocking"
#else
#define _PRIMITIVES "unknown"
#endif

#if defined(_ASYNC_RDMA_MODE)
#define _ASYNC_RDMA "yes"
#elif defined(_NOT_ASYNC_RDMA_MODE)
#define _ASYNC_RDMA "no"
#else
#define _ASYNC_RDMA "unknown"
#endif

#if defined(_NOT_BLOCK_ON_LOOP)
#define _BLOCK_ON_LOOP "no"
#elif defined(_BLOCK_ON_LOOP_CONDWAIT)
#define _BLOCK_ON_LOOP "wait/signal"
#elif defined(_BLOCK_ON_LOOP_EPOLL)
#define _BLOCK_ON_LOOP "epoll/eventfd"
#else
#define _BLOCK_ON_LOOP "unknown"
#endif


#define GASNET_CORE_VERSION      0.11
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         AXIOM
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_NAME      GASNET_CORE_NAME
#define GASNET_CONDUIT_NAME_STR  _STRINGIFY(GASNET_CONDUIT_NAME)
#define GASNET_CONDUIT_AXIOM 1

#define GASNETC_EXTRA_CONFIG_INFO ",AXIOM_CONFIG=(low_api=" _PRIMITIVES ",async_rdma=" _ASYNC_RDMA  ",block_on_loop=" _BLOCK_ON_LOOP ")"

  /* GASNET_PSHM defined 1 if this conduit supports PSHM. leave undefined otherwise. */
#if GASNETI_PSHM_ENABLED
/* #define GASNET_PSHM 1 */
#undef GASNET_PSHM
#endif

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#if GASNETI_DISABLE_ALIGNED_SEGMENTS || GASNET_PSHM
  #define GASNET_ALIGNED_SEGMENTS   0 /* user or PSHM disabled segment alignment */
#else
  #define GASNET_ALIGNED_SEGMENTS   1
#endif

  /* define to 1 if conduit allows internal GASNet fns to issue put/get for remote
     addrs out of segment - not true when PSHM is used */
#if !GASNET_PSHM && 0
#define GASNETI_SUPPORTS_OUTOFSEGMENT_PUTGET 1
#endif


#define _GASNET_SEGINFO_T

typedef struct gasneti_seginfo_s {
    /** start of rdma memory as returned by axiom_rdma_mmap. */
    void *rdma;
    uint64_t rdmasize;
    /** start of rdma hidden buffers. */
    void *base;
    //
    //
    /** start of user rdma memory. */
    void *addr;
    /** size of user rdma memory. */
    uintptr_t size;
} gasnet_seginfo_t;

#define _GASNET_ERRORS
#define _GASNET_ERR_BASE 10000
#define GASNET_ERR_NOT_INIT             (_GASNET_ERR_BASE+1)
#define GASNET_ERR_RESOURCE             (_GASNET_ERR_BASE+2)
#define GASNET_ERR_BAD_ARG              (_GASNET_ERR_BASE+3)
#define GASNET_ERR_NOT_READY            (_GASNET_ERR_BASE+4)
#define GASNET_ERR_BARRIER_MISMATCH     (_GASNET_ERR_BASE+5)
#define GASNET_ERR_RDMA                 (_GASNET_ERR_BASE+6)
#define GASNET_ERR_RAW_MSG              (_GASNET_ERR_BASE+7)
// similar to errno EGAIN
#define GASNET_ERR_AGAIN                -42

  /* conduits should define GASNETI_CONDUIT_THREADS to 1 if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
#if 0
#define GASNETI_CONDUIT_THREADS 1
#endif

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
#if 0
#define GASNETC_USE_INTERRUPTS 1
#endif

  /* define these to 1 if your conduit supports PSHM, but cannot use the
     default interfaces. (see template-conduit/gasnet_core.c and gasnet_pshm.h)
   */
#if 0
#define GASNETC_GET_HANDLER 1
typedef gasnet_handler_t gasnetc_handler_t;
#endif
#if 0
#define GASNETC_TOKEN_CREATE 1
#endif

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define GASNETC_CONDUIT_STATS(CNT,VAL,TIME) 

#endif
