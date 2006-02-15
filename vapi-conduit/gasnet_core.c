/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core.c,v $
 *     $Date: 2006/02/15 01:17:12 $
 * $Revision: 1.158 $
 * Description: GASNet vapi conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <sys/time.h>
#include <sys/resource.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

GASNETI_IDENT(gasnetc_IdentString_HaveSSHSpawner, "$GASNetSSHSpawner: 1 $");
#if HAVE_MPI_SPAWNER
  GASNETI_IDENT(gasnetc_IdentString_HaveMPISpawner, "$GASNetMPISpawner: 1 $");
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Configuration
  ==============
*/

/* Minimum number of pages to reserve for firehoses in SEGMENT_FAST: */
#define GASNETC_MIN_FH_PAGES		4096

/*
  The following values can be overridden by environment variables.
  Variable names are formed by replacing GASNETC_DEFAULT_ by GASNET_
*/

/* Default is to open one physical port per HCA */
#define GASNETC_DEFAULT_VAPI_PORTS		""

/* Limits on in-flight (queued but not reaped) RDMA Ops */
#define GASNETC_DEFAULT_NETWORKDEPTH_TOTAL	0	/* Max ops (RMDA + AM) outstanding at source, 0 = automatic */
#define GASNETC_DEFAULT_NETWORKDEPTH_PP		64	/* Max ops (RMDA + AM) outstanding to each peer */

/* Limits on in-flight (queued but not acknowledged) AM Requests */
#define GASNETC_DEFAULT_AM_CREDITS_TOTAL	MAX(1024,gasneti_nodes)	/* Max AM requests outstanding at source, 0 = automatic */
#define GASNETC_DEFAULT_AM_CREDITS_PP		32	/* Max AM requests outstanding to each peer */
#define GASNETC_DEFAULT_AM_CREDITS_SLACK	1	/* Max AM credits delayed by coalescing */

/* Limit on prepinned send bounce buffers */
#define GASNETC_DEFAULT_BBUF_COUNT		1024	/* Max bounce buffers prepinned, 0 = automatic */

/* Limit on size of prepinned regions */
#define GASNETC_DEFAULT_PIN_MAXSZ	(256*1024)

/* Use of rcv thread */
#ifndef GASNETC_DEFAULT_RCV_THREAD
  #define GASNETC_DEFAULT_RCV_THREAD	GASNETC_VAPI_RCV_THREAD
#elif GASNETC_DEFAULT_RCV_THREAD && !GASNETC_VAPI_RCV_THREAD
  #error "GASNETC_DEFAULT_RCV_THREAD and GASNETC_VAPI_RCV_THREAD conflict"
#endif

/* Use of multiple QPs */
#define GASNETC_DEFAULT_NUM_QPS			0	/* 0 = one per HCA */

/* Protocol switch points */
#define GASNETC_DEFAULT_INLINESEND_LIMIT	72
#define GASNETC_DEFAULT_NONBULKPUT_BOUNCE_LIMIT	(64*1024)
#define GASNETC_DEFAULT_PACKEDLONG_LIMIT	GASNETC_MAX_PACKEDLONG
#if !GASNETC_PIN_SEGMENT
  #define GASNETC_DEFAULT_PUTINMOVE_LIMIT	GASNETC_PUTINMOVE_LIMIT_MAX
#endif

/*
  These calues cannot yet be overridden by environment variables.
*/
#define GASNETC_QP_PATH_MTU		MTU1024
#define GASNETC_QP_STATIC_RATE		0
#define GASNETC_QP_MIN_RNR_TIMER	IB_RNR_NAK_TIMER_0_08
#define GASNETC_QP_RNR_RETRY		7	/* retry forever, but almost never happens */
#define GASNETC_QP_TIMEOUT		18	/* about 1s */
#define GASNETC_QP_RETRY_COUNT		7

#ifndef MT_MELLANOX_IEEE_VENDOR_ID
  #define MT_MELLANOX_IEEE_VENDOR_ID      0x02c9
#endif

/* ------------------------------------------------------------------------------------ */

int		gasnetc_num_hcas = 1;
gasnetc_hca_t	gasnetc_hca[GASNETC_VAPI_MAX_HCAS];
gasnetc_cep_t	*gasnetc_cep;
uintptr_t	gasnetc_max_msg_sz;

#if GASNETC_PIN_SEGMENT
  int			gasnetc_seg_reg_count;
  int			gasnetc_max_regs;
  uintptr_t		gasnetc_seg_start;
  uintptr_t		gasnetc_seg_end;
  unsigned long		gasnetc_pin_maxsz;
  int			gasnetc_pin_maxsz_shift;
#endif
firehose_info_t	gasnetc_firehose_info;

/* Used only once, to exchange addresses at connection time */
typedef struct _gasnetc_addr_t {
  IB_lid_t	lid;
  VAPI_qp_num_t	qp_num;
} gasnetc_addr_t;

gasnet_handlerentry_t const *gasnetc_get_handlertable();

int		gasnetc_op_oust_limit;
int		gasnetc_op_oust_pp;
int		gasnetc_am_oust_limit;
int		gasnetc_am_oust_pp;
int		gasnetc_bbuf_limit;

/* Maximum pinning capabilities of the HCA */
typedef struct gasnetc_pin_info_t_ {
    uintptr_t	memory;		/* How much pinnable (per proc) */
    uint32_t	regions;
    int		num_local;	/* How many procs */
} gasnetc_pin_info_t;
static gasnetc_pin_info_t gasnetc_pin_info;

gasnetc_handler_fn_t const gasnetc_unused_handler = (gasnetc_handler_fn_t)&abort;
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

static void gasnetc_atexit(void);
static void gasnetc_exit_sighandler(int sig);

static void (*gasneti_bootstrapFini_p)(void);
static void (*gasneti_bootstrapAbort_p)(int exitcode);
static void (*gasneti_bootstrapBarrier_p)(void);
static void (*gasneti_bootstrapExchange_p)(void *src, size_t len, void *dest);
static void (*gasneti_bootstrapAlltoall_p)(void *src, size_t len, void *dest);
static void (*gasneti_bootstrapBroadcast_p)(void *src, size_t len, void *dest, int rootnode);
#define gasneti_bootstrapFini		(*gasneti_bootstrapFini_p)	
#define gasneti_bootstrapAbort		(*gasneti_bootstrapAbort_p)	
#define gasneti_bootstrapBarrier	(*gasneti_bootstrapBarrier_p)	
#define gasneti_bootstrapExchange	(*gasneti_bootstrapExchange_p)	
#define gasneti_bootstrapAlltoall	(*gasneti_bootstrapAlltoall_p)	
#define gasneti_bootstrapBroadcast	(*gasneti_bootstrapBroadcast_p)	

static char *gasnetc_vapi_ports;

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  gasneti_check_config_preinit();

  gasneti_assert_always(offsetof(gasnetc_medmsg_t,args) == GASNETC_MEDIUM_HDRSZ);
  gasneti_assert_always(offsetof(gasnetc_longmsg_t,args) == GASNETC_LONG_HDRSZ);
}

extern void gasnetc_unpin(gasnetc_memreg_t *reg) {
  VAPI_ret_t vstat;

  vstat = VAPI_deregister_mr(reg->hca_hndl, reg->handle);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_deregister_mr()");
}

extern VAPI_ret_t gasnetc_pin(gasnetc_hca_t *hca, void *addr, size_t size, VAPI_mrw_acl_t acl, gasnetc_memreg_t *reg) {
  VAPI_mr_t	mr_in;
  VAPI_mr_t	mr_out;
  VAPI_ret_t	vstat;

  mr_in.type    = VAPI_MR;
  mr_in.start   = (uintptr_t)addr;
  mr_in.size    = size;
  mr_in.pd_hndl = hca->pd;
  mr_in.acl     = acl;

  vstat = VAPI_register_mr(hca->handle, &mr_in, &reg->handle, &mr_out);

  reg->hca_hndl = hca->handle;
  reg->lkey     = mr_out.l_key;
  reg->rkey     = mr_out.r_key;
  reg->addr     = mr_out.start;
  reg->len      = mr_out.size;
  reg->end      = mr_out.start + (mr_out.size - 1);
  reg->req_addr = addr;
  reg->req_size = size;

  return vstat;
}

static void *gasnetc_try_pin_inner(size_t size, gasnetc_memreg_t *reg) {
  VAPI_ret_t vstat;
  void *addr;
  int h;

  addr = gasneti_mmap(size);
  if (addr != MAP_FAILED) {
    GASNETC_FOR_ALL_HCA_INDEX(h) {
      gasnetc_hca_t *hca = &gasnetc_hca[h];
      vstat = gasnetc_pin(hca, addr, size, 0, &reg[h]);
      if (vstat != VAPI_OK) {
	for (h -= 1; h >= 0; --h) {
          gasnetc_unpin(&reg[h]);
	}
        gasneti_munmap(addr, size);
        return NULL;
      }
    }
  } else {
    addr = NULL;
  }

  return addr;
}

/* Try to pin up to 'limit' in chunks of size 'step' */
static uintptr_t gasnetc_trypin(uintptr_t limit, uintptr_t step) {
  uintptr_t size = 0;
  int h;

  if (limit != 0) {
    gasnetc_memreg_t reg[GASNETC_VAPI_MAX_HCAS];
    step = MIN(limit, step);
    if (gasnetc_try_pin_inner(step, reg) != NULL) {
      size = step + gasnetc_trypin(limit - step, step);
      GASNETC_FOR_ALL_HCA_INDEX(h) {
        gasnetc_unpin(&reg[h]);
      }
      gasneti_munmap(reg[0].req_addr, reg[0].req_size);
    }
  }

  return size;
}

/* Reproduce the mmap()/munmap() steps to keep compatible VM spaces */
static void gasnetc_fakepin(uintptr_t limit, uintptr_t step) {
  if (limit != 0) {
    void *addr;
    step = MIN(limit, step);
    addr = gasneti_mmap(step);
    if (addr != MAP_FAILED) {
      gasnetc_fakepin(limit - step, step);
      gasneti_munmap(addr, step);
    }
  }
}

/* Search for the total amount of memory we can pin per process.
 */
static void gasnetc_init_pin_info(int first_local, int num_local) {
  gasnetc_pin_info_t *all_info = gasneti_malloc(gasneti_nodes * sizeof(gasnetc_pin_info_t));
  unsigned long limit;
  int do_probe = 1;
  int i;

  /* 
   * We bound our search by the smallest of:
   *   2/3 of physical memory (1/4 or 1GB for Darwin)
   *   User's current (soft) mlock limit (optional)
   *   env(GASNET_PHYSMEM_MAX)
   *   if FIREHOSE_M and FIREHOSE_MAXVICTIM_M are both set:
   *     (SEGMENT_FAST ? MMAP_LIMIT : 0 ) + (FIREHOSE_M + FIREHOSE_MAXVICTIM_M + eplison)
   */

#if defined(__APPLE__)
  /* Note bug #532: Pin requests >= 1GB kill Cluster X nodes */
  limit = MIN((gasneti_getPhysMemSz(1) / 4) - 1, 0x3fffffff /*1GB-1*/);
#else
  limit = 2 * (gasneti_getPhysMemSz(1) / 3);
#endif
  #if defined(RLIMIT_MEMLOCK) && GASNETC_HONOR_RLIMIT_MEMLOCK
  { /* Honor soft mlock limit (build-time option) */
    struct rlimit r;
    if ((getrlimit(RLIMIT_MEMLOCK, &r) == 0) && (r.rlim_cur != RLIM_INFINITY)) {
      limit = MIN(limit, r.rlim_cur);
    }
  }
  #endif
  { /* Honor Firehose params if set */
    unsigned long fh_M = gasneti_parse_int(gasnet_getenv("GASNET_FIREHOSE_M"),(1<<20));
    unsigned long fh_VM = gasneti_parse_int(gasnet_getenv("GASNET_FIREHOSE_MAXVICTIM_M"),(1<<20));
    if (fh_M && fh_VM) {
      #if GASNETC_PIN_SEGMENT
	limit = MIN(limit, (fh_M + fh_VM + GASNETI_MMAP_LIMIT + GASNETI_MMAP_GRANULARITY));
      #else
	limit = MIN(limit, (fh_M + fh_VM + GASNETI_MMAP_GRANULARITY));
      #endif
    }
  }
  { /* Honor PHYSMEM_MAX if set */
    int tmp = gasneti_getenv_int_withdefault("GASNET_PHYSMEM_MAX", 0, 1);
    if (tmp) {
      limit = MIN(limit, tmp);
      if_pf (gasneti_getenv_yesno_withdefault("GASNET_PHYSMEM_NOPROBE", 0)) {
	/* Force use of PHYSMEM_MAX w/o probing */
	limit = tmp;
	do_probe = 0;
      }
    }
  }

  if_pf (limit == 0) {
    gasneti_fatalerror("Failed to determine the available physical memory");
  }

  gasnetc_pin_info.memory    = ~((uintptr_t)0);
  gasnetc_pin_info.num_local = num_local;
  gasnetc_pin_info.regions = gasnetc_hca[0].hca_cap.max_num_mr;
  for (i = 1; i < gasnetc_num_hcas; ++i) {
    gasnetc_pin_info.regions = MIN(gasnetc_pin_info.regions, gasnetc_hca[i].hca_cap.max_num_mr);
  }

  if (do_probe) {
    /* Now search for largest pinnable memory, on one process per machine */
    unsigned long step = GASNETI_MMAP_GRANULARITY;
    #if GASNETC_PIN_SEGMENT
      step = MAX(step, gasnetc_pin_maxsz);
    #endif
    if (gasneti_mynode == first_local) {
      uintptr_t size = gasnetc_trypin(limit, step);
      if_pf (!size) {
        gasneti_fatalerror("ERROR: Failure to determine the max pinnable memory.  VAPI may be misconfigured.");
      }
      gasnetc_pin_info.memory = size;
    }
    gasneti_bootstrapExchange(&gasnetc_pin_info, sizeof(gasnetc_pin_info_t), all_info);
    if (gasneti_mynode != first_local) {
      /* Extra mmap traffic to ensure compatible VM spaces */
      gasnetc_fakepin(all_info[first_local].memory, step);
    }
  } else {
    /* Note that README says PHYSMEM_NOPROBE must be equal on all nodes */
    gasnetc_pin_info.memory = limit;
    gasneti_bootstrapExchange(&gasnetc_pin_info, sizeof(gasnetc_pin_info_t), all_info);
  }

  /* Determine the global values (min of maxes) from the local values */
  for (i = 0; i < gasneti_nodes; i++) {
    gasnetc_pin_info_t *info = &all_info[i];

    info->memory = GASNETI_PAGE_ALIGNDOWN(info->memory / info->num_local);
    info->regions /= info->num_local;

    gasnetc_pin_info.memory  = MIN(gasnetc_pin_info.memory,  info->memory );
    gasnetc_pin_info.regions = MIN(gasnetc_pin_info.regions, info->regions);
  }
  gasneti_free(all_info);
}

