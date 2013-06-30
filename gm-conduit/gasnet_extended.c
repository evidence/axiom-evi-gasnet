/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/Attic/gasnet_extended.c,v $
 *     $Date: 2013/06/30 22:54:20 $
 * $Revision: 1.65 $
 * Description: GASNet Extended API GM Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

const gasnete_eopaddr_t	EOPADDR_NIL = { { 0xFF, 0xFF } };

/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
  Conduits may choose to override the default tuning parameters below by
  defining them in their gasnet_core_fwd.h
*/

/* the size threshold where gets/puts stop using medium messages and start
 * using longs */
#ifndef GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
#define GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD   gasnet_AMMaxMedium()
#endif

/* true if we should try to use Long replies in gets (only possible if dest
 * falls in segment) */
#ifndef GASNETE_USE_LONG_GETS
#define GASNETE_USE_LONG_GETS 1
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Extended API Common Code
  ========================
  Factored bits of extended API code common to most conduits, overridable when necessary
*/

#include "gasnet_extended_common.c"

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnete_check_config(void) {
  gasneti_check_config_postattach();

  gasneti_assert_always(GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD <= gasnet_AMMaxMedium());
  gasneti_assert_always(gasnete_eopaddr_isnil(EOPADDR_NIL));

#if 0 /* No AM-based Gets in gm-conduit */
  /* The next two ensure nbytes in AM-based Gets will fit in handler_arg_t (bug 2770) */
  gasneti_assert_always(gasnet_AMMaxMedium() <= (size_t)0xffffffff);
  gasneti_assert_always(gasnet_AMMaxLongReply() <= (size_t)0xffffffff);
#endif
}

extern void gasnete_init(void) {
    GASNETI_UNUSED_UNLESS_DEBUG
    static int firstcall = 1;
    GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
    gasneti_assert(firstcall); /*  make sure we haven't been called before */
    firstcall = 0;

	gasnete_check_config(); /* check for sanity */

	gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

	{ 
		gasnete_threaddata_t *threaddata = NULL;
		gasnete_eop_t *eop = NULL;
		#if GASNETI_MAX_THREADS > 1
			/* register first thread (optimization) */
			threaddata = gasnete_mythread(); 
		#else
			/* register only thread (required) */
			threaddata = gasnete_new_threaddata();
		#endif

		/* cause the first pool of eops to be allocated optimization */
		eop = gasnete_eop_new(threaddata);
		GASNETE_EOP_MARKDONE(eop);
		gasnete_eop_free(eop);
	}
 
  /* Initialize barrier resources */
  gasnete_barrier_init();

  /* Initialize VIS subsystem */
  gasnete_vis_init();
}
/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

/*  query an op for completeness 
 *  free it if complete
 *  returns 0 or 1 */
GASNETI_INLINE(gasnete_op_try_free)
int gasnete_op_try_free(gasnet_handle_t handle) {
	gasnete_op_t *op = (gasnete_op_t *)handle;

	gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
	if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
		gasnete_eop_t *eop = (gasnete_eop_t*)op;

		if (gasnete_eop_isdone(eop)) {
			gasneti_sync_reads();
			gasnete_eop_free(eop);
			return 1;
		}
	} else {
		gasnete_iop_t *iop = (gasnete_iop_t*)op;

		if (gasnete_iop_isdone(iop)) {
			gasneti_sync_reads();
			gasnete_iop_free(iop);
			return 1;
		}
	}
	return 0;
}

/*  query an op for completeness 
 *  free it and clear the handle if complete
 *  returns 0 or 1 */
GASNETI_INLINE(gasnete_op_try_free_clear)
int gasnete_op_try_free_clear(gasnet_handle_t *handle_p) {
	if (gasnete_op_try_free(*handle_p)) {
		*handle_p = GASNET_INVALID_HANDLE;
		return 1;
	}
	return 0;
}

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
#if 0
	/* polling now takes place in callers which needed and NOT in those which don't */
	GASNETI_SAFE(gasneti_AMPoll());
#endif

	return gasnete_op_try_free(handle) ? GASNET_OK : GASNET_ERR_NOT_READY;
}

extern int
gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles)
{
	int success = 0;
	int empty = 1;

#if 0
	/* polling for syncnb now happens in header file to avoid duplication */
	GASNETI_SAFE(gasneti_AMPoll());
#endif
	gasneti_assert(phandle);

	{	int i;
		for (i = 0; i < numhandles; i++) {
			if (phandle[i] != GASNET_INVALID_HANDLE) {
				empty = 0;
				success |= gasnete_op_try_free_clear(&phandle[i]);
			}
		}
	}

	return (success || empty) ? GASNET_OK : GASNET_ERR_NOT_READY;
}

extern int  
gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles)
{
	int success = 1;
#if 0
	/* polling for syncnb now happens in header file to avoid duplication */
	GASNETI_SAFE(gasneti_AMPoll());
#endif

	gasneti_assert(phandle);

	{	int i;
		for (i = 0; i < numhandles; i++) {
			if (phandle[i] != GASNET_INVALID_HANDLE) {
				success &= gasnete_op_try_free_clear(&phandle[i]);
			}
		}
	}

	return success ? GASNET_OK : GASNET_ERR_NOT_READY;
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
    #endif

    if (GASNETE_IOP_CNTDONE(iop,get)) {
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
  }
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
    #endif


    if (GASNETE_IOP_CNTDONE(iop,put)) {
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
/*  This implementation allows recursive access regions, although the spec does not require that */
/*  operations are associated with the most immediately enclosing access region */
extern void            gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread); /*  push an iop  */
  GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
  #if GASNET_DEBUG
    if (!allowrecursion && mythread->current_iop->next != NULL)
      gasneti_fatalerror("VIOLATION: tried to initiate a recursive NBI access region");
  #endif
  iop->next = mythread->current_iop;
  mythread->current_iop = iop;
}

extern gasnet_handle_t gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop; /*  pop an iop */
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,iop->initiated_get_cnt + iop->initiated_put_cnt);
  #if GASNET_DEBUG
    if (iop->next == NULL)
      gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion() outside access region");
  #endif
  mythread->current_iop = iop->next;
  iop->next = NULL;
  return (gasnet_handle_t)iop;
}
