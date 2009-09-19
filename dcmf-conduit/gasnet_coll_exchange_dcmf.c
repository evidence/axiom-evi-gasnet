/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_exchange_dcmf.c,v $
 * $Date: 2009/09/16 01:13:22 $
 * $Revision: 1.2 $
 * Description: GASNet exchange (alltoall) implementation on DCMF
 * LBNL 2009
 */

#include <gasnet_coll_exchange_dcmf.h>

/* #define G_DCMF_COLL_TRACE */

static DCMF_CollectiveProtocol_t g_dcmf_a2a_protos[G_DCMF_A2A_PROTO_NUM];
unsigned int g_dcmf_a2a_enabled[G_DCMF_A2A_PROTO_NUM];

/* callback function for DCMF_Alltoallv() */
static void gasnete_dcmf_coll_cb_done(void *clientdata, DCMF_Error_t *e)
{
  unsigned *p = (unsigned *)clientdata;
  *p = 0;
}

/* alltoall protocol registration */
void gasnete_coll_a2a_proto_register()
{
  DCMF_Alltoallv_Configuration_t alltoallv_conf;
  
  GASNETC_DCMF_LOCK(); /* for DCMF_SAFE */

  g_dcmf_a2a_enabled[TORUS_ALLTOALLV] =
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_ALLTOALLV", 1);
  if (g_dcmf_a2a_enabled[TORUS_ALLTOALLV])
    {
      alltoallv_conf.protocol = DCMF_TORUS_ALLTOALLV_PROTOCOL;
      DCMF_SAFE(DCMF_Alltoallv_register(&g_dcmf_a2a_protos[TORUS_ALLTOALLV], 
                                        &alltoallv_conf));
    }

  GASNETC_DCMF_UNLOCK();
}

void gasnete_coll_dcmf_a2a_init(gasnete_dcmf_a2a_data_t *a2a,
                                DCMF_CollectiveProtocol_t *registration,
                                DCMF_Geometry_t *geometry,
                                int nprocs, 
                                void *src, 
                                void *dst,
                                size_t nbytes)

{
  int i;

#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_dcmf_a2a_init is executed.\n");
#endif 

  gasneti_assert(a2a != NULL);
  a2a->registration = registration;
  a2a->geometry = geometry;
  a2a->cb_done.function = gasnete_dcmf_coll_cb_done;
  a2a->cb_done.clientdata = &a2a->active;
  a2a->active = 1;
  a2a->consistency = DCMF_MATCH_CONSISTENCY;
  a2a->sndbuf = src;
  a2a->rcvbuf = dst;

  a2a->sndlens = (unsigned *)gasneti_malloc(nprocs*sizeof(unsigned));
  a2a->rcvlens = (unsigned *)gasneti_malloc(nprocs*sizeof(unsigned));
  a2a->sdispls = (unsigned *)gasneti_malloc(nprocs*sizeof(unsigned));
  a2a->rdispls = (unsigned *)gasneti_malloc(nprocs*sizeof(unsigned));
  a2a->sndcounters = (unsigned *)gasneti_malloc(nprocs*sizeof(unsigned));
  a2a->rcvcounters = (unsigned *)gasneti_malloc(nprocs*sizeof(unsigned));
  
  for (i = 0; i < nprocs; i++) {
    a2a->sndlens[i] = nbytes;
    a2a->sdispls[i] = i * nbytes;
    a2a->rcvlens[i] = nbytes;
    a2a->rdispls[i] = i * nbytes;
  }
}
       
void gasnete_coll_dcmf_a2a_fini(gasnete_dcmf_a2a_data_t *a2a)
{
  gasneti_assert(a2a != NULL);
#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_dcmf_a2a_fini is executed.\n");
#endif 
  gasneti_free(a2a->sndlens);
  gasneti_free(a2a->rcvlens);
  gasneti_free(a2a->sdispls);
  gasneti_free(a2a->rdispls);
  gasneti_free(a2a->sndcounters);
  gasneti_free(a2a->rcvcounters);
}