/* Process defaults and the environment to get configuration settings */
static int gasnetc_load_settings(void) {
  const char *tmp;

  tmp =  gasneti_getenv("GASNET_HCA_ID");
  if (tmp && strlen(tmp)) {
    fprintf(stderr, "WARNING: GASNET_HCA_ID set in environment, but ignored.  See gasnet/vapi-conduit/README.\n");
  }
  tmp =  gasneti_getenv("GASNET_PORT_NUM");
  if (tmp && strlen(tmp) && atoi(tmp)) {
    fprintf(stderr, "WARNING: GASNET_PORT_NUM set in environment, but ignored.  See gasnet/vapi-conduit/README.\n");
  }

  gasnetc_vapi_ports = gasneti_getenv_withdefault("GASNET_VAPI_PORTS", GASNETC_DEFAULT_VAPI_PORTS);

  #define GASNETC_ENVINT(program_var, env_key, default_val, minval, is_mem) do {     \
      int64_t _tmp = gasneti_getenv_int_withdefault(#env_key, default_val, is_mem);  \
      if (_tmp < minval)                                                             \
        GASNETI_RETURN_ERRR(BAD_ARG, "("#env_key" < "#minval") in environment");     \
      program_var = _tmp;                                                            \
    } while (0)

  GASNETC_ENVINT(gasnetc_op_oust_pp, GASNET_NETWORKDEPTH_PP, GASNETC_DEFAULT_NETWORKDEPTH_PP, 1, 0);
  GASNETC_ENVINT(gasnetc_op_oust_limit, GASNET_NETWORKDEPTH_TOTAL, GASNETC_DEFAULT_NETWORKDEPTH_TOTAL, 0, 0);
  GASNETC_ENVINT(gasnetc_am_oust_pp, GASNET_AM_CREDITS_PP, GASNETC_DEFAULT_AM_CREDITS_PP, 1, 0);
  GASNETC_ENVINT(gasnetc_am_oust_limit, GASNET_AM_CREDITS_TOTAL, GASNETC_DEFAULT_AM_CREDITS_TOTAL, 0, 0);
  GASNETC_ENVINT(gasnetc_am_credits_slack, GASNET_AM_CREDITS_SLACK, GASNETC_DEFAULT_AM_CREDITS_SLACK, 0, 0);
  GASNETC_ENVINT(gasnetc_bbuf_limit, GASNET_BBUF_COUNT, GASNETC_DEFAULT_BBUF_COUNT, 0, 0);
  GASNETC_ENVINT(gasnetc_num_qps, GASNET_NUM_QPS, GASNETC_DEFAULT_NUM_QPS, 0, 0);
  GASNETC_ENVINT(gasnetc_inline_limit, GASNET_INLINESEND_LIMIT, GASNETC_DEFAULT_INLINESEND_LIMIT, 0, 1);
  GASNETC_ENVINT(gasnetc_bounce_limit, GASNET_NONBULKPUT_BOUNCE_LIMIT, GASNETC_DEFAULT_NONBULKPUT_BOUNCE_LIMIT, 0, 1);
  GASNETC_ENVINT(gasnetc_packedlong_limit, GASNET_PACKEDLONG_LIMIT, GASNETC_DEFAULT_PACKEDLONG_LIMIT, 0, 1);

  #if GASNETC_PIN_SEGMENT
  { long tmp;

    GASNETC_ENVINT(gasnetc_pin_maxsz, GASNET_PIN_MAXSZ, GASNETC_DEFAULT_PIN_MAXSZ, GASNET_PAGESIZE, 1);
    if_pf (!GASNETI_POWEROFTWO(gasnetc_pin_maxsz)) {
      gasneti_fatalerror("GASNET_PIN_MAXSZ (%lu) is not a power of 2", gasnetc_pin_maxsz);
    }
    tmp = gasnetc_pin_maxsz;
    for (gasnetc_pin_maxsz_shift=-1; tmp != 0; ++gasnetc_pin_maxsz_shift) { tmp >>= 1; }
  }
  #else
    GASNETC_ENVINT(gasnetc_putinmove_limit, GASNET_PUTINMOVE_LIMIT, GASNETC_DEFAULT_PUTINMOVE_LIMIT, 0, 1);
    if_pf (gasnetc_putinmove_limit > GASNETC_PUTINMOVE_LIMIT_MAX) {
      gasneti_fatalerror("GASNET_PUTINMOVE_LIMIT (%lu) is larger than the max permitted (%lu)", (unsigned long)gasnetc_putinmove_limit, (unsigned long)GASNETC_PUTINMOVE_LIMIT_MAX);
    }
  #endif
  gasnetc_use_rcv_thread = gasneti_getenv_yesno_withdefault("GASNET_RCV_THREAD", GASNETC_DEFAULT_RCV_THREAD); /* Bug 1012 - right default? */

  /* Verify correctness/sanity of values */
  if (gasnetc_use_rcv_thread && !GASNETC_VAPI_RCV_THREAD) {
    gasneti_fatalerror("VAPI AM receive thread enabled by environment variable GASNET_RCV_THREAD, but was disabled at GASNet build time");
  }
#if GASNET_DEBUG
  gasnetc_use_firehose = gasneti_getenv_yesno_withdefault("GASNET_USE_FIREHOSE", 1);
  if (!GASNETC_PIN_SEGMENT && !gasnetc_use_firehose) {
    gasneti_fatalerror("Use of the 'firehose' dynamic pinning library disabled by environment variable GASNET_USE_FIREHOSE, but is required in a GASNET_SEGMENT_" _STRINGIFY(GASNETI_SEGMENT_CONFIG) " configuration");
  }
#else
  if (!gasneti_getenv_yesno_withdefault("GASNET_USE_FIREHOSE", 1)) {
    fprintf(stderr,
	    "WARNING: Environment variable GASNET_USE_FIREHOSE ignored.  It is only available in a DEBUG build of GASNet\n");
  }
#endif
  if_pf (gasnetc_op_oust_limit && (gasnetc_am_oust_limit > gasnetc_op_oust_limit)) {
    fprintf(stderr,
            "WARNING: GASNET_AM_CREDITS_TOTAL reduced to GASNET_NETWORKDEPTH_TOTAL (from %d to %d)\n",
            gasnetc_am_oust_limit, gasnetc_op_oust_limit);
    gasnetc_am_oust_limit = gasnetc_op_oust_limit;
  }
  if_pf (gasnetc_am_oust_pp > gasnetc_op_oust_pp) {
    fprintf(stderr,
            "WARNING: GASNET_AM_CREDITS_PP reduced to GASNET_NETWORKDEPTH_PP (from %d to %d)\n",
            gasnetc_am_oust_pp, gasnetc_op_oust_pp);
    gasnetc_am_oust_pp = gasnetc_op_oust_pp;
  }
  if_pf (gasnetc_am_credits_slack >= gasnetc_am_oust_pp) {
    fprintf(stderr,
            "WARNING: GASNET_AM_CREDITS_SLACK reduced to GASNET_AM_CREDITS_PP-1 (from %d to %d)\n",
            gasnetc_am_credits_slack, gasnetc_am_oust_pp-1);
    gasnetc_am_credits_slack = gasnetc_am_oust_pp - 1;
  }
  if_pf (gasnetc_packedlong_limit > GASNETC_MAX_PACKEDLONG) {
    fprintf(stderr,
            "WARNING: GASNETC_PACKEDLONG_LIMIT reduced from %u to %u\n",
            (unsigned int)gasnetc_packedlong_limit, GASNETC_MAX_PACKEDLONG);
    gasnetc_packedlong_limit = GASNETC_MAX_PACKEDLONG;
  }


  /* Report */
  GASNETI_TRACE_PRINTF(C,("vapi-conduit build time configuration settings = {"));
  GASNETI_TRACE_PRINTF(C,("  AM receives in internal thread %sabled (GASNETC_VAPI_RCV_THREAD)",
				GASNETC_VAPI_RCV_THREAD ? "en" : "dis"));
#if GASNETC_VAPI_POLL_LOCK
  GASNETI_TRACE_PRINTF(C,("  Serialized CQ polls            YES (--enable-vapi-poll-lock)"));
#else
  GASNETI_TRACE_PRINTF(C,("  Serialized CQ polls            NO (default)"));
#endif
  GASNETI_TRACE_PRINTF(C,("  Max. snd completions per poll  %d (GASNETC_SND_REAP_LIMIT)",
				GASNETC_SND_REAP_LIMIT));
  GASNETI_TRACE_PRINTF(C,("  Max. rcv completions per poll  %d (GASNETC_RCV_REAP_LIMIT)",
				GASNETC_RCV_REAP_LIMIT));
  GASNETI_TRACE_PRINTF(C,  ("}"));

  GASNETI_TRACE_PRINTF(C,("vapi-conduit run time configuration settings = {"));
  if (gasnetc_vapi_ports && strlen(gasnetc_vapi_ports)) {
    GASNETI_TRACE_PRINTF(C,  ("  GASNET_VAPI_PORTS               = '%s'", gasnetc_vapi_ports));
  } else {
    GASNETI_TRACE_PRINTF(C,  ("  GASNET_VAPI_PORTS               = empty or unset (probe all)"));
  }
  if (gasnetc_num_qps) {
    GASNETI_TRACE_PRINTF(C,  ("  GASNET_NUM_QPS                  = %d", gasnetc_num_qps));
  } else {
    GASNETI_TRACE_PRINTF(C,  ("  GASNET_NUM_QPS                  = 0 (automatic)"));
  }
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_NETWORKDEPTH_PP          = %d", gasnetc_op_oust_pp));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_NETWORKDEPTH_TOTAL       = %d%s",
			  	gasnetc_op_oust_limit, gasnetc_op_oust_limit ? "" : " (automatic)"));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_AM_CREDITS_PP            = %d", gasnetc_am_oust_pp));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_AM_CREDITS_TOTAL         = %d%s",
			  	gasnetc_am_oust_limit, gasnetc_am_oust_limit ? "" : " (automatic)"));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_AM_CREDITS_SLACK         = %d", gasnetc_am_credits_slack));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_BBUF_COUNT               = %d%s",
			  	gasnetc_bbuf_limit, gasnetc_bbuf_limit ? "": " (automatic)"));
#if GASNETC_PIN_SEGMENT
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_PIN_MAXSZ                = %lu", gasnetc_pin_maxsz));
#endif
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_INLINESEND_LIMIT         = %u", (unsigned int)gasnetc_inline_limit));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_NONBULKPUT_BOUNCE_LIMIT  = %u", (unsigned int)gasnetc_bounce_limit));
#if !GASNETC_PIN_SEGMENT
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_PUTINMOVE_LIMIT          = %u", (unsigned int)gasnetc_putinmove_limit));
#endif
#if GASNETC_VAPI_RCV_THREAD
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_RCV_THREAD               = %d (%sabled)", gasnetc_use_rcv_thread,
				gasnetc_use_rcv_thread ? "en" : "dis"));
#else
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_RCV_THREAD               disabled at build time"));
#endif
  GASNETI_TRACE_PRINTF(C,  ("}"));

  return GASNET_OK;
}

