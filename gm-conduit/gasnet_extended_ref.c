/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/Attic/gasnet_extended_ref.c,v $
 * $Date: 2013/06/29 08:37:37 $
 * $Revision: 1.27 $
 * Description: GASNet GM conduit Extended API Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

/* ------------------------------------------------------------------------------------ */
/*
  Get/Put/Memset:
  ===============
*/

/* Use only pieces of reference implementation of put/memset in terms of AMs */
/* NOTE: Barriers, Collectives, VIS may use GASNETE_USING_REF_* in algorithm selection */
#define GASNETE_USING_REF_EXTENDED_GET_BULK 0
#define GASNETE_USING_REF_EXTENDED_PUT_BULK 0
#define GASNETE_USING_REF_EXTENDED_PUT      0
#define GASNETE_USING_REF_EXTENDED_MEMSET   1 /* The one piece not customized */

/* The put code in firehose needs these: */
#define GASNETE_BUILD_AMREF_PUT_HANDLERS 1
#define GASNETE_BUILD_AMREF_PUT          1

#if GASNETE_USING_REF_EXTENDED_GET_BULK
#define GASNETE_BUILD_AMREF_GET_HANDLERS 1
#define GASNETE_BUILD_AMREF_GET_BULK     1
#define gasnete_amref_get_nb_bulk   gasnete_get_nb_bulk
#define gasnete_amref_get_nbi_bulk  gasnete_get_nbi_bulk
#endif

#if GASNETE_USING_REF_EXTENDED_PUT_BULK
#define GASNETE_BUILD_AMREF_PUT_HANDLERS 1
#define GASNETE_BUILD_AMREF_PUT_BULK     1
#define gasnete_amref_put_nb_bulk   gasnete_put_nb_bulk
#define gasnete_amref_put_nbi_bulk  gasnete_put_nbi_bulk
#endif

#if GASNETE_USING_REF_EXTENDED_PUT
#define GASNETE_BUILD_AMREF_PUT_HANDLERS 1
#define GASNETE_BUILD_AMREF_PUT     1
#define gasnete_amref_put_nb        gasnete_put_nb
#define gasnete_amref_put_nbi       gasnete_put_nbi
#endif

#if GASNETE_USING_REF_EXTENDED_MEMSET
#define GASNETE_BUILD_AMREF_MEMSET_HANDLERS 1
#define GASNETE_BUILD_AMREF_MEMSET  1
#define gasnete_amref_memset_nb     gasnete_memset_nb
#define gasnete_amref_memset_nbi    gasnete_memset_nbi
#endif
#include "gasnet_extended_amref.c"

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
*/
 
/* Timings show that gm_send is just simply faster than gm_put */
#define GASNETE_BARRIER_INIT(TEAM, BARRIER_TYPE) \
   if ((BARRIER_TYPE) == GASNETE_COLL_BARRIER_DISSEM) BARRIER_TYPE = GASNETE_COLL_BARRIER_AMDISSEM

/* reference implementation of barrier */
#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C

/* ------------------------------------------------------------------------------------ */
/*
  Vector, Indexed & Strided:
  =========================
*/

/* use reference implementation of scatter/gather and strided */
#include "gasnet_extended_refvis.h"

/* ------------------------------------------------------------------------------------ */
/*
  Collectives:
  ============
*/

/* use reference implementation of collectives */
#include "gasnet_extended_refcoll.h"

/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_ref_handlers[] = {
  #ifdef GASNETE_REFBARRIER_HANDLERS
    GASNETE_REFBARRIER_HANDLERS(),
  #endif
  #ifdef GASNETE_REFVIS_HANDLERS
    GASNETE_REFVIS_HANDLERS()
  #endif
  #ifdef GASNETE_REFCOLL_HANDLERS
    GASNETE_REFCOLL_HANDLERS()
  #endif

  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */
#if GASNETE_BUILD_AMREF_GET_HANDLERS
  gasneti_handler_tableentry_with_bits(gasnete_amref_get_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_amref_get_reph),
  gasneti_handler_tableentry_with_bits(gasnete_amref_getlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_amref_getlong_reph),
#endif
#if GASNETE_BUILD_AMREF_PUT_HANDLERS
  gasneti_handler_tableentry_with_bits(gasnete_amref_put_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_amref_putlong_reqh),
#endif
#if GASNETE_BUILD_AMREF_MEMSET_HANDLERS
  gasneti_handler_tableentry_with_bits(gasnete_amref_memset_reqh),
#endif
#if GASNETE_BUILD_AMREF_PUT_HANDLERS || GASNETE_BUILD_AMREF_MEMSET_HANDLERS
  gasneti_handler_tableentry_with_bits(gasnete_amref_markdone_reph),
#endif

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_extref_handlertable(void)
{
	return gasnete_ref_handlers;
}

