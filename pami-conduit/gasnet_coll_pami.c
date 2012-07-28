/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami.c,v $
 *     $Date: 2012/07/28 03:32:22 $
 * $Revision: 1.19 $
 * Description: GASNet extended collectives implementation on PAMI
 * Copyright 2012, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_coll_pami.h>
#include <limits.h> /* For INT_MAX */

/* ------------------------------------------------------------------------------------ */
/* Bootstrap collectives and dependencies */

/* Get the default algorithm for a given (geometery, collective) pair.
   This will be the first "always works" algorithm unless user provides an override.
*/
extern void
gasnetc_dflt_coll_alg(pami_geometry_t geom, pami_xfer_type_t op, pami_algorithm_t *alg_p) {
  static int print_once[PAMI_XFER_COUNT]; /* static var must initially be all zeros */
  pami_result_t rc;
  size_t counts[2];
  pami_algorithm_t *algorithms;
  pami_metadata_t *metadata;
  const char *envvar, *envval, *dfltval;
  int alg, fullcount;

  gasneti_assert(op >= 0);
  gasneti_assert(op < PAMI_XFER_COUNT);

  rc = PAMI_Geometry_algorithms_num(geom, op, counts);
  GASNETC_PAMI_CHECK(rc, "calling PAMI_Geometry_algorithms_num()");
  gasneti_assert_always(counts[0] != 0);
  fullcount = counts[0] + counts[1];

  /* Space for algorithms and metadata */
  algorithms = alloca(fullcount * sizeof(pami_algorithm_t));
  metadata   = alloca(fullcount * sizeof(pami_metadata_t));

  rc = PAMI_Geometry_algorithms_query(geom, op,
                                      algorithms, metadata, counts[0],
                                      algorithms+counts[0], metadata+counts[0], counts[1]);
  GASNETC_PAMI_CHECK(rc, "calling PAMI_Geometry_algorithms_query()");

  /* Process environment and defaults: */
  switch(op) { /* please keep alphabetical */
  case PAMI_XFER_ALLTOALL: /* Used for blocking gasnet exchange */
    envvar = "GASNET_PAMI_ALLTOALL_ALG";
    dfltval = NULL; /* TODO: tune a better default than alg[0]? */
    break;
  case PAMI_XFER_ALLTOALLV_INT: /* Used for blocking gasnet exchange w/ multiple images */
    envvar = "GASNET_PAMI_ALLTOALLV_INT_ALG";
    dfltval = NULL; /* TODO: tune a better default than alg[0]? */
    break;
  case PAMI_XFER_ALLGATHER: /* Used for blocking gasnet gatherall and gasnetc_bootstrapExchange() */
    envvar = "GASNET_PAMI_ALLGATHER_ALG";
    dfltval = NULL; /* TODO: tune a better default than alg[0]? */
    break;
  case PAMI_XFER_ALLGATHERV_INT: /* Used for blocking gasnet gatherall w/ multiple images */
    envvar = "GASNET_PAMI_ALLGATHERV_INT_ALG";
    dfltval = NULL; /* TODO: tune a better default than alg[0]? */
    break;
  case PAMI_XFER_ALLREDUCE: /* Used for exitcode reduction and "PAMIALLREDUCE" barrier */
    envvar = "GASNET_PAMI_ALLREDUCE_ALG";
    dfltval = "I0:Binomial:"; /* uniformly "good" on BG/Q and PERCS */
    break;
  case PAMI_XFER_BARRIER: /* Used for gasnetc_fast_barrier() */
    envvar = "GASNET_PAMI_BARRIER_ALG";
    dfltval = NULL; /* TODO: tune a better default than alg[0]? */
    break;
  case PAMI_XFER_BROADCAST: /* Used for blocking gasnet broadcast */
    envvar = "GASNET_PAMI_BROADCAST_ALG";
    dfltval = "I0:Binomial:"; /* uniformly "good" on BG/Q and PERSC */
    break;
  case PAMI_XFER_GATHER: /* Used for blocking gasnet scatter gather */
    envvar = "GASNET_PAMI_GATHER_ALG";
    dfltval = NULL; /* TODO: tune for better default */
    break;
  case PAMI_XFER_GATHERV_INT: /* Used for blocking gasnet gather w/ multiple images */
    envvar = "GASNET_PAMI_GATHERV_INT_ALG";
    dfltval = NULL; /* TODO: tune for better default */
    break;
  case PAMI_XFER_SCATTER: /* Used for blocking gasnet scatter */
    envvar = "GASNET_PAMI_SCATTER_ALG";
    dfltval = "I0:Binomial:"; /* uniformly "good" on BG/Q and PERSC */
    break;
  case PAMI_XFER_SCATTERV_INT: /* Used for blocking gasnet scatter w/ multiple images */
    envvar = "GASNET_PAMI_SCATTERV_INT_ALG";
    dfltval = NULL; /* TODO: tune for better default */
    break;
  default:
    gasneti_fatalerror("Unknown 'op' value %d in %s", (int)op, __FUNCTION__);
    envvar = dfltval = NULL; /* for warning suppression only */
  }
  envval = gasneti_getenv_withdefault(envvar, dfltval);
  alg = 0; /* failsafe */
  if (NULL != envval) {
    while (envval[0] && isspace(envval[0])) ++envval; /* leading whitespace */
    if (!envval[0]) {
      /* empty - treat as zero */
    } else if (0 == strcmp("LIST", envval)) {
      if (!gasneti_mynode && !print_once[(int)op]) {
        int i;
        fprintf(stderr, "Listing available values for environment variable %s:\n", envvar);
        for (i=0; i<fullcount; ++i) {
          fprintf(stderr, " %c %3d %s\n", ((i<counts[0])?' ':'*'), i, metadata[i].name);
        }
        if (counts[1]) {
          fprintf(stderr,
                  "Note: Lines marked with '*' may not be valid for all inputs and/or job layouts.\n"
                  "      The user is responsible for ensuring only valid algorithms are requested.\n"
                 );
        }
        print_once[(int)op] = 1;
      }
    } else if (isdigit(envval[0])) {
      /* integer is used just as given */
      alg = atoi(envval);
      if (alg < 0 || alg >= fullcount) {
        if (!gasneti_mynode && !print_once[(int)op]) {
          fprintf(stderr, "WARNING: Ignoring value '%d' for environment variable %s,\n"
                          "         because it is outside the range of available algorithms.\n"
                          "         Set this variable to LIST for a list of all algorithms.\n",
                           alg, envvar);
          print_once[(int)op] = 1;
        }
        alg = 0;
      }
    } else {
      /* string is used for PREFIX match */
      size_t len = strlen(envval);
      for (alg=0; alg<fullcount; ++alg) {
        if (0 == strncmp(envval, metadata[alg].name, len)) {
          break;
        }
      }
      if (alg == fullcount) {
        if (!gasneti_mynode && !print_once[(int)op] && (envval != dfltval)) {
          fprintf(stderr, "WARNING: Ignoring value '%s' for environment variable %s,\n"
                          "         because it does not match any available algorithm.\n"
                          "         Set this variable to LIST for a list of all algorithms.\n",
                           envval, envvar);
          print_once[(int)op] = 1;
        }
        alg = 0;
      }
    }
  }

  *alg_p = algorithms[alg];
}