static int gasnete_coll_pf_exchg_dcmf(gasnete_coll_op_t *op GASNETE_THREAD_FARG) 
{
  gasnete_coll_generic_data_t *data = op->data;; 
  gasnete_dcmf_a2a_data_t *a2a = (gasnete_dcmf_a2a_data_t *)data->private_data;;
  static unsigned dcmf_busy = 0;  

  switch (data->state) {
  case 0:	
#ifdef G_DCMF_COLL_TRACE
    fprintf(stderr, "gasnete_coll_pf_exchg_dcmf is executed.\n");
#endif
    if (dcmf_busy)
      break;
    data->state = 1;
    
  case 1:	/* Initiate data movement */
    dcmf_busy = 1;
    /**
     * DCMF_Alltoallv has an explicit barrier at the beginning and an
     * implicit barrier at the end. see class A2AProtocol and class
     * AlltoallFactory, and function AlltoallFactory::generate().
     */
    GASNETC_DCMF_LOCK();
    DCMF_SAFE(DCMF_Alltoallv(a2a->registration,
                             &a2a->request, 
                             a2a->cb_done,
                             a2a->consistency,
                             a2a->geometry, 
                             a2a->sndbuf,
                             a2a->sndlens, 
                             a2a->sdispls, 
                             a2a->rcvbuf,
                             a2a->rcvlens, 
                             a2a->rdispls,
                             a2a->sndcounters, 
                             a2a->rcvcounters));
    GASNETC_DCMF_UNLOCK();
    data->state = 2;
    
  case 2:	/* Sync data movement */
    if (a2a->active)
      break;
    dcmf_busy = 0;
    data->state = 3;
    
  case 3: 
    if (!gasnete_coll_generic_outsync(op->team, data))
      break;

    /* clean up storage space */
    gasnete_coll_dcmf_a2a_fini(a2a);
    gasneti_free(a2a);
    gasnete_coll_generic_free(op->team, data GASNETE_THREAD_PASS);
    return (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }
  
  return 0;
}

/** Non-blocking version of gasnete_coll_exchange */
gasnet_coll_handle_t gasnete_coll_exchange_nb_dcmf(gasnet_team_handle_t team, 
                                                   void *dst, void *src, 
                                                   size_t nbytes, int flags, 
                                                   uint32_t sequence
                                                   GASNETE_THREAD_FARG)
{
  gasnete_coll_generic_data_t *data;
  int i;
  int nprocs =  team->total_ranks;
  gasnete_dcmf_a2a_data_t *a2a;
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;

  gasneti_assert(gasnete_coll_dcmf_inited == 1);
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));

#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_exchange_nb_dcmf is executed!\n");
#endif

#if GASNET_PAR
  return gasnete_coll_exchange_nb_default(team, dst, src, nbytes, flags, 
                                          sequence GASNETE_THREAD_PASS);
#endif
  
  /**
   * Check the pre-conditions of using the tours alltoallv
   * collective. If not, use the default exchange (alltoall)
   * implementation.
   */
  if(!g_dcmf_a2a_enabled[TORUS_ALLTOALLV] || nprocs < 2 
     || !(DCMF_Geometry_analyze(&dcmf_tp->geometry, 
                                &g_dcmf_a2a_protos[TORUS_ALLTOALLV])))
    {
#ifdef G_DCMF_COLL_TRACE
      fprintf(stderr, "gasnete_coll_exchange_nb_default is called.\n");
#endif
      return gasnete_coll_exchange_nb_default(team, dst, src, nbytes, flags, 
                                              sequence GASNETE_THREAD_PASS);
    }
  
  a2a = (gasnete_dcmf_a2a_data_t *)
    gasneti_malloc(sizeof(gasnete_dcmf_a2a_data_t));
  gasnete_coll_dcmf_a2a_init(a2a, &g_dcmf_a2a_protos[TORUS_ALLTOALLV], 
                             &dcmf_tp->geometry, nprocs, src, dst, nbytes);
  
  data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
  data->private_data = a2a;
  GASNETE_COLL_GENERIC_SET_TAG(data, exchange);
  data->options = 
    GASNETE_COLL_GENERIC_OPT_INSYNC_IF((flags & GASNET_COLL_IN_ALLSYNC)) |
    GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF((flags & GASNET_COLL_OUT_ALLSYNC));
  
  return gasnete_coll_op_generic_init_with_scratch(team, flags, data, 
                                                   gasnete_coll_pf_exchg_dcmf, 
                                                   sequence, NULL, 0, NULL, 
                                                   NULL GASNETE_THREAD_PASS);
}

