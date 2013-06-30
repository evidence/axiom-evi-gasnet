/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_extended_fwd.h,v $
 *     $Date: 2013/06/30 02:25:47 $
 * $Revision: 1.34 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#include <firehose_trace.h>

#define GASNET_EXTENDED_VERSION      1.13
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#if GASNET_CONDUIT_VAPI
  #define GASNET_EXTENDED_NAME         VAPI
#elif GASNET_CONDUIT_IBV
  #define GASNET_EXTENDED_NAME         IBV
#else
  #error "Exactly one of GASNET_CONDUIT_VAPI or GASNET_CONDUIT_IBV must be defined"
#endif
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)


/* Addition(s) to barrier-types enum: */
#define GASNETE_COLL_CONDUIT_BARRIERS \
        GASNETE_COLL_BARRIER_IBDISSEM

#define _GASNET_HANDLE_T
/*  an opaque type representing a non-blocking operation in-progress initiated using the extended API */
struct _gasnete_op_t;
typedef struct _gasnete_op_t *gasnet_handle_t;
#define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)
#define GASNETI_EOP_IS_HANDLE 1

  /* if conduit-internal threads may call the Extended API and/or they may run
     progress functions, then define GASNETE_CONDUIT_THREADS_USING_TD to the
     maximum COUNT of such threads to allocate space for their threaddata
   */
#if (GASNET_CONDUIT_VAPI && GASNETC_VAPI_RCV_THREAD) || \
    (GASNET_CONDUIT_IBV  && GASNETC_IBV_RCV_THREAD )
  /* The RCV thread needs a slot in the threadtable.  The CONN thread doesn't. */
  #define GASNETE_CONDUIT_THREADS_USING_TD 1
#endif

  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define GASNETE_CONDUIT_STATS(CNT,VAL,TIME)  \
        GASNETI_VIS_STATS(CNT,VAL,TIME)      \
	GASNETI_COLL_STATS(CNT,VAL,TIME)     \
	GASNETI_FIREHOSE_STATS(CNT,VAL,TIME) \
        CNT(C, DYNAMIC_THREADLOOKUP, cnt)           

#define GASNETE_AUXSEG_DECLS \
    extern gasneti_auxseg_request_t gasnete_barr_auxseg_alloc(gasnet_seginfo_t *auxseg_info);
#define GASNETE_AUXSEG_FNS() gasnete_barr_auxseg_alloc, 

/* We perform these blocking ops w/o the overhead of eop alloc/free: */
#define GASNETI_DIRECT_GET_BULK 1
#define GASNETI_DIRECT_PUT_BULK 1
#define GASNETI_DIRECT_MEMSET 1

#define GASNETE_EOP_COUNTED 1
#define GASNETE_EXTENDED_NEEDS_CORE 1

/* Stealing these unused slots: */
#define _hidx_gasnete_memset_reqh    _hidx_gasnete_amref_memset_reqh
#define _hidx_gasnete_markdone_reph  _hidx_gasnete_amref_markdone_reph

#endif

