/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/elan-conduit/Attic/gasnet_core_fwd.h,v $
 *     $Date: 2006/05/23 12:42:17 $
 * $Revision: 1.26 $
 * Description: GASNet header for elan conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

GASNETI_BEGIN_EXTERNC

#define GASNET_CORE_VERSION      1.7
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         ELAN
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_ELAN      1

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1 

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define GASNETC_CONDUIT_STATS(CNT,VAL,TIME) \
        VAL(C, AMLONG_DIRECT, sz)        \
        VAL(C, AMLONG_BUFFERED, sz)      \
        VAL(C, PUT_DIRECT, sz)           \
        VAL(C, PUT_BULK_DIRECT, sz)      \
        VAL(C, PUT_BUFFERED, sz)         \
        VAL(C, PUT_BULK_BUFFERED, sz)    \
        VAL(C, PUT_AMMEDIUM, sz)         \
        VAL(C, PUT_BULK_AMMEDIUM, sz)    \
        VAL(C, PUT_AMLONG, sz)           \
        VAL(C, PUT_BULK_AMLONG, sz)      \
        VAL(C, GET_DIRECT, sz)           \
        VAL(C, GET_BUFFERED, sz)         \
        VAL(C, GET_AMMEDIUM, sz)         \
        VAL(C, GET_AMLONG, sz)         

/* get elan timers, if appropriate */
#include <gasnet_core_toolhelp.h>

#define GASNETC_TRACE_FINISH()  gasnetc_trace_finish()
extern void gasnetc_trace_finish();

#define GASNETC_FATALSIGNAL_CALLBACK(sig) gasnetc_fatalsignal_callback(sig)
extern void gasnetc_fatalsignal_callback(int sig);

  /* conduits should define GASNETI_CONDUIT_THREADS to 1 if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
/* #define GASNETI_CONDUIT_THREADS 1 */

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
/* #define GASNETC_USE_INTERRUPTS 1 */

#if defined(GASNETC_ELAN4) || PLATFORM_ARCH_32
  #define GASNETI_SUPPORTS_OUTOFSEGMENT_PUTGET 1
#endif

GASNETI_END_EXTERNC

#endif