static void gasneti_bootstrapInit(int *argc_p, char ***argv_p,
				  gasnet_node_t *nodes_p, gasnet_node_t *mynode_p) {
#if HAVE_MPI_SPAWNER
  if ((*argc_p < 2) || strncmp((*argv_p)[1], "-GASNET-SPAWN-", 14)) {
    gasneti_bootstrapInit_mpi(argc_p, argv_p, nodes_p, mynode_p);
    gasneti_bootstrapFini_p	= &gasneti_bootstrapFini_mpi;
    gasneti_bootstrapAbort_p	= &gasneti_bootstrapAbort_mpi;
    gasneti_bootstrapBarrier_p	= &gasneti_bootstrapBarrier_mpi;
    gasneti_bootstrapExchange_p	= &gasneti_bootstrapExchange_mpi;
    gasneti_bootstrapAlltoall_p	= &gasneti_bootstrapAlltoall_mpi;
    gasneti_bootstrapBroadcast_p= &gasneti_bootstrapBroadcast_mpi;
  } else
#endif
  {
    gasneti_bootstrapInit_ssh(argc_p, argv_p, nodes_p, mynode_p);
    gasneti_bootstrapFini_p	= &gasneti_bootstrapFini_ssh;
    gasneti_bootstrapAbort_p	= &gasneti_bootstrapAbort_ssh;
    gasneti_bootstrapBarrier_p	= &gasneti_bootstrapBarrier_ssh;
    gasneti_bootstrapExchange_p	= &gasneti_bootstrapExchange_ssh;
    gasneti_bootstrapAlltoall_p	= &gasneti_bootstrapAlltoall_ssh;
    gasneti_bootstrapBroadcast_p= &gasneti_bootstrapBroadcast_ssh;
  }
}

/* Info used while probing for HCAs/ports */
typedef struct {
  int			hca_index;	/* Slot in gasnetc_hca[] */
  IB_port_t		port_num;	/* Port number */
  VAPI_hca_port_t	port;		/* Port info */
  int			rd_atom;
} gasnetc_port_info_t;
typedef struct gasnetc_port_list_ {
  struct gasnetc_port_list_	*next;
  char				*id;
  int				port;
  int				matched;
} gasnetc_port_list_t;

static gasnetc_port_list_t *gasnetc_port_list = NULL;

static void
gasnetc_add_port(char *id, int port) {
  gasnetc_port_list_t *result = gasneti_malloc(sizeof(gasnetc_port_list_t));
  result->next = gasnetc_port_list;
  gasnetc_port_list = result;
  result->id = id;
  result->port = port;
  result->matched = 0;
}

static int
gasnetc_match_port(const char *id, int port, int mark) {
  gasnetc_port_list_t *curr;
  int found;

  if (gasnetc_port_list != NULL) {
    found = 0;
    for (curr = gasnetc_port_list; curr && !found; curr = curr->next) {
      if (!curr->matched && !strcmp(id, curr->id)) {
	if (!port) { /* match HCA only */
	  found = 1;
	} else if (!curr->port || (curr->port == port)) { /* 0 is a wild card */
	  found = 1;
	  curr->matched = mark;
        }
      }
    }
  } else {
    found = 1;
  }

  return found;
}

static void
gasnetc_clear_ports(void) {
  gasnetc_port_list_t *curr;

  curr = gasnetc_port_list;
  while(curr) {
    gasnetc_port_list_t *next = curr->next;
    if (curr->matched == 0) {
      if (curr->port) {
        GASNETI_TRACE_PRINTF(C,("Probe left element '%s:%d' of GASNET_VAPI_PORTS unused", curr->id, curr->port));
      } else {
        GASNETI_TRACE_PRINTF(C,("Probe left element '%s' of GASNET_VAPI_PORTS unused", curr->id));
      }
    }
    gasneti_free(curr->id);
    gasneti_free(curr);
    curr = next;
  }
  gasnetc_port_list = NULL;
}

/* Return length of initial substring of S not containing C,
 * limited to LEN or unlimited if LEN is 0.
 */
static int
gasnetc_scan(const char *s, int c, int len) {
  int i;
  for (i = 0; !len || (i < len); ++i) {
    if ((s[i] == '\0') || (s[i] == c)) return i;
  }
  return len;
}

static int
gasnetc_parse_ports(const char *p) {
  int len = strlen(p);
  while (len > 0) {
    int a = gasnetc_scan(p, '+', len);
    int b = gasnetc_scan(p, ':', a);
    char *hca = gasneti_strndup(p, b);
    if (b >= a-1) {
      gasnetc_add_port(gasneti_strndup(p, b), 0);
    } else {
      const char *r = p + b + 1;
      while (r < p + a) {
	int port = atoi(r);
	if (port) {
	  gasnetc_add_port(gasneti_strndup(p, b), port);
	  r += gasnetc_scan(r, ',', 0) + 1;
	} else {
          gasneti_free(hca);
	  return 1;
	}
      }
    }
    gasneti_free(hca);
    p += a + 1;
    len -= a + 1;
  }

  return 0;
}

/* Try to find up to *port_count_p ACTIVE ports, replacing w/ the actual count */
static gasnetc_port_info_t* gasnetc_probe_ports(int *port_count_p) {
  VAPI_hca_id_t		*hca_ids;
  gasnetc_port_info_t	*port_tbl;
  VAPI_ret_t		vstat;
  u_int32_t		num_hcas;	/* Type specified by Mellanox */
  int			max_ports = *port_count_p;
  int			port_count = 0;
  int			hca_count = 0;
  int			curr_hca;

  if (gasnetc_parse_ports(gasnetc_vapi_ports)) {
    GASNETI_TRACE_PRINTF(C,("Failed to parse GASNET_VAPI_PORTS='%s'", gasnetc_vapi_ports));
    gasnetc_clear_ports();
    return NULL;
  }

  if (max_ports) {
    GASNETI_TRACE_PRINTF(C,("Probing HCAs for active ports (max %d)", max_ports));
  } else {
    GASNETI_TRACE_PRINTF(C,("Probing HCAs for active ports"));
    max_ports = 128;	/* If you have more than 128 IB ports per node, then I owe you $20 :-) */
  }

  port_tbl = gasneti_calloc(max_ports, sizeof(gasnetc_port_info_t));
  num_hcas = 0; /* call will overwrite with actual count */
  vstat = EVAPI_list_hcas(0, &num_hcas, NULL);
  if (((vstat != VAPI_OK) && (vstat != VAPI_EAGAIN)) || (num_hcas == 0)) {
    GASNETI_TRACE_PRINTF(C,("Probe failed to locate any HCAs"));
    gasnetc_clear_ports();
    return NULL;
  }
  hca_ids = gasneti_calloc(num_hcas, sizeof(VAPI_hca_id_t));
  vstat = EVAPI_list_hcas(num_hcas, &num_hcas, hca_ids);
  GASNETC_VAPI_CHECK(vstat, "while enumerating HCAs");

  if ((num_hcas > GASNETC_VAPI_MAX_HCAS) && (gasnetc_port_list == NULL)) {
    fprintf(stderr, "WARNING: Found %d IB HCAs, but GASNet was configured with '--with-vapi-max-hcas="
		    _STRINGIFY(GASNETC_VAPI_MAX_HCAS) "'.  To utilize all your HCAs, you should "
		    "reconfigure GASNet with '--with-vapi-max-hcas=%d'.  You can silence this warning "
		    "by setting the environment variable GASNET_VAPI_PORTS as described in the file "
		    "'gasnet/vapi-conduit/README'.\n", num_hcas, num_hcas);
  }

  /* Loop over VAPI's list of HCAs */
  for (curr_hca = 0;
       (hca_count < GASNETC_VAPI_MAX_HCAS) && (port_count < max_ports) && (curr_hca < num_hcas);
       ++curr_hca) {
    VAPI_hca_hndl_t	hca_handle;
    VAPI_hca_vendor_t	hca_vendor;
    VAPI_hca_cap_t	hca_cap;
    int found = 0;
    int curr_port;

    if (!gasnetc_match_port(hca_ids[curr_hca], 0, 0)) {
      GASNETI_TRACE_PRINTF(C,("Probe skipping HCA '%s' - no match in GASNET_VAPI_PORTS", hca_ids[curr_hca]));
      continue;
    }

    vstat = VAPI_open_hca(hca_ids[curr_hca], &hca_handle);
    if (vstat != VAPI_OK) {
      vstat = EVAPI_get_hca_hndl(hca_ids[curr_hca], &hca_handle);
    }
    if (vstat == VAPI_OK) {
      GASNETI_TRACE_PRINTF(C,("Probe found HCA '%s'", hca_ids[curr_hca]));
    } else {
      GASNETI_TRACE_PRINTF(C,("Probe failed to open HCA '%s'", hca_ids[curr_hca]));
      continue;	/* OK, keep trying HCAs */
    }
    vstat = VAPI_query_hca_cap(hca_handle, &hca_vendor, &hca_cap);
    GASNETC_VAPI_CHECK(vstat, "from VAPI_query_hca_cap()");

    /* Loop over ports on the HCA (they start numbering at 1) */
    for (curr_port = 1;
	 (port_count < max_ports) && (curr_port <= hca_cap.phys_port_num);
	 ++curr_port) {
      gasnetc_port_info_t *this_port = &port_tbl[port_count];

      if (!gasnetc_match_port(hca_ids[curr_hca], curr_port, 0)) {
        GASNETI_TRACE_PRINTF(C,("Probe skipping HCA '%s', port %d - no match in GASNET_VAPI_PORTS", hca_ids[curr_hca], curr_port));
	continue;
      }

      (void)VAPI_query_hca_port_prop(hca_handle, curr_port, &this_port->port);

      if (this_port->port.state == PORT_ACTIVE) {
        ++port_count;
        ++found;
        this_port->port_num = curr_port;
        this_port->hca_index = hca_count;
	this_port->rd_atom   = MIN(hca_cap.max_qp_init_rd_atom, hca_cap.max_qp_ous_rd_atom);
        GASNETI_TRACE_PRINTF(C,("Probe found HCA '%s', port %d", hca_ids[curr_hca], curr_port));
        (void)gasnetc_match_port(hca_ids[curr_hca], curr_port, 1);
	if (gasnetc_port_list == NULL) {
	  /* By default one at most 1 port per HCA */
	  break;
	}
      } else {
	const char *state;

	switch (this_port->port.state) {
	case PORT_DOWN:
		state = "DOWN";
		break;
	case PORT_INITIALIZE:
		state = "INITIALIZE";
		break;
	case PORT_ARMED:
		state = "ARMED";
		break;
	default:
		state = "unknown";
        }
        GASNETI_TRACE_PRINTF(C,("Probe skipping HCA '%s', port %d - state = %s", hca_ids[curr_hca], curr_port, state));
      }
    }

    /* Install or release the HCA */
    if (found) {
      gasnetc_hca_t *hca = &gasnetc_hca[hca_count];

      memset(hca, 0, sizeof(gasnetc_hca_t));
      hca->handle	= hca_handle;
      hca->hca_index	= hca_count;
      hca->hca_id	= gasneti_strdup(hca_ids[curr_hca]);
      hca->hca_cap	= hca_cap;
      hca->hca_vendor	= hca_vendor;

      hca_count++;
    } else {
      (void)EVAPI_release_hca_hndl(hca_handle);
    }
  }
  GASNETI_TRACE_PRINTF(C,("Probe found %d active port(s) on %d HCA(s)", port_count, hca_count));
  gasnetc_clear_ports();

  gasnetc_num_hcas = hca_count;
  *port_count_p = port_count;
  
  return port_tbl;
}

