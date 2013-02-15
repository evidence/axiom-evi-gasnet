/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami_bcast.c,v $
 *     $Date: 2012/07/19 03:55:00 $
 * $Revision: 1.1 $
 * Description: GASNet extended collectives implementation on PAMI
 * Copyright 2012, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_coll_pami.h>

#if GASNET_PAMI_NATIVE_COLL

static void
gasnete_coll_pami_bcast(const gasnet_team_handle_t team, void *dst,
                        gasnet_image_t srcimage, const void *src,
                        size_t nbytes, int flags GASNETE_THREAD_FARG)
{
    volatile unsigned int done = 0;
    pami_result_t rc;
    pami_xfer_t op;

    op = gasnete_op_template_bcast;
    op.cookie = (void *)&done;
    op.cmd.xfer_broadcast.root = gasnetc_endpoint(gasnete_coll_image_node(team, srcimage));
    op.cmd.xfer_broadcast.buf  = (srcimage == team->myrank) ? (/*not-const*/ void *)src : dst;
    op.cmd.xfer_broadcast.typecount = nbytes;

    if (flags & GASNET_COLL_IN_ALLSYNC) gasnetc_fast_barrier();

    GASNETC_PAMI_LOCK(gasnetc_context);
    rc = PAMI_Collective(gasnetc_context, &op);
    GASNETC_PAMI_UNLOCK(gasnetc_context);
    GASNETC_PAMI_CHECK(rc, "initiating blocking broadcast");

    if (srcimage == team->myrank) {
      if (dst != src) GASNETE_FAST_UNALIGNED_MEMCPY(dst, src, nbytes);
    }

    rc = gasnetc_wait_uint(gasnetc_context, &done, 1);
    GASNETC_PAMI_CHECK(rc, "advancing blocking broadcast");

    if (flags & GASNET_COLL_OUT_ALLSYNC) gasnetc_fast_barrier();
}
extern void
gasnete_coll_broadcast_pami(gasnet_team_handle_t team, void *dst,
                            gasnet_image_t srcimage, void *src,
                            size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  if ((team != GASNET_TEAM_ALL) || !gasnete_use_pami_bcast) {
    /* Use generic implementation for cases we don't (yet) handle, or when disabled */
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcast_nb_default(team,dst,srcimage,src,nbytes,flags,0 GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  } else {
    /* Use PAMI-specific implementation */
    gasnete_coll_pami_bcast(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  }
}
extern void
gasnete_coll_broadcastM_pami(gasnet_team_handle_t team,
                             void * const dstlist[],
                             gasnet_image_t srcimage, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  if ((team != GASNET_TEAM_ALL) || !gasnete_use_pami_bcast) {
    /* Use generic implementation for cases we don't (yet) handle, or when disabled */
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcastM_nb_default(team,dstlist,srcimage,src,nbytes,flags,0 GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  } else {
    /* Use PAMI-specific implementation */
    void * const dst = (flags & GASNET_COLL_LOCAL) ? dstlist[0] : dstlist[team->myrank]; /* SEQ-specific */
    gasnete_coll_pami_bcast(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  }
}

#endif /* GASNET_PAMI_NATIVE_COLL */
