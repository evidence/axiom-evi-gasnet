/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami.h,v $
 *     $Date: 2012/07/19 03:55:00 $
 * $Revision: 1.1 $
 * Description: GASNet extended collectives implementation on PAMI
 * Copyright 2012, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */


#ifndef _GASNET_COLL_PAMI_H
#define _GASNET_COLL_PAMI_H

#include <gasnet_core_internal.h>

#if GASNET_PAMI_NATIVE_COLL

#include <gasnet_extended_refcoll.h>
#include <gasnet_coll.h>
#include <gasnet_coll_internal.h>

/* Flags for enable/disable each operation: */
extern int gasnete_use_pami_bcast;

/* Partially initialized pami_xfer_t each operation: */
extern pami_xfer_t gasnete_op_template_bcast;

#endif /* GASNET_PAMI_NATIVE_COLL */

#endif /* _GASNET_COLL_PAMI_H */