static void bootstrap_collective(pami_xfer_t *op_p) {
  pami_result_t rc;
  volatile unsigned int counter = 0;

  op_p->cb_done = &gasnetc_cb_inc_uint;
  op_p->cookie = (void *)&counter;
  op_p->options.multicontext = PAMI_HINT_DISABLE;

  rc = PAMI_Collective(gasnetc_context, op_p);
  GASNETC_PAMI_CHECK(rc, "initiating a bootstrap collective");

  rc = gasnetc_wait_uint(gasnetc_context, &counter, 1);
  GASNETC_PAMI_CHECK(rc, "polling a bootstrap collective");
}

extern void
gasnetc_fast_barrier(void) {
  static pami_xfer_t op;
  static int is_init = 0;

  if_pf (!is_init) {
    memset(&op, 0, sizeof(op)); /* Shouldn't need for static, but let's be safe */
    gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_BARRIER, &op.algorithm);
    is_init = 1;
  }

  bootstrap_collective(&op);
}
#define gasnetc_bootstrapBarrier gasnetc_fast_barrier

extern void
gasnetc_bootstrapExchange(void *src, size_t len, void *dst) {
  static pami_xfer_t op;
  static int is_init = 0;

  if_pf (!is_init) {
    memset(&op, 0, sizeof(op)); /* Shouldn't need for static, but let's be safe */
    gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_ALLGATHER, &op.algorithm);
    is_init = 1;
  }

  op.cmd.xfer_allgather.sndbuf     = src;
  op.cmd.xfer_allgather.stype      = PAMI_TYPE_BYTE;
  op.cmd.xfer_allgather.stypecount = len;
  op.cmd.xfer_allgather.rcvbuf     = dst;
  op.cmd.xfer_allgather.rtype      = PAMI_TYPE_BYTE;
  op.cmd.xfer_allgather.rtypecount = len; /* times gasneti_nodes */

  bootstrap_collective(&op);
}

