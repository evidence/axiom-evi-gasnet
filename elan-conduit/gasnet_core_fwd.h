/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_fwd.h              $
 *     $Date: 2002/07/08 13:00:33 $
 * $Revision: 1.1 $
 * Description: GASNet header for elan conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      0.1
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         ELAN
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1 

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_help.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

/* ------------------------------------------------------------------------------------ */
/* use ELAN-specific high-performance nanosecond timer */
#define GASNETC_CONDUIT_SPECIFIC_TIMERS
typedef uint64_t gasneti_stattime_t;
#define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
#define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
extern uint64_t gasnetc_clock();
#define GASNETI_STATTIME_NOW()      (gasnetc_clock())
#define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
/* ------------------------------------------------------------------------------------ */

#define GASNETC_USE_INTERRUPTS  0

#endif
