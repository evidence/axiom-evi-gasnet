/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core_fwd.h $
 * Description: GASNet header for axiom conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#include <stdint.h>

#define GASNET_CORE_VERSION      0.1
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         AXIOM
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_NAME      GASNET_CORE_NAME
#define GASNET_CONDUIT_NAME_STR  _STRINGIFY(GASNET_CONDUIT_NAME)
#define GASNET_CONDUIT_AXIOM 1

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