static int gasnetc_init(int *argc, char ***argv) {
  gasnetc_port_info_t	*port_tbl, **port_map;
  gasnetc_hca_t		*hca;
  gasnetc_addr_t	*local_addr;
  gasnetc_addr_t	*remote_addr;
  VAPI_ret_t		vstat;
  int			ceps;
  int 			num_ports;
  int 			h, i;

  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  /* Initialize the bootstrapping support. */
  /* Must come very early to get the global ENV. */
  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif
  gasneti_bootstrapInit(argc, argv, &gasneti_nodes, &gasneti_mynode);

  gasneti_init_done = 1; /* enable early to allow tracing */

  gasneti_freezeForDebugger();

  /* Now enable tracing of all the following steps */
  gasneti_trace_init(argc, argv);

  /* Process the environment for configuration/settings */
  i = gasnetc_load_settings();
  if (i != GASNET_OK) {
    return i;
  }

  /* Find the port(s) to use */
  num_ports = gasnetc_num_qps;
  port_tbl = gasnetc_probe_ports(&num_ports);
  if (!gasnetc_num_qps) {
    /* Let the probe determine gasnetc_num_qps */
    gasnetc_num_qps = num_ports;
  }
  if (!num_ports || (port_tbl == NULL)) {
    if (gasnetc_vapi_ports && strlen(gasnetc_vapi_ports)) {
      GASNETI_RETURN_ERRR(RESOURCE, "unable to open any HCA ports given in GASNETC_VAPI_PORTS");
    } else {
      GASNETI_RETURN_ERRR(RESOURCE, "unable to open any HCA ports");
    }
  }

  /* allocate resources */
  ceps = gasneti_nodes * gasnetc_num_qps;
  gasnetc_cep = (gasnetc_cep_t *)GASNETI_ALIGNUP(gasneti_calloc(1, ceps*sizeof(gasnetc_cep_t)
									+ GASNETI_CACHE_LINE_BYTES - 1),
						 GASNETI_CACHE_LINE_BYTES);
  local_addr = gasneti_calloc(ceps, sizeof(gasnetc_addr_t));
  remote_addr = gasneti_calloc(ceps, sizeof(gasnetc_addr_t));
  port_map = gasneti_calloc(ceps, sizeof(gasnetc_port_info_t *));

  /* Distribute the qps to each peer round-robin over the ports */
  for (i = 0; i < ceps; ) {
    if (i/gasnetc_num_qps == gasneti_mynode) {
      i += gasnetc_num_qps;
    } else {
      int j;
      for (j = 0; j < gasnetc_num_qps; ++j, ++i) {
        port_map[i] = &port_tbl[j % num_ports];
	++(gasnetc_hca[port_map[i]->hca_index].total_qps);
      }
    }
  }
  if (gasneti_nodes == 1) {
    GASNETC_FOR_ALL_HCA(hca) {
      /* Avoid a later division by zero */
      hca->total_qps = 1;
      hca->qps = 1;
    }
  } else {
    GASNETC_FOR_ALL_HCA(hca) {
      hca->qps = hca->total_qps / (gasneti_nodes - 1);
    }
  }

  /* Report/check hca and port properties */
  gasnetc_max_msg_sz = ~0;
  GASNETC_FOR_ALL_HCA_INDEX(h) {
    hca = &gasnetc_hca[h];
    GASNETI_TRACE_PRINTF(C,("vapi-conduit HCA properties (%d of %d) = {", h+1, gasnetc_num_hcas));
    GASNETI_TRACE_PRINTF(C,("  HCA id                   = '%s'", hca->hca_id));
    GASNETI_TRACE_PRINTF(C,("  HCA vendor id            = 0x%x", (unsigned int)hca->hca_vendor.vendor_id));
    GASNETI_TRACE_PRINTF(C,("  HCA vendor part id       = 0x%x", (unsigned int)hca->hca_vendor.vendor_part_id));
    GASNETI_TRACE_PRINTF(C,("  HCA hardware version     = 0x%x", (unsigned int)hca->hca_vendor.hw_ver));
    GASNETI_TRACE_PRINTF(C,("  HCA firmware version     = 0x%x%08x", (unsigned int)(hca->hca_vendor.fw_ver >> 32), (unsigned int)(hca->hca_vendor.fw_ver & 0xffffffff)));
    GASNETI_TRACE_PRINTF(C,("  max_num_qp               = %u", (unsigned int)hca->hca_cap.max_num_qp));
    GASNETI_TRACE_PRINTF(C,("  max_qp_ous_wr            = %u", (unsigned int)hca->hca_cap.max_qp_ous_wr));
    GASNETI_TRACE_PRINTF(C,("  max_num_sg_ent           = %u", (unsigned int)hca->hca_cap.max_num_sg_ent));
    gasneti_assert_always(hca->hca_cap.max_num_sg_ent >= GASNETC_SND_SG);
    gasneti_assert_always(hca->hca_cap.max_num_sg_ent >= 1);
    #if 1 /* QP end points */
      GASNETI_TRACE_PRINTF(C,("  max_qp_init_rd_atom      = %u", (unsigned int)hca->hca_cap.max_qp_init_rd_atom));
      gasneti_assert_always(hca->hca_cap.max_qp_init_rd_atom >= 1);	/* RDMA Read support required */
      GASNETI_TRACE_PRINTF(C,("  max_qp_ous_rd_atom       = %u", (unsigned int)hca->hca_cap.max_qp_ous_rd_atom));
      gasneti_assert_always(hca->hca_cap.max_qp_ous_rd_atom >= 1);	/* RDMA Read support required */
    #else
      GASNETI_TRACE_PRINTF(C,("  max_ee_init_rd_atom      = %u", (unsigned int)hca->hca_cap.max_ee_init_rd_atom));
      gasneti_assert_always(hca->hca_cap.max_ee_init_rd_atom >= 1);	/* RDMA Read support required */
      GASNETI_TRACE_PRINTF(C,("  max_ee_ous_rd_atom       = %u", (unsigned int)hca->hca_cap.max_ee_ous_rd_atom));
      gasneti_assert_always(hca->hca_cap.max_ee_ous_rd_atom >= 1);	/* RDMA Read support required */
    #endif
    GASNETI_TRACE_PRINTF(C,("  max_num_cq               = %u", (unsigned int)hca->hca_cap.max_num_cq));
    gasneti_assert_always(hca->hca_cap.max_num_cq >= 2);
    GASNETI_TRACE_PRINTF(C,("  max_num_ent_cq           = %u", (unsigned int)hca->hca_cap.max_num_ent_cq));
  
    /* Check for sufficient pinning resources */
    {
      int mr_needed = 2;		/* rcv bufs and snd bufs */
      #if FIREHOSE_USE_FMR
        int fmr_needed = 0;	/* none by default */
      #endif
  
      #if GASNETC_PIN_SEGMENT
        mr_needed++;		/* XXX: need more than 1 due to gasnetc_pin_maxsz */
      #endif
      #if FIREHOSE_USE_FMR
        fmr_needed += FIREHOSE_CLIENT_MAXREGIONS;	/* FMRs needed for firehoses */
      #else
        mr_needed += FIREHOSE_CLIENT_MAXREGIONS;	/* regular MRs needed for firehoses */
      #endif
  
      GASNETI_TRACE_PRINTF(C,("  max_num_mr               = %u", (unsigned int)hca->hca_cap.max_num_mr));
      gasneti_assert_always(hca->hca_cap.max_num_mr >=  mr_needed);
      #if FIREHOSE_USE_FMR
        GASNETI_TRACE_PRINTF(C,("  max_num_fmr              = %u", (unsigned int)hca->hca_cap.max_num_fmr));
        gasneti_assert_always(hca->hca_cap.max_num_fmr >= fmr_needed);
      #endif
    }
  
    /* Vendor-specific firmware checks */
    if (hca->hca_vendor.vendor_id == MT_MELLANOX_IEEE_VENDOR_ID) {
       int defect;

      #if !GASNETC_VAPI_POLL_LOCK
        /* For firmware < 3.0 there is a thread safety bug with VAPI_poll_cq(). */
        defect = (hca->hca_vendor.fw_ver < (uint64_t)(0x300000000LL));
        GASNETI_TRACE_PRINTF(C,("  Serialized CQ polls      : %srequired for this firmware",
			        defect ? "" : "not "));
	if (defect) {
	  GASNETI_RETURN_ERRR(RESOURCE, "\n"
		  "Your HCA firmware is suspected to include a thread safety bug in\n"
		  "VAPI_poll_cq(), which may cause hangs, crashes or incorrect results.\n"
		  "You must either upgrade your firmware, or rebuild GASNet passing the\n"
		  "flag '--enable-vapi-poll-lock' to configure.  See vapi-conduit/README.\n");
	}
      #endif

      /* For some firmware there is a performance bug with EVAPI_post_inline_sr(). */
      /* (1.18 <= fw_ver < 3.0) is known bad */
      defect = (hca->hca_vendor.fw_ver >= (uint64_t)(0x100180000LL)) &&
	       (hca->hca_vendor.fw_ver <  (uint64_t)(0x300000000LL));
      if (defect && gasnetc_inline_limit) {
	  fprintf(stderr,
		  "WARNING: Your HCA firmware is suspected to include a performance defect\n"
		  "when using EVAPI_post_inline_sr().  You may wish to either upgrade your\n"
		  "firmware, or set GASNET_INLINESEND_LIMIT=0 in your environment.\n");
      }
    
      GASNETI_TRACE_PRINTF(C,("  Inline perfomance defect : %ssuspected in this firmware",
			      defect ? "" : "not "));
    }
      
    /* Per-port: */
    for (i = 0; i < num_ports; ++i) {
      if (port_tbl[i].hca_index == h) {
        GASNETI_TRACE_PRINTF(C,("  port %d properties = {", (int)port_tbl[i].port_num));
        GASNETI_TRACE_PRINTF(C,("    LID                      = %u", (unsigned int)port_tbl[i].port.lid));
        GASNETI_TRACE_PRINTF(C,("    max_msg_sz               = %u", (unsigned int)port_tbl[i].port.max_msg_sz));
        GASNETI_TRACE_PRINTF(C,("  }"));
        gasnetc_max_msg_sz = MIN(gasnetc_max_msg_sz, port_tbl[i].port.max_msg_sz);
      }
    }

    GASNETI_TRACE_PRINTF(C,("}")); /* end of HCA report */
  }

  /* Divide _pp bounds equally over the available QPs */
  gasnetc_op_oust_pp /= gasnetc_num_qps;
  gasnetc_am_oust_pp /= gasnetc_num_qps;

  /* sanity checks */
  GASNETC_FOR_ALL_HCA(hca) {
    if_pf (gasneti_nodes*((gasnetc_num_qps+gasnetc_num_hcas-1)/gasnetc_num_hcas) > hca->hca_cap.max_num_qp) {
      GASNETC_FOR_ALL_HCA(hca) { (void)EVAPI_release_hca_hndl(hca->handle); }
      GASNETI_RETURN_ERRR(RESOURCE, "gasnet_nodes exceeds HCA capabilities");
    }
    if_pf (gasnetc_am_oust_pp * 2 > hca->hca_cap.max_qp_ous_wr) {
      GASNETC_FOR_ALL_HCA(hca) { (void)EVAPI_release_hca_hndl(hca->handle); }
      GASNETI_RETURN_ERRR(RESOURCE, "GASNET_AM_CREDITS_PP exceeds HCA capabilities");
    }
    if_pf (gasnetc_op_oust_pp > hca->hca_cap.max_qp_ous_wr) {
      GASNETC_FOR_ALL_HCA(hca) { (void)EVAPI_release_hca_hndl(hca->handle); }
      GASNETI_RETURN_ERRR(RESOURCE, "GASNET_NETWORKDEPTH_PP exceeds HCA capabilities");
    }
  }
  #if GASNETC_PIN_SEGMENT
    if_pf (gasnetc_max_msg_sz < gasnetc_pin_maxsz) {
      GASNETC_FOR_ALL_HCA(hca) { (void)EVAPI_release_hca_hndl(hca->handle); }
      GASNETI_RETURN_ERRR(RESOURCE, "GASNET_PIN_MAXSZ exceeds HCA capabilities");
    }
  #endif
  gasnetc_bounce_limit = MIN(gasnetc_max_msg_sz, gasnetc_bounce_limit);

  /* get a pd for the QPs and memory registration */
  GASNETC_FOR_ALL_HCA(hca) {
    vstat =  VAPI_alloc_pd(hca->handle, &hca->pd);
    GASNETC_VAPI_CHECK(vstat, "from VAPI_alloc_pd()");
  }

  /* allocate/initialize transport resources */
  i = gasnetc_sndrcv_init();
  if (i != GASNET_OK) {
    GASNETC_FOR_ALL_HCA(hca) {
      (void)VAPI_dealloc_pd(hca->handle, hca->pd);
      (void)EVAPI_release_hca_hndl(hca->handle);
    }
    return i;
  }

  /* create all the endpoints */
  {
    gasnetc_cep_t *cep = &gasnetc_cep[0];
    VAPI_qp_init_attr_t	qp_init_attr;
    VAPI_qp_prop_t	qp_prop;

    qp_init_attr.cap.max_oust_wr_rq = gasnetc_am_oust_pp * 2;
    qp_init_attr.cap.max_oust_wr_sq = gasnetc_op_oust_pp;
    qp_init_attr.cap.max_sg_size_rq = 1;
    qp_init_attr.cap.max_sg_size_sq = GASNETC_SND_SG;
    qp_init_attr.rdd_hndl           = 0;
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type            = VAPI_TS_RC;

    for (i = 0; i < ceps; ++i) {
      if (i/gasnetc_num_qps == gasneti_mynode) continue;

      hca = &gasnetc_hca[port_map[i]->hca_index];

      cep[i].hca = hca;
      cep[i].hca_handle = hca->handle;
      cep[i].hca_index = hca->hca_index;

      qp_init_attr.pd_hndl            = hca->pd;
      qp_init_attr.rq_cq_hndl         = hca->rcv_cq;
      qp_init_attr.sq_cq_hndl         = hca->snd_cq;

      /* create the QP */
      vstat = VAPI_create_qp(hca->handle, &qp_init_attr, &cep[i].qp_handle, &qp_prop);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_create_qp()");
      gasneti_assert(qp_prop.cap.max_oust_wr_rq >= gasnetc_am_oust_pp * 2);
      gasneti_assert(qp_prop.cap.max_oust_wr_sq >= gasnetc_op_oust_pp);

      local_addr[i].lid = port_map[i]->port.lid;
      local_addr[i].qp_num = qp_prop.qp_num;
    }
  }

  /* exchange endpoint info for connecting */
  gasneti_bootstrapAlltoall(local_addr, gasnetc_num_qps*sizeof(gasnetc_addr_t), remote_addr);

  /* connect the endpoints */
  {
    VAPI_qp_attr_t	qp_attr;
    VAPI_qp_attr_mask_t	qp_mask;
    VAPI_qp_cap_t	qp_cap;

    /* advance RST -> INIT */
    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PKEY_IX);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PORT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);
    qp_attr.qp_state            = VAPI_INIT;
    qp_attr.pkey_ix             = 0;
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;
    for (i = 0; i < ceps; ++i) {
      if (i/gasnetc_num_qps == gasneti_mynode) continue;
      
      qp_attr.port = port_map[i]->port_num;
      vstat = VAPI_modify_qp(gasnetc_cep[i].hca_handle, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_modify_qp(INIT)");
    }

    /* post recv buffers and other local initialization */
    for (i = 0; i < gasneti_nodes; ++i) {
      gasnetc_sndrcv_init_peer(i);
    }

    /* advance INIT -> RTR */
    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_AV);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PATH_MTU);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RQ_PSN);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_OUS_RD_ATOM);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_DEST_QP_NUM);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_MIN_RNR_TIMER);
    qp_attr.qp_state         = VAPI_RTR;
    qp_attr.av.sl            = 0;
    qp_attr.av.grh_flag      = FALSE;
    qp_attr.av.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.av.src_path_bits = 0;
    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;
    for (i = 0; i < ceps; ++i) {
      if (i/gasnetc_num_qps == gasneti_mynode) continue;

      qp_attr.qp_ous_rd_atom = port_map[i]->rd_atom;
      qp_attr.path_mtu       = MIN(GASNETC_QP_PATH_MTU, port_map[i]->port.max_mtu);
      qp_attr.rq_psn         = i;
      qp_attr.av.dlid        = remote_addr[i].lid;
      qp_attr.dest_qp_num    = remote_addr[i].qp_num;
      vstat = VAPI_modify_qp(gasnetc_cep[i].hca_handle, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_modify_qp(RTR)");
    }

    /* QPs must reach RTR before their peer can advance to RTS */
    gasneti_bootstrapBarrier();

    /* advance RTR -> RTS */
    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_SQ_PSN);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_TIMEOUT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RETRY_COUNT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RNR_RETRY);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_OUS_DST_RD_ATOM);
    qp_attr.qp_state         = VAPI_RTS;
    qp_attr.timeout          = GASNETC_QP_TIMEOUT;
    qp_attr.retry_count      = GASNETC_QP_RETRY_COUNT;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;
    for (i = 0; i < ceps; ++i) {
      if (i/gasnetc_num_qps == gasneti_mynode) continue;

      qp_attr.sq_psn           = gasneti_mynode*gasnetc_num_qps + (i % gasnetc_num_qps);
      qp_attr.ous_dst_rd_atom  = port_map[i]->rd_atom;
      vstat = VAPI_modify_qp(gasnetc_cep[i].hca_handle, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_modify_qp(RTS)");
      if (qp_cap.max_inline_data_sq < gasnetc_inline_limit) {
	fprintf(stderr,
		"WARNING: Requested GASNET_INLINESEND_LIMIT %d reduced to HCA limit %d\n",
		(int)gasnetc_inline_limit, (int)qp_cap.max_inline_data_sq);
        gasnetc_inline_limit = qp_cap.max_inline_data_sq;
      }
    }
  }

  gasneti_free(port_map);
  gasneti_free(port_tbl);

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif
  
  /* Find max pinnable size before we start carving up memory w/ mmap()s.
   *
   * Take care that only one process per LID (node) performs the probe.
   * The result is then divided by the number of processes on the adapter,
   * which is easily determined from the connection information we exchanged above.
   */
  {
    gasnet_node_t	num_local;
    gasnet_node_t	first_local;

    /* Determine the number of local processes and distinguish one */
    num_local = 1;
    first_local = gasneti_mynode;
    for (i = 0; i < gasneti_nodes; ++i) {
      /* Use lid of 1st qp to match procs on same node */
      if (i == gasneti_mynode) continue;
      if (remote_addr[i * gasnetc_num_qps].lid == local_addr[0].lid) {
        ++num_local;
        first_local = MIN(i, first_local);
      }
    }
    gasneti_assert(num_local != 0);
    gasneti_assert(first_local != gasneti_nodes);
    GASNETI_TRACE_PRINTF(C,("Detected %d on-node peer(s)", num_local-1));

    /* Query the pinning limits of the HCA */
    gasnetc_init_pin_info(first_local, num_local);

    gasneti_assert(gasnetc_pin_info.memory != 0);
    gasneti_assert(gasnetc_pin_info.memory != (uintptr_t)(-1));
    gasneti_assert(gasnetc_pin_info.regions != 0);
  }

  gasneti_free(remote_addr);
  gasneti_free(local_addr);

  #if GASNET_SEGMENT_FAST
  {
    /* Reserved memory needed by firehose on each node */
    /* NOTE: We reserve this memory even when firehose is disabled, since the disable
     * is only made available for debugging. */
    size_t reserved_mem = GASNETC_MIN_FH_PAGES * GASNET_PAGESIZE;

    if_pf (gasnetc_pin_info.memory < reserved_mem) {
      gasneti_fatalerror("Pinnable memory (%lu) is less than reserved minimum %lu\n", (unsigned long)gasnetc_pin_info.memory, (unsigned long)reserved_mem);
    }
    gasneti_segmentInit((gasnetc_pin_info.memory - reserved_mem), &gasneti_bootstrapExchange);
  }
  #elif GASNET_SEGMENT_LARGE
    gasneti_segmentInit((uintptr_t)(-1), &gasneti_bootstrapExchange);
  #elif GASNET_SEGMENT_EVERYTHING
    /* segment is everything - nothing to do */
  #endif

  atexit(gasnetc_atexit);

  #if 0
    /* Done earlier to allow tracing */
    gasneti_init_done = 1;  
  #endif
  gasneti_bootstrapBarrier();

  gasneti_auxseg_init(); /* adjust max seg values based on auxseg */

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* Already done in gasnetc_init() to allow tracing of init steps */
    gasneti_trace_init(argc, argv);
  #endif
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
static char checkuniqhandler[256] = { 0 };
static int gasnetc_reghandlers(gasnet_handlerentry_t *table, int numentries,
                               int lowlimit, int highlimit,
                               int dontcare, int *numregistered) {
  int i;
  *numregistered = 0;
  for (i = 0; i < numentries; i++) {
    int newindex;

    if ((table[i].index == 0 && !dontcare) || 
        (table[i].index && dontcare)) continue;
    else if (table[i].index) newindex = table[i].index;
    else { /* deterministic assignment of dontcare indexes */
      for (newindex = lowlimit; newindex <= highlimit; newindex++) {
        if (!checkuniqhandler[newindex]) break;
      }
      if (newindex > highlimit) {
        char s[255];
        sprintf(s,"Too many handlers. (limit=%i)", highlimit - lowlimit + 1);
        GASNETI_RETURN_ERRR(BAD_ARG, s);
      }
    }

    /*  ensure handlers fall into the proper range of pre-assigned values */
    if (newindex < lowlimit || newindex > highlimit) {
      char s[255];
      sprintf(s, "handler index (%i) out of range [%i..%i]", newindex, lowlimit, highlimit);
      GASNETI_RETURN_ERRR(BAD_ARG, s);
    }

    /* discover duplicates */
    if (checkuniqhandler[newindex] != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "handler index not unique");
    checkuniqhandler[newindex] = 1;

    /* register the handler */
    gasnetc_handler[newindex] = table[i].fnptr;

    /* The check below for !table[i].index is redundant and present
     * only to defeat the over-aggressive optimizer in pathcc 2.1
     */
    if (dontcare && !table[i].index) table[i].index = newindex;

    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
  gasnetc_hca_t *hca;
  void *segbase = NULL;
  size_t maxsize = 0;
  int numreg = 0;
  gasnet_node_t i;
  
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
                          numentries, (unsigned long)segsize, (unsigned long)minheapoffset));

  if (!gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
  if (gasneti_attach_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

  /*  check argument sanity */
  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    if ((segsize % GASNET_PAGESIZE) != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize not page-aligned");
    if (segsize > gasneti_MaxLocalSegmentSize) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize too large");
    if ((minheapoffset % GASNET_PAGESIZE) != 0) /* round up the minheapoffset to page sz */
      minheapoffset = ((minheapoffset / GASNET_PAGESIZE) + 1) * GASNET_PAGESIZE;
  #elif GASNET_SEGMENT_EVERYTHING
    segsize = 0;
    minheapoffset = 0;
  #endif

  segsize = gasneti_auxseg_preattach(segsize); /* adjust segsize for auxseg reqts */

  /* ------------------------------------------------------------------------------------ */
  /*  register handlers */
  { /*  core API handlers */
    gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *)gasnetc_get_handlertable();
    int len = 0;
    gasneti_assert(ctable);
    while (ctable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(ctable, len, 1, 63, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
    gasneti_assert(numreg == len);
  }

  { /*  extended API handlers */
    gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
    int len = 0;
    gasneti_assert(etable);
    while (etable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(etable, len, 64, 127, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
    gasneti_assert(numreg == len);
  }

  #if GASNETC_PIN_SEGMENT
    /* No firehose AMs should ever be sent in this configuration */
  #else
  { /* firehose handlers */
    gasnet_handlerentry_t *ftable = (gasnet_handlerentry_t *)firehose_get_handlertable();
    int len = 0;
    int base = 64 + numreg;	/* start right after etable */
    gasneti_assert(ftable);
    while (ftable[len].fnptr) len++; /* calc len */
    gasneti_assert(base + len <= 128);	/* enough space remaining after etable? */
    if (gasnetc_reghandlers(ftable, len, base, 127, 1, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE, "Error registering firehose handlers");
    gasneti_assert(numreg == len);
  }
  #endif

  if (table) { /*  client handlers */
    int numreg1 = 0;
    int numreg2 = 0;

    /*  first pass - assign all fixed-index handlers */
    if (gasnetc_reghandlers(table, numentries, 128, 255, 0, &numreg1) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

    /*  second pass - fill in dontcare-index handlers */
    if (gasnetc_reghandlers(table, numentries, 128, 255, 1, &numreg2) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering variable-index client handlers");

    gasneti_assert(numreg1 + numreg2 == numentries);
  }

  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /* catch fatal signals and convert to SIGQUIT */
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

  /*  (###) register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasneti_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));

  #if GASNET_SEGMENT_EVERYTHING
  {
    for (i=0;i<gasneti_nodes;i++) {
      gasneti_seginfo[i].addr = (void *)0;
      gasneti_seginfo[i].size = (uintptr_t)-1;
    }
    segbase = (void *)0;
    segsize = (uintptr_t)-1;
  }
  #elif GASNETC_PIN_SEGMENT
  {
    /* allocate the segment and exchange seginfo */
    gasneti_segmentAttach(segsize, minheapoffset, gasneti_seginfo, &gasneti_bootstrapExchange);
    segbase = gasneti_seginfo[gasneti_mynode].addr;
    segsize = gasneti_seginfo[gasneti_mynode].size;

    gasnetc_seg_start = (uintptr_t)segbase;
    gasnetc_seg_end   = (uintptr_t)segbase + (segsize - 1);

    /* Find the largest number of pinned regions required */
    for (i=0; i<gasneti_nodes; ++i) {
      maxsize = MAX(maxsize, gasneti_seginfo[i].size);
    }
    gasnetc_max_regs = (maxsize + gasnetc_pin_maxsz - 1) >> gasnetc_pin_maxsz_shift;

    /* pin the segment and exchange the RKeys, once per HCA */
    { VAPI_rkey_t	*my_rkeys;
      VAPI_ret_t	vstat;
      int		j;

      my_rkeys = gasneti_calloc(gasnetc_max_regs, sizeof(VAPI_rkey_t));
      GASNETC_FOR_ALL_HCA(hca) {
        size_t		remain;
        uintptr_t		addr;
        hca->rkeys = gasneti_calloc(gasneti_nodes*gasnetc_max_regs, sizeof(VAPI_rkey_t));
        hca->seg_reg = gasneti_calloc(gasnetc_max_regs, sizeof(gasnetc_memreg_t));

        for (j = 0, addr = gasnetc_seg_start, remain = segsize; remain != 0; ++j) {
	  size_t len = MIN(remain, gasnetc_pin_maxsz);
          vstat = gasnetc_pin(hca, (void *)addr, len,
			      VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE | VAPI_EN_REMOTE_READ,
			      &hca->seg_reg[j]);
          GASNETC_VAPI_CHECK(vstat, "from VAPI_register_mr(segment)");
	  my_rkeys[j] = hca->seg_reg[j].rkey;
	  addr += len;
	  remain -= len;
          gasneti_assert(j <= gasnetc_max_regs);
        }

        gasneti_bootstrapExchange(my_rkeys, gasnetc_max_regs*sizeof(VAPI_rkey_t), hca->rkeys);
      }
      gasnetc_seg_reg_count = j;
      gasneti_free(my_rkeys);
    }
  }
  #else	/* just allocate the segment but don't pin it */
  {
    /* allocate the segment and exchange seginfo */
    gasneti_segmentAttach(segsize, minheapoffset, gasneti_seginfo, gasneti_bootstrapExchange);
    segbase = gasneti_seginfo[gasneti_mynode].addr;
    segsize = gasneti_seginfo[gasneti_mynode].size;
  }
  #endif

  /* Per-endpoint work */
  for (i = 0; i < gasneti_nodes; i++) {
    gasnetc_sndrcv_attach_peer(i);
  }

  /* Initialize firehose */
  if (GASNETC_USE_FIREHOSE) {
    uintptr_t firehose_mem = gasnetc_pin_info.memory;
    int firehose_reg = gasnetc_pin_info.regions;
    int reg_count, h;
    firehose_region_t prereg[2];
    size_t reg_size, maxsz;

    /* Setup prepinned regions list */
    prereg[0].addr             = gasnetc_hca[0].snd_reg.addr;
    prereg[0].len              = gasnetc_hca[0].snd_reg.len;
    GASNETC_FOR_ALL_HCA_INDEX(h) {
      prereg[0].client.handle[h] = VAPI_INVAL_HNDL;	/* unreg must fail */
      prereg[0].client.lkey[h]   = gasnetc_hca[h].snd_reg.lkey;
      prereg[0].client.rkey[h]   = gasnetc_hca[h].snd_reg.rkey;
    }
    reg_size = prereg[0].len;
    reg_count = 1;
    if (gasneti_nodes > 1) {
	prereg[reg_count].addr             = gasnetc_hca[0].rcv_reg.addr;
	prereg[reg_count].len              = gasnetc_hca[0].rcv_reg.len;
        GASNETC_FOR_ALL_HCA_INDEX(h) {
	  prereg[reg_count].client.handle[h] = VAPI_INVAL_HNDL;	/* unreg must fail */
	  prereg[reg_count].client.lkey[h]   = gasnetc_hca[h].rcv_reg.lkey;
	  prereg[reg_count].client.rkey[h]   = gasnetc_hca[h].rcv_reg.rkey;
	}
        reg_size += prereg[reg_count].len;
	reg_count++;
    }
    /* Adjust for prepinned regions (they were pinned before init_pin_info probe) */
    firehose_mem += reg_size;

    #if FIREHOSE_VAPI_USE_FMR
    {
      /* Prepare FMR properties */
      GASNETC_FOR_ALL_HCA(hca) {
        hca->fmr_props.pd_hndl = hca->pd;
        hca->fmr_props.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE | VAPI_EN_REMOTE_READ;
        hca->fmr_props.log2_page_sz = GASNETI_PAGESHIFT;
        hca->fmr_props.max_outstanding_maps = 1;
        hca->fmr_props.max_pages = FIREHOSE_CLIENT_MAXREGION_SIZE / GASNET_PAGESIZE;
      }
    }
    #endif

    /* Now initialize firehose */
    {
      uint32_t flags = 0;

      #if GASNETC_PIN_SEGMENT
        /* Adjust for the pinned segment (which is not advertised to firehose as prepinned) */
        gasneti_assert_always(firehose_mem > maxsize);
        firehose_mem -= maxsize;
        gasneti_assert_always(firehose_reg > gasnetc_seg_reg_count);
        firehose_reg -= gasnetc_seg_reg_count;

	flags |= FIREHOSE_INIT_FLAG_LOCAL_ONLY;
      #endif
      #if defined(__APPLE__) && defined(__MACH__)
	flags |= FIREHOSE_INIT_FLAG_UNPIN_ON_FINI;
      #endif

      firehose_init(firehose_mem, firehose_reg,
		    prereg, reg_count, flags, &gasnetc_firehose_info);
    }

    /* Determine alignment (and max size) for fh requests - a power-of-two <= max_region/2 */
    maxsz = MIN(gasnetc_max_msg_sz, gasnetc_firehose_info.max_LocalPinSize);
    #if !GASNETC_PIN_SEGMENT
      maxsz = MIN(maxsz, gasnetc_firehose_info.max_RemotePinSize);
    #endif
    gasneti_assert_always(maxsz >= (GASNET_PAGESIZE + gasnetc_inline_limit));
    gasnetc_fh_align = GASNET_PAGESIZE;
    while ((gasnetc_fh_align * 2) <= (maxsz / 2)) {
      gasnetc_fh_align *= 2;
    }
    gasnetc_fh_align_mask = gasnetc_fh_align - 1;
  }

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasneti_bootstrapBarrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

  gasneti_assert(gasneti_seginfo[gasneti_mynode].addr == segbase &&
         gasneti_seginfo[gasneti_mynode].size == segsize);

  gasneti_auxseg_attach(); /* provide auxseg */

  gasnete_init(); /* init the extended API */

  /* ensure extended API is initialized across nodes */
  gasneti_bootstrapBarrier();

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
/*
  Exit handling code
*/

#ifndef GASNETI_HAVE_ATOMIC_CAS
  #error "required atomic compare-and-swap is not yet implemented for your CPU/OS/compiler"
#endif

gasneti_atomic_t gasnetc_exit_running = gasneti_atomic_init(0);		/* boolean used by GASNETC_IS_EXITING */

static gasneti_atomic_t gasnetc_exit_code = gasneti_atomic_init(0);	/* value to _exit() with */
static gasneti_atomic_t gasnetc_exit_reqs = gasneti_atomic_init(0);	/* count of remote exit requests */
static gasneti_atomic_t gasnetc_exit_reps = gasneti_atomic_init(0);	/* count of remote exit replies */
static gasneti_atomic_t gasnetc_exit_done = gasneti_atomic_init(0);	/* flag to show exit coordination done */
static gasnetc_counter_t gasnetc_exit_repl_oust = GASNETC_COUNTER_INITIALIZER; /* track send of our AM reply */
static int gasnetc_exit_in_signal = 0;	/* to avoid certain things in signal context */

#define GASNETC_ROOT_NODE 0

enum {
  GASNETC_EXIT_ROLE_UNKNOWN,
  GASNETC_EXIT_ROLE_MASTER,
  GASNETC_EXIT_ROLE_SLAVE
};

static gasneti_atomic_t gasnetc_exit_role = gasneti_atomic_init(GASNETC_EXIT_ROLE_UNKNOWN);

extern void gasnetc_fatalsignal_callback(int sig) {
  gasnetc_exit_in_signal = 1;
}

/*
 * Code to disable user's AM handlers when exiting.  We need this because we must call
 * AMPoll to run system-level handlers, including ACKs for flow control.
 *
 * We do it this way because it adds absolutely nothing the normal execution path.
 * Thanks to Dan for the suggestion.
 */
static void gasnetc_noop(void) { return; }
static void gasnetc_disable_AMs(void) {
  int i;

  for (i = 0; i < GASNETC_MAX_NUMHANDLERS; ++i) {
    gasnetc_handler[i] = (gasnetc_handler_fn_t)&gasnetc_noop;
  }
}

/*
 * gasnetc_exit_role_reqh()
 *
 * This request handler (invoked only on the "root" node) handles the election
 * of a single exit "master", who will coordinate an orderly shutdown.
 */
static void gasnetc_exit_role_reqh(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  gasnet_node_t src;
  int local_role, result;
  int rc;

  gasneti_assert(numargs == 0);
  gasneti_assert(gasneti_mynode == GASNETC_ROOT_NODE);	/* May only send this request to the root node */

  
  /* What role would the local node get if the requester is made the master? */
  rc = gasnet_AMGetMsgSource(token, &src);
  gasneti_assert(rc == GASNET_OK);
  local_role = (src == GASNETC_ROOT_NODE) ? GASNETC_EXIT_ROLE_MASTER : GASNETC_EXIT_ROLE_SLAVE;

  /* Try atomically to assume the proper role.  Result determines role of requester */
  result = gasneti_atomic_compare_and_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, local_role)
                ? GASNETC_EXIT_ROLE_MASTER : GASNETC_EXIT_ROLE_SLAVE;

  /* Inform the requester of the outcome. */
  rc = gasnetc_ReplySystem(token, NULL, gasneti_handleridx(gasnetc_SYS_exit_role_rep),
			   1, (gasnet_handlerarg_t)result);
  gasneti_assert(rc == GASNET_OK);
}

/*
 * gasnetc_exit_role_reph()
 *
 * This reply handler receives the result of the election of an exit "master".
 * The reply contains the exit "role" this node should assume.
 */
static void gasnetc_exit_role_reph(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  int role;

  #if GASNET_DEBUG
  {
    gasnet_node_t src;
    int rc;

    rc = gasnet_AMGetMsgSource(token, &src);
    gasneti_assert(rc == GASNET_OK);
    gasneti_assert(src == GASNETC_ROOT_NODE);	/* May only receive this reply from the root node */
  }
  #endif

  /* What role has this node been assigned? */
  gasneti_assert(args != NULL);
  gasneti_assert(numargs == 1);
  role = (int)args[0];
  gasneti_assert((role == GASNETC_EXIT_ROLE_MASTER) || (role == GASNETC_EXIT_ROLE_SLAVE));

  /* Set the role if not yet set.  Then assert that the assigned role has been assumed.
   * This way the assertion is checking that if the role was obtained by other means
   * (namely by receiving an exit request) it must match the election result. */
  gasneti_atomic_compare_and_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, role);
  gasneti_assert(gasneti_atomic_read(&gasnetc_exit_role) == role);
}

/*
 * gasnetc_get_exit_role()
 *
 * This function returns the exit role immediately if known.  Otherwise it sends an AMRequest
 * to determine its role and then polls the network until the exit role is determined, either
 * by the reply to that request, or by a remote exit request.
 *
 * Should be called with an alarm timer in-force in case we get hung sending or the root node
 * is not responsive.
 *
 * Note that if we get here as a result of a remote exit request then our role has already been
 * set to "slave" and we won't touch the network from inside the request handler.
 */
static int gasnetc_get_exit_role()
{
  int role;

  role = gasneti_atomic_read(&gasnetc_exit_role);
  if (role == GASNETC_EXIT_ROLE_UNKNOWN) {
    int rc;

    /* Don't know our role yet.  So, send a system-category AM Request to determine our role */
    rc = gasnetc_RequestSystem(GASNETC_ROOT_NODE, NULL,
		    	       gasneti_handleridx(gasnetc_SYS_exit_role_req), 0);
    gasneti_assert(rc == GASNET_OK);

    /* Now spin until somebody tells us what our role is */
    do {
      gasnetc_sndrcv_poll(); /* works even before _attach */
      role = gasneti_atomic_read(&gasnetc_exit_role);
    } while (role == GASNETC_EXIT_ROLE_UNKNOWN);
  }

  return role;
}

/* gasnetc_exit_head
 *
 * All exit paths pass through here as the first step.
 * This function ensures that gasnetc_exit_code is written only once
 * by the first call.
 * It also lets the handler for remote exit requests know if a local
 * request has already begun.
 *
 * returns non-zero on the first call only
 * returns zero on all subsequent calls
 */
static int gasnetc_exit_head(int exitcode) {
  static gasneti_atomic_t once = gasneti_atomic_init(1);
  int retval;

  gasneti_atomic_set(&gasnetc_exit_running, 1);

  retval = gasneti_atomic_decrement_and_test(&once);

  if (retval) {
    /* Store the exit code for later use */
    gasneti_atomic_set(&gasnetc_exit_code, exitcode);
  }

  return retval;
}

/* gasnetc_exit_now
 *
 * First we set the atomic variable gasnetc_exit_done to allow the exit
 * of any threads which are spinning on it in gasnetc_exit().
 * Then this function tries hard to actually terminate the calling thread.
 * If for some unlikely reason gasneti_killmyprocess() returns, we abort().
 *
 * DOES NOT RETURN
 */
static void gasnetc_exit_now(int) GASNETI_NORETURN;
static void gasnetc_exit_now(int exitcode) {
  /* If anybody is still waiting, let them go */
  gasneti_atomic_set(&gasnetc_exit_done, 1);

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_exit(): node %i/%i calling killmyprocess...\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif
  gasneti_killmyprocess(exitcode);
  /* NOT REACHED */

  gasneti_reghandler(SIGABRT, SIG_DFL);
  abort();
  /* NOT REACHED */
}

/* gasnetc_exit_tail
 *
 * This the final exit code for the cases of local or remote requested exits.
 * It is not used for the return-from-main case.  Nor is this code used if a fatal
 * signal (including SIGALRM on timeout) is encountered while trying to shutdown.
 *
 * Just a wrapper around gasnetc_exit_now() to actually terminate.
 *
 * DOES NOT RETURN
 */
static void gasnetc_exit_tail(void) GASNETI_NORETURN;
static void gasnetc_exit_tail(void) {
  gasnetc_exit_now((int)gasneti_atomic_read(&gasnetc_exit_code));
  /* NOT REACHED */
}

/* gasnetc_exit_sighandler
 *
 * This signal handler is for a last-ditch exit when a signal arrives while
 * attempting the graceful exit.  That includes SIGALRM if we get wedged.
 *
 * Just a (verbose) signal-handler wrapper for gasnetc_exit_now().
 *
 * DOES NOT RETURN
 */
#if GASNET_DEBUG
  static const char * volatile gasnetc_exit_state = "UNKNOWN STATE";
  #define GASNETC_EXIT_STATE(st) gasnetc_exit_state = st
#else
  #define GASNETC_EXIT_STATE(st) do {} while (0)
#endif
static void gasnetc_exit_sighandler(int sig) {
  int exitcode = (int)gasneti_atomic_read(&gasnetc_exit_code);
  static gasneti_atomic_t once = gasneti_atomic_init(1);

  #if GASNET_DEBUG
  /* note - can't call trace macros here, or even sprintf */
  if (sig == SIGALRM) {
    static const char msg[] = "gasnet_exit(): timeout during exit... goodbye.  [";
    const char * state = gasnetc_exit_state;
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    write(STDERR_FILENO, state, strlen(state));
    write(STDERR_FILENO, "]\n", 2);
  } else {
    static const char msg1[] = "gasnet_exit(): signal ";
    static const char msg2[] = " received during exit... goodbye.  [";
    const char * state = gasnetc_exit_state;
    char digit;

    write(STDERR_FILENO, msg1, sizeof(msg1) - 1);

    /* assume sig < 100 */
    if (sig > 9) {
      digit = '0' + ((sig / 10) % 10);
      write(STDERR_FILENO, &digit, 1);
    }
    digit = '0' + (sig % 10);
    write(STDERR_FILENO, &digit, 1);
    
    write(STDERR_FILENO, msg2, sizeof(msg2) - 1);
    write(STDERR_FILENO, state, strlen(state));
    write(STDERR_FILENO, "]\n", 2);
  }
  #endif

  if (gasneti_atomic_decrement_and_test(&once)) {
    /* We ask the bootstrap support to kill us, but only once */
    gasneti_reghandler(SIGALRM, gasnetc_exit_sighandler);
    GASNETC_EXIT_STATE("in suicide timer");
    alarm(5);
    gasneti_bootstrapAbort(exitcode);
  } else {
    gasnetc_exit_now(exitcode);
  }

  /* NOT REACHED */
}

/* gasnetc_exit_master
 *
 * We say a polite goodbye to our peers and then listen for their replies.
 * This forms the root node's portion of a barrier for graceful shutdown.
 *
 * The "goodbyes" are just a system-category AM containing the desired exit code.
 * The AM helps ensure that on non-collective exits the "other" nodes know to exit.
 * If we see a "goodbye" from all of our peers we know we've managed to coordinate
 * an orderly shutdown.  If not, then in gasnetc_exit_body() we can ask the bootstrap
 * support to kill the job in a less graceful way.
 *
 * Takes the exitcode and a timeout in us as arguments
 *
 * Returns 0 on success, non-zero on any sort of failure including timeout.
 */
static int gasnetc_exit_master(int exitcode, int64_t timeout_us) {
  int i, rc;
  int64_t start_time;

  gasneti_assert(timeout_us > 0); 

  start_time = gasneti_getMicrosecondTimeStamp();

  /* Notify phase */
  for (i = 0; i < gasneti_nodes; ++i) {
    if (i == gasneti_mynode) continue;

    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    rc = gasnetc_RequestSystem(i, NULL,
		    	       gasneti_handleridx(gasnetc_SYS_exit_req),
			       1, (gasnet_handlerarg_t)exitcode);
    if (rc != GASNET_OK) return -1;
  }

  /* Wait phase - wait for replies from our N-1 peers */
  while (gasneti_atomic_read(&gasnetc_exit_reps) < (gasneti_nodes - 1)) {
    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    gasnetc_sndrcv_poll(); /* works even before _attach */
  }

  return 0;
}

/* gasnetc_exit_slave
 *
 * We wait for a polite goodbye from the exit master.
 *
 * Takes a timeout in us as an argument
 *
 * Returns 0 on success, non-zero on timeout.
 */
static int gasnetc_exit_slave(int64_t timeout_us) {
  int64_t start_time;

  gasneti_assert(timeout_us > 0); 

  start_time = gasneti_getMicrosecondTimeStamp();

  /* wait until the exit request is received from the master */
  while (gasneti_atomic_read(&gasnetc_exit_reqs) == 0) {
    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    gasnetc_sndrcv_poll(); /* works even before _attach */
  }

  /* wait until out reply has been placed on the wire */
  gasneti_sync_reads(); /* For non-atomic portion of gasnetc_exit_repl_oust */
  gasnetc_counter_wait(&gasnetc_exit_repl_oust, 1);

  return 0;
}

/* gasnetc_exit_body
 *
 * This code is common to all the exit paths and is used to perform a hopefully graceful exit in
 * all cases.  To coordinate a graceful shutdown gasnetc_get_exit_role() will select one node as
 * the "master".  That master node will then send a remote exit request to each of its peers to
 * ensure they know that it is time to exit.  If we fail to coordinate the shutdown, we ask the
 * bootstrap to shut us down agressively.  Otherwise we return to our caller.  Unless our caller
 * is the at-exit handler, we are typically followed by a call to gasnetc_exit_tail() to perform
 * the actual termination.  Note also that this function will block all calling threads other than
 * the first until the shutdown code has been completed.
 *
 * XXX: timeouts contained here are entirely arbitrary
 */
static void gasnetc_exit_body(void) {
  int i, role, exitcode;
  int graceful = 0;
  int64_t timeout_us;

  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  (void)gasneti_reghandler(SIGQUIT, SIG_IGN);

  /* Ensure only one thread ever continues past this point.
   * Others will spin here until time to die.
   * We can't/shouldn't use mutex code here since it is not signal-safe.
   */
  #ifdef GASNETI_USE_GENERIC_ATOMICOPS
    #error "We need real atomic ops with signal-safety for gasnet_exit..."
  #endif
  {
    static gasneti_atomic_t exit_lock = gasneti_atomic_init(1);
    if (!gasneti_atomic_decrement_and_test(&exit_lock)) {
      /* poll until it is time to exit */
      while (!gasneti_atomic_read(&gasnetc_exit_done)) {
        gasneti_sched_yield(); /* NOT safe to use sleep() here - conflicts with alarm() */
      }
      gasnetc_exit_tail();
      /* NOT REACHED */
    }
  }

  /* read exit code, stored by first caller to gasnetc_exit_head() */
  exitcode = gasneti_atomic_read(&gasnetc_exit_code);

  /* Establish a last-ditch signal handler in case of failure. */
  alarm(0);
  gasneti_reghandler(SIGALRM, gasnetc_exit_sighandler);
  #if GASNET_DEBUG
    gasneti_reghandler(SIGABRT, SIG_DFL);
  #else
    gasneti_reghandler(SIGABRT, gasnetc_exit_sighandler);
  #endif
  gasneti_reghandler(SIGILL,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGSEGV, gasnetc_exit_sighandler);
  gasneti_reghandler(SIGFPE,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGBUS,  gasnetc_exit_sighandler);

  /* Disable processing of AMs, except system-level ones */
  gasnetc_disable_AMs();

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  /* Try to flush out all the output, allowing upto 30s */
  GASNETC_EXIT_STATE("flushing output");
  alarm(30);
  {
    gasneti_flush_streams();
    gasneti_trace_finish();
    alarm(0);
    gasneti_sched_yield();
  }

  /* Determine our role (master or slave) in the coordination of this shutdown */
  GASNETC_EXIT_STATE("determining exit role");
  alarm(10);
  role = gasnetc_get_exit_role();

  /* Attempt a coordinated shutdown */
  GASNETC_EXIT_STATE("coordinating shutdown");
  timeout_us = 2000000 + gasneti_nodes*250000; /* 2s + 0.25s * nodes */
  alarm(1 + timeout_us/1000000);
  switch (role) {
  case GASNETC_EXIT_ROLE_MASTER:
    /* send all the remote exit requests and wait for the replies */
    graceful = (gasnetc_exit_master(exitcode, timeout_us) == 0);
    break;

  case GASNETC_EXIT_ROLE_SLAVE:
    /* wait for the exit request and reply before proceeding */
    graceful = (gasnetc_exit_slave(timeout_us) == 0);
    break;

  default:
      gasneti_fatalerror("invalid exit role");
  }

  /* Clean up transport resources, allowing upto 30s */
  alarm(30);
  { gasnetc_hca_t *hca = NULL;
    GASNETC_EXIT_STATE("in gasnetc_sndrcv_fini_peer()");
    for (i = 0; i < gasneti_nodes; ++i) {
      gasnetc_sndrcv_fini_peer(i);
    }
    GASNETC_EXIT_STATE("in gasnetc_sndrcv_fini()");
    gasnetc_sndrcv_fini();
    if (gasneti_attach_done) {
      if (GASNETC_USE_FIREHOSE && !gasnetc_exit_in_signal) {
	/* Note we skip firehose_fini() on exit via a signal */
        GASNETC_EXIT_STATE("in firehose_fini()");
        firehose_fini();
      }
#if GASNETC_PIN_SEGMENT
      GASNETC_FOR_ALL_HCA(hca) {
        GASNETC_EXIT_STATE("in gasnetc_unpin()");
        for (i=0; i<gasnetc_seg_reg_count; ++i) {
      	  gasnetc_unpin(&hca->seg_reg[i]);
        }
        gasneti_free(hca->seg_reg);
      }
#endif
    }
    GASNETC_FOR_ALL_HCA(hca) {
      GASNETC_EXIT_STATE("in VAPI_dealloc_pd()");
      (void)VAPI_dealloc_pd(hca->handle, hca->pd);
      if (!gasnetc_use_rcv_thread)	{
        /* can't release if we could possibly be inside the RCV thread */
        GASNETC_EXIT_STATE("in EVAPI_release_hca_hndl()");
        (void)EVAPI_release_hca_hndl(hca->handle);
      }
    }
  }

  /* Try again to flush out any recent output, allowing upto 5s */
  GASNETC_EXIT_STATE("closing output");
  alarm(5);
  {
    gasneti_flush_streams();
    #if !GASNET_DEBUG_VERBOSE
      gasneti_close_streams();
    #endif
  }

  /* XXX potential problems here if exiting from the "Wrong" thread, or from a signal handler */
  alarm(10);
  {
    if (graceful) {
      #if GASNET_DEBUG_VERBOSE
	fprintf(stderr, "Graceful exit initiated by node %d\n", (int)gasneti_mynode);
      #endif
      GASNETC_EXIT_STATE("in gasneti_bootstrapFini()");
      gasneti_bootstrapFini();
    } else {
      /* We couldn't reach our peers, so hope the bootstrap code can kill the entire job */
      #if GASNET_DEBUG_VERBOSE
	fprintf(stderr, "Ungraceful exit initiated by node %d\n", (int)gasneti_mynode);
      #endif
      GASNETC_EXIT_STATE("in gasneti_bootstrapAbort()");
      gasneti_bootstrapAbort(exitcode);
      /* NOT REACHED */
    }
  }

  alarm(0);
}

/* gasnetc_exit_reqh
 *
 * This is a system-category AM handler and is therefore available as soon as gasnet_init()
 * returns, even before gasnet_attach().  This handler is responsible for receiving the
 * remote exit requests from the master node and replying.  We call gasnetc_exit_head()
 * with the exitcode seen in the remote exit request.  If this remote request is seen before
 * any local exit requests (normal or signal), then we are also responsible for starting the
 * exit procedure, via gasnetc_exit_{body,tail}().  Additionally, we are responsible for
 * firing off a SIGQUIT to let the user's handler, if any, run before we begin to exit.
 */
static void gasnetc_exit_reqh(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  int rc;

  gasneti_assert(args != NULL);
  gasneti_assert(numargs == 1);

  /* The master will send this AM, but should _never_ receive it */
  gasneti_assert(gasneti_atomic_read(&gasnetc_exit_role) != GASNETC_EXIT_ROLE_MASTER);

  /* We should never receive this AM multiple times */
  gasneti_assert(gasneti_atomic_read(&gasnetc_exit_reqs) == 0);

  /* If we didn't already know, we are now certain our role is "slave" */
  (void)gasneti_atomic_compare_and_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, GASNETC_EXIT_ROLE_SLAVE);

  /* Send a reply so the master knows we are reachable */
  rc = gasnetc_ReplySystem(token, &gasnetc_exit_repl_oust,
		  	   gasneti_handleridx(gasnetc_SYS_exit_rep), /* no args */ 0);
  gasneti_assert(rc == GASNET_OK);

  /* Count the exit requests, so gasnetc_exit_slave() knows when to return */
  gasneti_sync_writes(); /* For non-atomic portion of gasnetc_exit_repl_oust */
  gasneti_atomic_increment(&gasnetc_exit_reqs);

  /* Initiate an exit IFF this is the first we've heard of it */
  if (gasnetc_exit_head(args[0])) {
    gasneti_sighandlerfn_t handler;
    /* IMPORTANT NOTE
     * When we reach this point we are in a request handler which will never return.
     * Care should be taken to ensure this doesn't wedge the AM recv logic.
     *
     * This is currently safe because:
     * 1) request handlers are run w/ no locks held
     * 2) we poll for AMs in all the places we need them
     */

    /* To try and be reasonably robust, want to avoid performing the shutdown and exit from signal
     * context if we can avoid it.  However, we must raise SIGQUIT if the user has registered a handler.
     * Therefore we inspect what is registered before calling raise().
     *
     * XXX we don't do this atomically w.r.t the signal
     * XXX we don't do the right thing w/ SIG_ERR and SIG_HOLD
     */
    handler = gasneti_reghandler(SIGQUIT, SIG_IGN);
    if ((handler != gasneti_defaultSignalHandler) &&
#ifdef SIG_HOLD
	(handler != (gasneti_sighandlerfn_t)SIG_HOLD) &&
#endif
	(handler != (gasneti_sighandlerfn_t)SIG_ERR) &&
	(handler != (gasneti_sighandlerfn_t)SIG_IGN) &&
	(handler != (gasneti_sighandlerfn_t)SIG_DFL)) {
      (void)gasneti_reghandler(SIGQUIT, handler);
      #if 1
        raise(SIGQUIT);
        /* Note: Both ISO C and POSIX assure us that raise() won't return until after the signal handler
         * (if any) has executed.  However, if that handler calls gasnetc_exit(), we'll never return here. */
      #elif 0
	kill(getpid(),SIGQUIT);
      #else
	handler(SIGQUIT);
      #endif
    } else {
      /* No need to restore the handler, since _exit_body will set it to SIG_IGN anyway. */
    }

    gasnetc_exit_body();
    gasnetc_exit_tail();
    /* NOT REACHED */
  }

  return;
}

/* gasnetc_exit_reph
 *
 * Simply count replies
 */
static void gasnetc_exit_reph(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  gasneti_assert(numargs == 0);

  gasneti_atomic_increment(&gasnetc_exit_reps);
}
  
/* gasnetc_atexit
 *
 * This is a simple atexit() handler to achieve a hopefully graceful exit.
 * We use the functions gasnetc_exit_{head,body}() to coordinate the shutdown.
 * Note that we don't call gasnetc_exit_tail() since we anticipate the normal
 * exit() procedures to shutdown the multi-threaded process nicely and also
 * because we don't have access to the exit code!
 *
 * Unfortunately, we don't have access to the exit code to send to the other
 * nodes in the event this is a non-collective exit.  However, experience with at
 * lease one MPI suggests that when using MPI for bootstrap a non-zero return from
 * at least one executable is sufficient to produce that non-zero exit code from
 * the parallel job.  Therefore, we can "safely" pass 0 to our peers and still
 * expect to preserve a non-zero exit code for the GASNet job as a whole.  Of course
 * there is no _guarantee_ this will work with all bootstraps.
 *
 * XXX: consider autoconf probe for on_exit()
 */
static void gasnetc_atexit(void) {
  /* Check return from _head to avoid reentrance */
  if (gasnetc_exit_head(0)) { /* real exit code is outside our control */
    gasnetc_exit_body();
  }
  return;
}

/* gasnetc_exit
 *
 * This is the start of a locally requested exit from GASNet.
 * The caller might be the user, some part of the conduit which has detected an error,
 * or possibly gasneti_defaultSignalHandler() responding to a termination signal.
 */
extern void gasnetc_exit(int exitcode) {
  gasnetc_exit_head(exitcode);
  gasnetc_exit_body();
  gasnetc_exit_tail();
  /* NOT REACHED */
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_RequestGeneric(gasnetc_Short, dest, handler,
		  		  NULL, 0, NULL,
				  numargs, NULL, argptr);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_RequestGeneric(gasnetc_Medium, dest, handler,
		  		  source_addr, nbytes, NULL,
				  numargs, NULL, argptr);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  gasnetc_counter_t mem_oust = GASNETC_COUNTER_INITIALIZER;
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_RequestGeneric(gasnetc_Long, dest, handler,
		  		  source_addr, nbytes, dest_addr,
				  numargs, &mem_oust, argptr);

  /* block for completion of RDMA transfer */
  gasnetc_counter_wait(&mem_oust, 0);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_RequestGeneric(gasnetc_Long, dest, handler,
		  		  source_addr, nbytes, dest_addr,
				  numargs, NULL, argptr);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_ReplyGeneric(gasnetc_Short, token, handler,
		  		NULL, 0, NULL,
				numargs, NULL, argptr);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_ReplyGeneric(gasnetc_Medium, token, handler,
		  		source_addr, nbytes, NULL,
				numargs, NULL, argptr);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyLongM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs); 
  va_start(argptr, numargs); /*  pass in last argument */

  #if GASNETC_PIN_SEGMENT
  {
    gasnetc_counter_t mem_oust = GASNETC_COUNTER_INITIALIZER;

    retval = gasnetc_ReplyGeneric(gasnetc_Long, token, handler,
		  		  source_addr, nbytes, dest_addr,
				  numargs, &mem_oust, argptr);

    /* block for completion of RDMA transfer */
    gasnetc_counter_wait(&mem_oust, 1 /* calling from a request handler */);
  }
  #else
  retval = gasnetc_ReplyGeneric(gasnetc_Long, token, handler,
		  		source_addr, nbytes, dest_addr,
				numargs, NULL, argptr);

  #endif

  va_end(argptr);
  GASNETI_RETURN(retval);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
  This section is only required for conduits that may use interrupt-based handler dispatch
  See the GASNet spec and http://www.cs.berkeley.edu/~bonachea/upc/gasnet.html for
    philosophy and hints on efficiently implementing no-interrupt sections
  Note: the extended-ref implementation provides a thread-specific void* within the 
    gasnete_threaddata_t data structure which is reserved for use by the core 
    (and this is one place you'll probably want to use it)
*/
#if GASNETC_USE_INTERRUPTS
  #error interrupts not implemented
  extern void gasnetc_hold_interrupts() {
    GASNETI_CHECKATTACH();
    /* add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts() {
    GASNETI_CHECKATTACH();
    /* add code here to re-enable handler interrupts for _this_ thread */
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
#if !GASNETC_NULL_HSL
extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_init(&(hsl->lock));

  #if GASNETC_USE_INTERRUPTS
    /* add code here to init conduit-specific HSL state */
    #error interrupts not implemented
  #endif
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_destroy(&(hsl->lock));

  #if GASNETC_USE_INTERRUPTS
    /* add code here to cleanup conduit-specific HSL state */
    #error interrupts not implemented
#endif
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  {
    #if GASNETI_STATS_OR_TRACE
      gasneti_stattime_t startlock = GASNETI_STATTIME_NOW_IFENABLED(L);
    #endif
    #if GASNETC_HSL_SPINLOCK
      while (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) { }
    #else
      gasneti_mutex_lock(&(hsl->lock));
    #endif
    #if GASNETI_STATS_OR_TRACE
      hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
      GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
    #endif
  }

  #if GASNETC_USE_INTERRUPTS
    /* conduits with interrupt-based handler dispatch need to add code here to 
       disable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock acquire and we're not inside an enclosing no-interrupt section)
     */
    #error interrupts not implemented
  #endif
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  #if GASNETC_USE_INTERRUPTS
    /* conduits with interrupt-based handler dispatch need to add code here to 
       re-enable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock release and we're not inside an enclosing no-interrupt section)
     */
    #error interrupts not implemented
  #endif

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW_IFENABLED(L)-hsl->acquiretime);

  gasneti_mutex_unlock(&(hsl->lock));
}

extern int  gasnetc_hsl_trylock(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  {
    int locked = (gasneti_mutex_trylock(&(hsl->lock)) == 0);

    GASNETI_TRACE_EVENT_VAL(L, HSL_TRYLOCK, locked);
    if (locked) {
      #if GASNETI_STATS_OR_TRACE
        hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
      #endif
      #if GASNETC_USE_INTERRUPTS
        /* conduits with interrupt-based handler dispatch need to add code here to 
           disable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock acquire and we're not inside an enclosing no-interrupt section)
         */
        #error interrupts not implemented
      #endif
    }

    return locked ? GASNET_OK : GASNET_ERR_NOT_READY;
  }
}
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Private Handlers:
  ================
  see mpi-conduit and extended-ref for examples on how to declare AM handlers here
  (for internal conduit use in bootstrapping, job management, etc.)
*/
static gasnet_handlerentry_t const gasnetc_handlers[] = {
  #ifdef GASNETC_AUXSEG_HANDLERS
    GASNETC_AUXSEG_HANDLERS(),
  #endif
  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/*
  System handlers, available even between _init and _attach
*/

const gasnetc_sys_handler_fn_t gasnetc_sys_handler[GASNETC_MAX_NUMHANDLERS] = {
  NULL,	/* ACK: NULL -> do nothing */
  gasnetc_exit_role_reqh,
  gasnetc_exit_role_reph,
  gasnetc_exit_reqh,
  gasnetc_exit_reph,
  NULL,
};
  
/* ------------------------------------------------------------------------------------ */

/* 
  Pthread_create wrapper:
  ======================
  On Darwin we see that if the stacks of pthreads become dynamically pinned,
  then bad things are happening when they are later unmapped.  To deal with
  this we are wrapping pthread_create() to provide our own stacks, which will
  not be unmapped automatically, and additionally we are passing the flag
  FIREHOSE_INIT_FLAG_UNPIN_ON_FINI to unpin all memory before it gets unmapped.
  XXX: Eventually we should be able to firehose_local_invalidate() the stacks
  we create and unmap them rather than leaking them as we do now.
 */

#if defined(GASNETC_PTHREAD_CREATE_OVERRIDE)
  /* Configuration */
  #if defined(__APPLE__) && defined(__MACH__)
    #include <sys/mman.h>	/* For mprotect */
    #define GASNETC_DEFAULT_PTHREAD_STACKSZ (512*1024)
    #define GASNETC_PTHREAD_STACK_DIR (-1)
  #else
    #error "Override of pthread_create() attempted on unsupported platform"
  #endif

  struct gasnetc_pthread_args {
    void			*real_arg;
    void			*(*real_fn)(void *);
    void			*stackaddr;	/* exclusive of guard pages */
    size_t			stacksize;	/* exclusive of guard pages */
    struct gasnetc_pthread_args	*next;		/* once stacks are unused */
  };

  static struct {
    gasneti_mutex_t		lock;
    struct gasnetc_pthread_args	*list;
  } gasnetc_pthread_dead_stacks = {GASNETI_MUTEX_INITIALIZER, NULL};

  static void gasnetc_pthread_cleanup_fn(void *arg)
  {
    /* Here we form a linked list of the "dead" stacks.
     * However, we can't reuse them until the threads are join()ed since
     * the threads are STILL RUNNING (this very function) when linked in.
     * XXX: Resolve this by overriding pthread_join() and linking there?
     */
    struct gasnetc_pthread_args *args = arg;

    gasneti_mutex_lock(&gasnetc_pthread_dead_stacks.lock);
    args->next = gasnetc_pthread_dead_stacks.list;
    gasnetc_pthread_dead_stacks.list = args;
    gasneti_mutex_unlock(&gasnetc_pthread_dead_stacks.lock);
  }

  static void *gasnetc_pthread_start_fn(void *arg)
  {
    struct gasnetc_pthread_args *args = arg;
    void *retval = NULL;

    pthread_cleanup_push(&gasnetc_pthread_cleanup_fn, arg);
    retval = (*args->real_fn)(args->real_arg);
    pthread_cleanup_pop(1);

    return retval;
  }

  extern int gasnetc_pthread_create(gasneti_pthread_create_fn_t *create_fn, pthread_t *thread, const pthread_attr_t *attr, void * (*fn)(void *), void * arg) {
    pthread_attr_t my_attr = *attr; /* Copy it to maintain 'const' */
    struct gasnetc_pthread_args *args;
    size_t stacksize;
    void *stackaddr;

    /* Get caller's stack size and address */
    gasneti_assert_zeroret(pthread_attr_getstackaddr(&my_attr, &stackaddr));
    gasneti_assert_zeroret(pthread_attr_getstacksize(&my_attr, &stacksize));

    /* Use system's default stacksize if caller passed zero */
    if (!stacksize) {
      #ifdef GASNETC_DEFAULT_PTHREAD_STACKSZ
        stacksize = GASNETC_DEFAULT_PTHREAD_STACKSZ;
      #else
        pthread_attr_t tmp_attr;
        pthread_attr_init(&tmp_attr);
        pthread_attr_getstacksize(&tmp_attr, &stacksize);
        pthread_attr_destroy(&tmp_attr);
        if (!stacksize) {
          gasneti_fatalerror("Failed to determine stack size in gasnetc_pthread_create()");
        }
      #endif
    }

    if (stackaddr) {
      /* XXX: should we just assume they know what they are doing? */
      gasneti_fatalerror("gasnetc_pthread_create() does not support caller provided stack address");
    } else {
      /* Allocate memory w/ room for guard pages at both ends.
       * Note that these are not just for debugging/protection.
       * They also ensure no coalescing with adjacent pinned regions.
       * XXX: Coalescing doesn't matter yet, since we aren't doing
       * a firehose_local_invalidate() on them (we leak them instead).
       */
      stackaddr = gasneti_mmap(stacksize + 2 * GASNET_PAGESIZE);
      gasneti_assert_zeroret(mprotect(stackaddr, GASNET_PAGESIZE, PROT_NONE));
      gasneti_assert_zeroret(mprotect((void *)((uintptr_t)stackaddr + stacksize + GASNET_PAGESIZE), GASNET_PAGESIZE, PROT_NONE));
      #if (GASNETC_PTHREAD_STACK_DIR < 0)	/* stack grows down */
        stackaddr = (void *)((uintptr_t)stackaddr + stacksize + GASNET_PAGESIZE);
      #elif (GASNETC_PTHREAD_STACK_DIR > 0)	/* stack grows up */
        stackaddr = (void *)((uintptr_t)stackaddr + GASNET_PAGESIZE);
      #else
	#error "Don't know which way stacks grow"
      #endif
    }

    /* Set stack size/addr */
    gasneti_assert_zeroret(pthread_attr_setstackaddr(&my_attr, stackaddr));
    gasneti_assert_zeroret(pthread_attr_setstacksize(&my_attr, stacksize));

    /* Build args structure (leaked by design) */;
    args = gasneti_malloc(sizeof(*args));
    args->real_fn = fn;
    args->real_arg = arg;
    args->stackaddr = stackaddr;
    args->stacksize = stacksize;
    args->next = NULL;

    return (*create_fn)(thread, &my_attr, &gasnetc_pthread_start_fn, args);
  }
#endif /* defined(GASNETC_PTHREAD_CREATE_OVERRIDE) */
/* ------------------------------------------------------------------------------------ */
