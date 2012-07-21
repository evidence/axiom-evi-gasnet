/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami.c,v $
 *     $Date: 2012/07/21 00:23:05 $
 * $Revision: 1.3 $
 * Description: GASNet extended collectives implementation on PAMI
 * Copyright 2012, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_coll_pami.h>

#if GASNET_PAMI_NATIVE_COLL

/* These live here, not in per-op files, to avoid unwanted link dependencies */
int gasnete_use_pami_bcast = 0;
pami_xfer_t gasnete_op_template_bcast;

extern void
gasnete_coll_init_pami(void)
{
  if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_COLL", 1)) {

    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_BCAST", 1)) {
      memset(&gasnete_op_template_bcast, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_BROADCAST, &gasnete_op_template_bcast.algorithm);
      gasnete_op_template_bcast.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_bcast.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_bcast.cmd.xfer_broadcast.type = PAMI_TYPE_BYTE;
      gasnete_use_pami_bcast = 1;
    }

    /* etc. */
  }
}

extern void gasnete_coll_team_init_pami(gasnet_team_handle_t team) {
  #if GASNET_PAR
    team->pami.done = 0;
    team->pami.barrier_phase = 0;
    gasneti_atomic_set(&team->pami.barrier_counter[0], team->my_images, 0);
    gasneti_atomic_set(&team->pami.barrier_counter[1], team->my_images, 0);
  #endif
}

#endif /* GASNET_PAMI_NATIVE_COLL */
