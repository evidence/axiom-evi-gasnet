/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_coll_pami.c,v $
 *     $Date: 2012/07/25 06:29:42 $
 * $Revision: 1.8 $
 * Description: GASNet extended collectives implementation on PAMI
 * Copyright 2012, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_coll_pami.h>

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
  case PAMI_XFER_ALLGATHER: /* Used only for gasnetc_bootstrapExchange() */
    envvar = "GASNET_PAMI_ALLGATHER_ALG";
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
  case PAMI_XFER_SCATTER: /* Used for blocking gasnet scatter */
    envvar = "GASNET_PAMI_SCATTER_ALG";
    dfltval = "I0:Binomial:"; /* uniformly "good" on BG/Q and PERSC */
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
        if (!gasneti_mynode && !print_once[(int)op]) {
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
int gasnete_use_pami_bcast = 0;
int gasnete_use_pami_gathr = 0;
int gasnete_use_pami_scatt = 0;
pami_xfer_t gasnete_op_template_bcast;
pami_xfer_t gasnete_op_template_gathr;
pami_xfer_t gasnete_op_template_scatt;

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

    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_GATHR", 1)) {
      memset(&gasnete_op_template_gathr, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_GATHER, &gasnete_op_template_gathr.algorithm);
      gasnete_op_template_gathr.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_gathr.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_gathr.cmd.xfer_gather.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_gathr.cmd.xfer_gather.rtype = PAMI_TYPE_BYTE;
      gasnete_use_pami_gathr = 1;
    }

    if (gasneti_getenv_yesno_withdefault("GASNET_USE_PAMI_SCATT", 1)) {
      memset(&gasnete_op_template_scatt, 0, sizeof(pami_xfer_t));
      gasnetc_dflt_coll_alg(gasnetc_world_geom, PAMI_XFER_SCATTER, &gasnete_op_template_scatt.algorithm);
      gasnete_op_template_scatt.cb_done = &gasnetc_cb_inc_uint; /* XXX: do we need release semantics? */
      gasnete_op_template_scatt.options.multicontext = PAMI_HINT_DISABLE;
      gasnete_op_template_scatt.cmd.xfer_scatter.stype = PAMI_TYPE_BYTE;
      gasnete_op_template_scatt.cmd.xfer_scatter.rtype = PAMI_TYPE_BYTE;
      gasnete_use_pami_scatt = 1;
    }

    /* etc. */
  }
}

extern void gasnete_coll_team_init_pami(gasnet_team_handle_t team) {
  #if GASNET_PAR
    team->pami.local_dst = NULL;
    team->pami.barrier_phase = 0;
    gasneti_atomic_set(&team->pami.barrier_counter[0], team->my_images, 0);
    gasneti_atomic_set(&team->pami.barrier_counter[1], team->my_images, 0);
  #endif
}

#endif /* GASNET_PAMI_NATIVE_COLL */
