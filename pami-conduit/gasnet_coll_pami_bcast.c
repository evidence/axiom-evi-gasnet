/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami_bcast.c,v $
 *     $Date: 2012/07/20 22:54:32 $
 * $Revision: 1.4 $
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
    const int i_am_root = gasnete_coll_image_is_local(team, srcimage);
    pami_result_t rc;
    pami_xfer_t op;

    op = gasnete_op_template_bcast;
    op.cookie = (void *)&done;
    op.cmd.xfer_broadcast.root = gasnetc_endpoint(gasnete_coll_image_node(team, srcimage));
    op.cmd.xfer_broadcast.buf  = i_am_root ? (/*not-const*/ void *)src : dst;
    op.cmd.xfer_broadcast.typecount = nbytes;

    GASNETC_PAMI_LOCK(gasnetc_context);
    rc = PAMI_Collective(gasnetc_context, &op);
    GASNETC_PAMI_UNLOCK(gasnetc_context);
    GASNETC_PAMI_CHECK(rc, "initiating blocking broadcast");

    if (i_am_root) {
      if (dst != src) GASNETE_FAST_UNALIGNED_MEMCPY(dst, src, nbytes);
    }

    gasneti_polluntil(done);
}

extern void
gasnete_coll_broadcast_pami(gasnet_team_handle_t team, void *dst,
                            gasnet_image_t srcimage, void *src,
                            size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  if ((team != GASNET_TEAM_ALL) || !gasnete_use_pami_bcast
#if GASNET_PAR
      || (flags & GASNET_COLL_LOCAL)
#endif
     ) {
    /* Use generic implementation for cases we don't (yet) handle, or when disabled */
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcast_nb_default(team,dst,srcimage,src,nbytes,flags,0 GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  } else {
    /* Use PAMI-specific implementation */
    int i_am_leader = gasnete_coll_pami_images_barrier(team, 1); /* XXX: over-synced ??? */

    if (i_am_leader) {
      if (flags & GASNET_COLL_IN_ALLSYNC) gasnetc_fast_barrier();
      gasnete_coll_pami_bcast(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    }
    (void) gasnete_coll_pami_images_barrier(team, 0); /* XXX: over-synced on OUT_NO? */
      
    if (flags & GASNET_COLL_OUT_ALLSYNC) {
       if (i_am_leader) gasnetc_fast_barrier();
       (void) gasnete_coll_pami_images_barrier(team, 0);
    }
  }
}

extern void
gasnete_coll_broadcastM_pami(gasnet_team_handle_t team,
                             void * const dstlist[],
                             gasnet_image_t srcimage, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  if ((team != GASNET_TEAM_ALL) || !gasnete_use_pami_bcast
#if GASNET_PAR
      || (flags & GASNET_COLL_LOCAL)
#endif
     ) {
    /* Use generic implementation for cases we don't (yet) handle, or when disabled */
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcastM_nb_default(team,dstlist,srcimage,src,nbytes,flags,0 GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  } else {
    /* Use PAMI-specific implementation */
    int i_am_leader = gasnete_coll_pami_images_barrier(team, 1); /* XXX: over-synced for IN_NO and IN_MY */

    if (i_am_leader) {
      void * const dst = GASNETE_COLL_MY_1ST_IMAGE(team, dstlist, flags);
      if (flags & GASNET_COLL_IN_ALLSYNC) gasnetc_fast_barrier();
      gasnete_coll_pami_bcast(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    #if GASNET_PAR
      /* TODO: PULL would be more cache-friendly than this PUSH.
               +PRO: It would also allow LOCAL support w/o "gathering" dst values
               -CON: It would require one additional barrier (and TLD?)
       */
      { void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(team, dstlist, 0);
        gasnete_coll_local_broadcast(team->my_images - 1, p + 1, *p, nbytes);
      }
    #endif
    }
    (void) gasnete_coll_pami_images_barrier(team, 0); /* XXX: over-synced on OUT_NO? */
      
    if (flags & GASNET_COLL_OUT_ALLSYNC) {
       if (i_am_leader) gasnetc_fast_barrier();
       (void) gasnete_coll_pami_images_barrier(team, 0);
    }
  }
}

#endif /* GASNET_PAMI_NATIVE_COLL */
