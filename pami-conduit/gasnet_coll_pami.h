/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami.h,v $
 *     $Date: 2012/07/21 00:00:52 $
 * $Revision: 1.4 $
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

#if GASNET_PAR
  /* thread (image) barrier returning non-zero to exactly one caller */
  GASNETI_INLINE(gasnete_coll_pami_images_barrier)
  int gasnete_coll_pami_images_barrier(gasnet_team_handle_t team) {
    int phase = team->pami.barrier_phase;
    gasneti_atomic_t *counter = &team->pami.barrier_counter[phase];
    int last = gasneti_atomic_decrement_and_test(counter, GASNETI_ATOMIC_REL);
    int goal = phase ^ 1;

    if (last) {
      gasneti_atomic_set(counter, team->my_images, GASNETI_ATOMIC_WMB_POST);
      team->pami.barrier_phase = goal;
    } else {
      while ( team->pami.barrier_phase != goal ) GASNETI_WAITHOOK();
    }
    gasneti_sync_reads();

    return last;
  }
#else
  #define gasnete_coll_pami_images_barrier(_t) (1)
#endif

#endif /* GASNET_PAMI_NATIVE_COLL */

#endif /* _GASNET_COLL_PAMI_H */
