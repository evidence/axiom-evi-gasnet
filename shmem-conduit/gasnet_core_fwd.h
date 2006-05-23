/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/shmem-conduit/gasnet_core_fwd.h,v $
 *     $Date: 2006/05/23 12:42:37 $
 * $Revision: 1.12 $
 * Description: GASNet header for shmem conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      1.7
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         SHMEM
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_SHMEM 1

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   0 

  /* conduits should define GASNETI_CONDUIT_THREADS to 1 if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
/* #define GASNETI_CONDUIT_THREADS 1 */

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
/* #define GASNETC_USE_INTERRUPTS 1 */

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define GASNETC_CONDUIT_STATS(CNT,VAL,TIME) 

#define _GASNET_NODE_T
typedef uint32_t        gasnet_node_t;
#define _GASNET_HANDLER_T
typedef uint32_t        gasnet_handler_t;

#define _GASNET_TOKEN_T
typedef uintptr_t    gasnet_token_t;

/*#if !defined(GASNET_SEGMENT_EVERYTHING) && (defined(SGI_SHMEM) || PLATFORM_ARCH_CRAYX1)*/
#if (defined(SGI_SHMEM) || PLATFORM_ARCH_CRAYX1)
  #define GASNETC_GLOBAL_ADDRESS
  #define GASNETE_GLOBAL_ADDRESS
#else
  #undef GASNETC_GLOBAL_ADDRESS
  #undef GASNETE_GLOBAL_ADDRESS
#endif

/* -------------------------------------------------------------------- */
/*
 * These settings are based on benchmarks executed over various implementations
 * of shmem, and can be reproduced by the shmem_core.c file in contrib/
 */
/*
 * Quadrics has higher latency and is hence more sensitive to remote atomic
 * opeartions.  Turns out randomly chosing an index in the shared queue
 * provides much better performance without impacting performance under high
 * contention.
 */
#ifdef QUADRICS_SHMEM
  #define GASNETC_VECTORIZE

/*
 * Cray does very well with the mswap operation, which essentially allows us to
 * reduce the unsuccessful AMPoll case to a single word read (if queue <= 64).
 */
#elif defined(CRAY_SHMEM) 
  #define GASNETC_VECTORIZE		_Pragma("_CRI concurrent")
  #define GASNETE_SHMEM_BARRIER

/* 
 * SGI does not implement shmem_int_mswap (even though it exists in the header
 * file!).  We use the put-based mechanism instead.
 */
#elif defined(SGI_SHMEM)
  #define GASNETC_VECTORIZE
  #define GASNETE_SHMEM_BARRIER
#endif

#ifdef SGI_SHMEM
  /* tweak auxseg allocation to ensure client seg remains power-of-two aligned */
  #define GASNETI_AUXSEG_PRESERVE_POW2_FULLSEGSZ 1
  #define GASNETI_FORCE_CLIENTSEG_TO_BASE 1
#endif

#define GASNETI_GASNETC_AMPOLL
extern int _gasnetc_AMPoll(int replyonly);
#define gasnetc_AMPoll()   _gasnetc_AMPoll(0)

#endif
