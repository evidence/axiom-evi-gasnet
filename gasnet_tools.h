/*  $Archive:: /Ti/GASNet/gasnet_tools.h                                   $
 *     $Date: 2003/01/04 06:16:10 $
 * $Revision: 1.3 $
 * Description: GASNet Tools library 
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#ifndef _GASNET_TOOLS_H
#define _GASNET_TOOLS_H
#define _IN_GASNET_TOOLS_H

#include <gasnet_config.h>
#include <gasnet_basic.h>

/* ------------------------------------------------------------------------------------ */
/* portable high-performance, low-overhead timers */

#include <gasnet_timer.h>

#define gasnett_tick_t               gasneti_stattime_t
#define GASNETT_TICK_MIN             GASNETI_STATTIME_MIN
#define GASNETT_TICK_MAX             GASNETI_STATTIME_MAX
#define gasnett_ticks_to_us(ticks)   GASNETI_STATTIME_TO_US(ticks)
#define gasnett_ticks_now()          GASNETI_STATTIME_NOW()

#ifdef GASNETI_USING_GETTIMEOFDAY
#define GASNETT_USING_GETTIMEOFDAY
#endif

/* ------------------------------------------------------------------------------------ */
/* portable atomic increment/decrement */

#include <gasnet_atomicops.h>

#define gasnett_atomic_t             gasneti_atomic_t
#define gasnett_atomic_read(p)       gasneti_atomic_read(p)
#define gasnett_atomic_init(v)       gasneti_atomic_init(v)
#define gasnett_atomic_set(p,v)      gasneti_atomic_set(p,v) 
#define gasnett_atomic_increment(p)  gasneti_atomic_increment(p)
#define gasnett_atomic_decrement(p)  gasneti_atomic_decrement(p)

/* portable memory barrier */

#define gasnett_local_membar()       gasneti_local_membar()

/* ------------------------------------------------------------------------------------ */

#undef _IN_GASNET_TOOLS_H
#endif
