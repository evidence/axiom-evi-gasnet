/*  $Archive:: /Ti/GASNet/udp-conduit/gasnet_core_fwd.h                   $
 *     $Date: 2004/01/19 12:57:35 $
 * $Revision: 1.3 $
 * Description: GASNet header for UDP conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      1.3
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         UDP
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_UDP       1

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#ifndef GASNET_ALIGNED_SEGMENTS
  /* udp-conduit supports both aligned and un-aligned */
  #ifdef HAVE_MMAP
    #define GASNET_ALIGNED_SEGMENTS   1  
  #else
    #define GASNET_ALIGNED_SEGMENTS   0
  #endif
#endif

/* AMUDP currently only supports 256 nodes */
#define GASNET_MAXNODES 256
#define _GASNET_NODE_T
typedef uint8_t gasnet_node_t;

  /* conduits should define GASNETI_CONDUIT_THREADS to 1 if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
/* #define GASNETI_CONDUIT_THREADS 1 */

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
/* #define GASNETC_USE_INTERRUPTS 1 */

/*  override default error values to use those defined by AMUDP */
#define _GASNET_ERRORS
#define _GASNET_ERR_BASE 10000
#define GASNET_ERR_NOT_INIT             1
#define GASNET_ERR_RESOURCE             3
#define GASNET_ERR_BAD_ARG              2
#define GASNET_ERR_NOT_READY            (_GASNET_ERR_BASE+4)
#define GASNET_ERR_BARRIER_MISMATCH     (_GASNET_ERR_BASE+5)

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#define GASNETC_TRACE_FINISH()  gasnetc_trace_finish()
extern void gasnetc_trace_finish();

#define GASNETC_FATALSIGNAL_CALLBACK(sig) gasnetc_fatalsignal_callback(sig)
extern void gasnetc_fatalsignal_callback(int sig);

#endif
