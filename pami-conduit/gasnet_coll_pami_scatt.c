/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami_scatt.c,v $
 *     $Date: 2012/07/25 22:24:14 $
 * $Revision: 1.4 $
 * Description: GASNet extended collectives implementation on PAMI
 * Copyright 2012, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_coll_pami.h>

#if GASNET_PAMI_NATIVE_COLL

#if GASNET_PAR
static void
gasnete_coll_pami_scattvi(const gasnet_team_handle_t team, void *dst,
                          gasnet_image_t srcimage, const void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG)
{
    const int i_am_root = gasnete_coll_image_is_local(team, srcimage);
    int i_am_leader = gasnete_coll_pami_images_barrier(team); /* XXX: over-synced for IN_NO and IN_MY */
    const gasnete_coll_threaddata_t * const td = GASNETE_COLL_MYTHREAD_NOALLOC;

    if ((flags & GASNET_COLL_LOCAL) && i_am_root) {
        /* root thread must be leader for its node */
        i_am_leader = (srcimage == td->my_image);
    }

    if (i_am_leader) {
        volatile unsigned int done = 0;
        pami_result_t rc;
        pami_xfer_t op;

        if (flags & GASNET_COLL_IN_ALLSYNC) gasnetc_fast_barrier();

        op = gasnete_op_template_scattvi; /* scatterv_int */
        op.cookie = (void *)&done;
        op.cmd.xfer_scatterv_int.root = gasnetc_endpoint(gasnete_coll_image_node(team, srcimage));
        op.cmd.xfer_scatterv_int.rcvbuf = team->pami.scratch_addr;
        op.cmd.xfer_scatterv_int.rtypecount = nbytes * team->my_images;

        if (i_am_root) {
            int i;
            op.cmd.xfer_scatterv_int.sndbuf = (/*not-const*/ void *)src;
            /* TODO: allocate space for these in the team struct? */
            op.cmd.xfer_scatterv_int.stypecounts = alloca(sizeof(int) * team->total_ranks);
            op.cmd.xfer_scatterv_int.sdispls = alloca(sizeof(int) * team->total_ranks);
            for (i = 0; i < team->total_ranks; ++i) {
                op.cmd.xfer_scatterv_int.stypecounts[i] = nbytes * team->all_images[i];
                op.cmd.xfer_scatterv_int.sdispls[i] = nbytes * team->all_offset[i];
            }
        }

        GASNETC_PAMI_LOCK(gasnetc_context);
        rc = PAMI_Collective(gasnetc_context, &op);
        GASNETC_PAMI_UNLOCK(gasnetc_context);
        GASNETC_PAMI_CHECK(rc, "initiating blocking scatterv_int");

        gasneti_polluntil(done);

        gasneti_assert(NULL == team->pami.tmp_addr);
        gasneti_sync_writes();
        team->pami.tmp_addr = team->pami.scratch_addr; /* wakes pollers, below */
    } else {
        gasneti_waitwhile(NULL == team->pami.tmp_addr);
    }

    GASNETE_FAST_UNALIGNED_MEMCPY(dst,
                                  gasnete_coll_scale_ptr(team->pami.tmp_addr,
                                                         td->my_local_image,
                                                         nbytes),
                                  nbytes);
    (void) gasnete_coll_pami_images_barrier(team);

    if (i_am_leader) {
        team->pami.tmp_addr = NULL;
    }

    if (flags & GASNET_COLL_OUT_ALLSYNC) {
        if (i_am_leader) gasnetc_fast_barrier();
        (void) gasnete_coll_pami_images_barrier(team);
    }
}
#endif