/** Blocking version of gasnete_coll_exchange */
void gasnete_coll_exchange_dcmf(gasnet_team_handle_t team, 
                                void *dst, void *src, 
                                size_t nbytes, int flags, 
                                uint32_t sequence
                                GASNETE_THREAD_FARG)
{
  int i;
  int  nprocs = team->total_ranks;
  gasnete_dcmf_a2a_data_t * a2a;
  gasnete_coll_team_dcmf_t *dcmf_tp = 
    (gasnete_coll_team_dcmf_t *)team->dcmf_tp;

  gasneti_assert(gasnete_coll_dcmf_inited == 1);
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));

#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_exchange_nb_dcmf is executed!\n");
#endif
  
#if GASNET_PAR
  {
    gasnet_coll_handle_t handle;
#if G_DCMF_COLL_TRACE
    fprintf(stderr, "PAR: gasnete_coll_exchange_nb_default is called.\n");
#endif
    handle = gasnete_coll_exchange_nb_default(team, dst, src, nbytes, flags,
                                              0 GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
    return;
  }
#endif
  
  /**
   * Check the pre-conditions of using the tours alltoallv
   * collective. If not, use the default exchange (alltoall)
   * implementation.
   */
  if(!g_dcmf_a2a_enabled[TORUS_ALLTOALLV] || nprocs < 2 
     || !(DCMF_Geometry_analyze(&dcmf_tp->geometry, 
                                &g_dcmf_a2a_protos[TORUS_ALLTOALLV])))
    {
      gasnet_coll_handle_t handle;
#if G_DCMF_COLL_TRACE
      fprintf(stderr, "Pre-conditions failed.  gasnete_coll_exchange_nb_default is called.\n");
#endif
      handle = gasnete_coll_exchange_nb_default(team, dst, src, nbytes, flags,
                                                0 GASNETE_THREAD_PASS);
      gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
      return;
    }
  
  a2a = (gasnete_dcmf_a2a_data_t *)
    gasneti_malloc(sizeof(gasnete_dcmf_a2a_data_t));
  gasnete_coll_dcmf_a2a_init(a2a, &g_dcmf_a2a_protos[TORUS_ALLTOALLV], 
                             &dcmf_tp->geometry, nprocs, src, dst, nbytes);
  
  GASNETC_DCMF_LOCK();
  DCMF_SAFE(DCMF_Alltoallv(a2a->registration,
                           &a2a->request, 
                           a2a->cb_done,
                           a2a->consistency,
                           a2a->geometry, 
                           a2a->sndbuf,
                           a2a->sndlens, 
                           a2a->sdispls, 
                           a2a->rcvbuf,
                           a2a->rcvlens, 
                           a2a->rdispls,
                           a2a->sndcounters, 
                           a2a->rcvcounters));
  
  while (a2a->active)
    DCMF_Messager_advance();
  
  GASNETC_DCMF_UNLOCK();

  /* optional barrier for the out_allsync mode, should be a team barrier */
  if (flags & GASNET_COLL_OUT_ALLSYNC)
      gasnetc_dcmf_bootstrapBarrier();   
  
  /* clean up storage space */
  gasnete_coll_dcmf_a2a_fini(a2a);
  gasneti_free(a2a);
}

void  gasnete_coll_dcmf_a2a_print(FILE *fp, gasnete_dcmf_a2a_data_t * a2a, 
                                  int nprocs)
{
  fprintf(fp, "active flag %d\n", a2a->active);
  fprintf(fp, "registration %x\n", a2a->registration);
  fprintf(fp, "consistency %d\n", a2a->consistency);
  fprintf(fp, "geometry %x\n", a2a->geometry);
  fprintf(fp, "sndbuf %x\n", a2a->sndbuf);
  PRINT_ARRAY(fp, a2a->sndlens, nprocs, "%u");
  PRINT_ARRAY(fp, a2a->sdispls, nprocs, "%u");
  fprintf(fp, "rcvbuf %x\n",a2a->rcvbuf);
  PRINT_ARRAY(fp, a2a->rcvlens, nprocs, "%u");
  PRINT_ARRAY(fp, a2a->rdispls, nprocs, "%u");
  PRINT_ARRAY(fp, a2a->sndcounters, nprocs, "%u");
  PRINT_ARRAY(fp, a2a->rcvcounters, nprocs, "%u");
}