/* ------------------------------------------------------------------------------------ */
/* Native collectives */

#if GASNET_PAMI_NATIVE_COLL

/* These live here, not in per-op files, to avoid unwanted link dependencies */
int gasnete_use_pami_allga = 0;
int gasnete_use_pami_allto = 0;
int gasnete_use_pami_bcast = 0;
int gasnete_use_pami_gathr = 0;
int gasnete_use_pami_scatt = 0;
pami_xfer_t gasnete_op_template_allga;
pami_xfer_t gasnete_op_template_allto;
pami_xfer_t gasnete_op_template_bcast;
pami_xfer_t gasnete_op_template_gathr;
pami_xfer_t gasnete_op_template_scatt;
#if GASNET_PAR
pami_xfer_t gasnete_op_template_allgavi;
pami_xfer_t gasnete_op_template_alltovi;
pami_xfer_t gasnete_op_template_gathrvi;
pami_xfer_t gasnete_op_template_scattvi;
#endif

static size_t scratch_size;

extern void
gasnete_coll_init_pami(void)
{
  if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_COLL", 1)) {
    scratch_size = gasneti_getenv_int_withdefault("GASNET_PAMI_COLL_SCRATCH", 4096, 1);
    /* We use the int-type scatterv and gatherv on the assumption of reasonable sizes. */
    if (scratch_size > (size_t)INT_MAX) scratch_size = INT_MAX;

    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_GATHERALL", 1)) {
      memset(&gasnete_op_template_allga, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_ALLGATHER, &gasnete_op_template_allga.algorithm);
      gasnete_op_template_allga.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_allga.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_allga.cmd.xfer_allgather.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_allga.cmd.xfer_allgather.rtype = PAMI_TYPE_BYTE;
    #if GASNET_PAR
      memset(&gasnete_op_template_allgavi, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_ALLGATHERV_INT, &gasnete_op_template_allgavi.algorithm);
      gasnete_op_template_allgavi.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_allgavi.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_allgavi.cmd.xfer_allgatherv_int.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_allgavi.cmd.xfer_allgatherv_int.rtype = PAMI_TYPE_BYTE;
    #endif
      gasnete_use_pami_allga = 1;
    }

    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_EXCHANGE", 1)) {
      memset(&gasnete_op_template_allto, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_ALLTOALL, &gasnete_op_template_allto.algorithm);
      gasnete_op_template_allto.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_allto.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_allto.cmd.xfer_alltoall.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_allto.cmd.xfer_alltoall.rtype = PAMI_TYPE_BYTE;
    #if GASNET_PAR
      memset(&gasnete_op_template_alltovi, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_ALLTOALLV_INT, &gasnete_op_template_alltovi.algorithm);
      gasnete_op_template_alltovi.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_alltovi.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_alltovi.cmd.xfer_alltoallv_int.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_alltovi.cmd.xfer_alltoallv_int.rtype = PAMI_TYPE_BYTE;
    #endif
      gasnete_use_pami_allto = 1;
    }

    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_BROADCAST", 1)) {
      memset(&gasnete_op_template_bcast, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_BROADCAST, &gasnete_op_template_bcast.algorithm);
      gasnete_op_template_bcast.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_bcast.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_bcast.cmd.xfer_broadcast.type = PAMI_TYPE_BYTE;
      gasnete_use_pami_bcast = 1;
    }

  #if GASNETI_ARCH_IBMPE /* Bug in PE1207 has been reported to IBM */
    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_GATHER", 0)) {
  #else
    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_GATHER", 1)) {
  #endif
      memset(&gasnete_op_template_gathr, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_GATHER, &gasnete_op_template_gathr.algorithm);
      gasnete_op_template_gathr.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_gathr.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_gathr.cmd.xfer_gather.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_gathr.cmd.xfer_gather.rtype = PAMI_TYPE_BYTE;
    #if GASNET_PAR
      memset(&gasnete_op_template_gathrvi, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_GATHERV_INT, &gasnete_op_template_gathrvi.algorithm);
      gasnete_op_template_gathrvi.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_gathrvi.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_gathrvi.cmd.xfer_gatherv_int.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_gathrvi.cmd.xfer_gatherv_int.rtype = PAMI_TYPE_BYTE;
    #endif
      gasnete_use_pami_gathr = 1;
    }

    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_SCATTER", 1)) {
      memset(&gasnete_op_template_scatt, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_SCATTER, &gasnete_op_template_scatt.algorithm);
      gasnete_op_template_scatt.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_scatt.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_scatt.cmd.xfer_scatter.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_scatt.cmd.xfer_scatter.rtype = PAMI_TYPE_BYTE;
    #if GASNET_PAR
      memset(&gasnete_op_template_scattvi, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_SCATTERV_INT, &gasnete_op_template_scattvi.algorithm);
      gasnete_op_template_scattvi.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_scattvi.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_scattvi.cmd.xfer_scatterv_int.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_scattvi.cmd.xfer_scatterv_int.rtype = PAMI_TYPE_BYTE;
    #endif
      gasnete_use_pami_scatt = 1;
    }

    /* etc. */
  }
}

extern void gasnete_coll_team_init_pami(gasnet_team_handle_t team) {
  #if GASNET_PAR
    team->pami.scratch_rcvbuf = gasneti_malloc(scratch_size);
    team->pami.scounts = gasneti_malloc(4 * sizeof(int) * team->total_ranks);
    team->pami.sdispls = team->pami.scounts + team->total_ranks;
    team->pami.prev_sndsz = 0;

    team->pami.scratch_sndbuf = gasneti_malloc(scratch_size);
    team->pami.rcounts = team->pami.sdispls + team->total_ranks;
    team->pami.rdispls = team->pami.rcounts + team->total_ranks;
    team->pami.prev_rcvsz = 0;

    team->pami.scratch_size = scratch_size;
    team->pami.tmp_addr = NULL;
    team->pami.barrier_phase = 0;
    gasneti_atomic_set(&team->pami.barrier_counter[0], team->my_images, 0);
    gasneti_atomic_set(&team->pami.barrier_counter[1], team->my_images, 0);
  #endif
}

#endif /* GASNET_PAMI_NATIVE_COLL */