static void
gasnete_coll_pami_scatt(const gasnet_team_handle_t team, void *dst,
                        gasnet_image_t srcimage, const void *src,
                        size_t nbytes, int flags GASNETE_THREAD_FARG)
{
    const int i_am_root = gasnete_coll_image_is_local(team, srcimage);

  #if GASNET_PAR
    int i_am_leader = gasnete_coll_pami_images_barrier(team); /* XXX: over-synced for IN_NO and IN_MY */

    if ((flags & GASNET_COLL_LOCAL) && i_am_root) {
        /* root thread must be leader for its node */
        const gasnete_coll_threaddata_t * const td = GASNETE_COLL_MYTHREAD_NOALLOC;
        i_am_leader = (srcimage == td->my_image);
    }
  #else
    const int i_am_leader = 1;
  #endif

    if (i_am_leader) {
        volatile unsigned int done = 0;
        pami_result_t rc;
        pami_xfer_t op;

        if (flags & GASNET_COLL_IN_ALLSYNC) gasnetc_fast_barrier();

        op = gasnete_op_template_scatt;
        op.cookie = (void *)&done;
        op.cmd.xfer_scatter.root = gasnetc_endpoint(gasnete_coll_image_node(team, srcimage));
        op.cmd.xfer_scatter.sndbuf = (/*not-const*/ void *)src;
        op.cmd.xfer_scatter.stypecount = nbytes;
        op.cmd.xfer_scatter.rcvbuf = dst;
        op.cmd.xfer_scatter.rtypecount = nbytes;

        GASNETC_PAMI_LOCK(gasnetc_context);
        rc = PAMI_Collective(gasnetc_context, &op);
        GASNETC_PAMI_UNLOCK(gasnetc_context);
        GASNETC_PAMI_CHECK(rc, "initiating blocking scatter");

        gasneti_polluntil(done);
    }
      
    if (flags & GASNET_COLL_OUT_ALLSYNC) {
        if (i_am_leader) gasnetc_fast_barrier();
        (void) gasnete_coll_pami_images_barrier(team);
    }
}

extern void
gasnete_coll_scatter_pami(gasnet_team_handle_t team, void *dst,
                          gasnet_image_t srcimage, void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  if ((team != GASNET_TEAM_ALL) || !gasnete_use_pami_scatt
  #if GASNET_PAR
      /* TODO: could remove size restriction by segmenting/pipelining */
      || (team->multi_images_any && (nbytes*team->max_images > team->pami.scratch_size))
  #endif
     ) {
    /* Use generic implementation for cases we don't (yet) handle, or when disabled */
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_scatter_nb_default(team,dst,srcimage,src,nbytes,flags,0 GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  } else { /* Use PAMI-specific implementation: */
  #if GASNET_PAR
    if (team->multi_images_any) {
      gasnete_coll_pami_scattvi(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    } else {
      gasnete_coll_pami_scatt(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    }
  #else
    gasnete_coll_pami_scatt(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  #endif
  }
}

extern void
gasnete_coll_scatterM_pami(gasnet_team_handle_t team,
                             void * const dstlist[],
                             gasnet_image_t srcimage, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  if ((team != GASNET_TEAM_ALL) || !gasnete_use_pami_scatt
  #if GASNET_PAR
      /* TODO: could remove size restriction by segmenting/pipelining */
      || (team->multi_images_any && (nbytes*team->max_images > team->pami.scratch_size))
  #endif
     ) {
    /* Use generic implementation for cases we don't (yet) handle, or when disabled */
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_scatterM_nb_default(team,dstlist,srcimage,src,nbytes,flags,0 GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  } else { /* Use PAMI-specific implementation: */
  #if GASNET_PAR
    const gasnete_coll_threaddata_t * const td = GASNETE_COLL_MYTHREAD_NOALLOC;
    void * const dst = dstlist[((flags & GASNET_COLL_LOCAL) ? td->my_local_image : td->my_image)];
    if (team->multi_images_any) {
      gasnete_coll_pami_scattvi(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    } else {
      gasnete_coll_pami_scatt(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    }
  #else
    void * const dst = GASNETE_COLL_MY_1ST_IMAGE(team, dstlist, flags);
    gasnete_coll_pami_scatt(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  #endif
  }
}

#endif /* GASNET_PAMI_NATIVE_COLL */
