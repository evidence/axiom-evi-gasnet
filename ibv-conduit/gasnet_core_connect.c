/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_connect.c,v $
 *     $Date: 2011/02/23 09:29:42 $
 * $Revision: 1.34 $
 * Description: Connection management code
 * Copyright 2011, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

#if GASNETC_IBV_XRC /* For open(), stat(), O_CREAT, etc. */
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <fcntl.h>
#endif

/*
  The following configuration cannot yet be overridden by environment variables.
*/
#if GASNET_CONDUIT_VAPI
  #define GASNETC_QP_PATH_MTU           MTU1024
  #define GASNETC_QP_MIN_RNR_TIMER      IB_RNR_NAK_TIMER_0_08
#else
  #define GASNETC_QP_PATH_MTU           IBV_MTU_1024
  #define GASNETC_QP_MIN_RNR_TIMER      6       /*IB_RNR_NAK_TIMER_0_08*/
#endif
#define GASNETC_QP_STATIC_RATE          0
#define GASNETC_QP_RNR_RETRY            7       /* retry forever, but almost never happens */

#define GASNETC_PSN(_sender, _qpi)  ((_sender)*gasnetc_alloc_qps + (_qpi))

/* Convenience iterators */
#define GASNETC_FOR_EACH_QPI(_conn_info, _qpi, _cep)  \
  for((_cep) = (_conn_info)->cep, (_qpi) = 0; \
      (_qpi) < gasnetc_alloc_qps; ++(_cep), ++(_qpi))

/* Common types */
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_t,       struct ibv_qp_attr)     gasnetc_qp_attr_t;
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_mask_t,  enum ibv_qp_attr_mask)  gasnetc_qp_mask_t;

/* Info used for connection establishment */
typedef struct {
  gasnetc_cep_t     *cep;        /* Vector of gasnet endpoints */
  gasnetc_qpn_t     *local_qpn;  /* Local qpns of connections */
  gasnetc_qpn_t     *remote_qpn; /* Remote qpns of connections */
#if GASNETC_IBV_XRC
  gasnetc_qpn_t     *local_xrc_qpn;  /* Local qpns of XRC rcv qps */
  gasnetc_qpn_t     *remote_xrc_qpn; /* Remote qpns of XRC rcv qps */
  uint32_t          *xrc_remote_srq_num; /* Remote SRQ numbers */
#endif
  const gasnetc_port_info_t **port;
} gasnetc_conn_info_t;

#if GASNETC_IBV_XRC
  #define GASNETC_SND_QP_NEEDS_MODIFY(_xrc_snd_qp,_state) \
	(!gasnetc_use_xrc || ((_xrc_snd_qp).state < (_state)))
#else
  #define GASNETC_SND_QP_NEEDS_MODIFY(_xrc_snd_qp,_state) 1
#endif


#if GASNETC_IBV_XRC
typedef struct gasnetc_xrc_snd_qp_s {
  gasnetc_qp_hndl_t handle;
  enum ibv_qp_state state;
  gasneti_semaphore_t *sq_sema_p;
} gasnetc_xrc_snd_qp_t;

static gasnetc_xrc_snd_qp_t *gasnetc_xrc_snd_qp = NULL;
#define GASNETC_NODE2SND_QP(_node) \
	(&gasnetc_xrc_snd_qp[gasneti_nodeinfo[(_node)] * gasnetc_alloc_qps])

static gasnetc_qpn_t *gasnetc_xrc_rcv_qpn = NULL;

/* Create one XRC rcv QP */
static int
gasnetc_xrc_create_qp(struct ibv_xrc_domain *xrc_domain, gasnet_node_t node, int qpi) {
  const int cep_idx = node * gasnetc_alloc_qps + qpi;
  gasneti_atomic32_t *rcv_qpn_p = (gasneti_atomic32_t *)(&gasnetc_xrc_rcv_qpn[cep_idx]);
  gasnetc_qpn_t rcv_qpn = 0;
  int ret;

  gasneti_assert(gasnetc_xrc_rcv_qpn != NULL);
  gasneti_assert(sizeof(gasnetc_qpn_t) == sizeof(gasneti_atomic32_t));

  /* Create the RCV QPs once per supernode and register in the non-creating node(s) */
  /* QPN==1 is reserved, so we can use it as a "lock" value */
  if (gasneti_atomic32_compare_and_swap(rcv_qpn_p, 0, 1, 0)) {
    struct ibv_qp_init_attr init_attr;
    init_attr.xrc_domain = xrc_domain;
    ret = ibv_create_xrc_rcv_qp(&init_attr, &rcv_qpn);
    GASNETC_VAPI_CHECK(ret, "from ibv_create_xrc_rcv_qp()");

    gasneti_atomic32_set(rcv_qpn_p, rcv_qpn, 0);
  } else {
    gasneti_waituntil(1 != (rcv_qpn = gasneti_atomic32_read(rcv_qpn_p, 0)));
    ret = ibv_reg_xrc_rcv_qp(xrc_domain, rcv_qpn);
    GASNETC_VAPI_CHECK(ret, "from ibv_reg_xrc_rcv_qp()");
  }

  return GASNET_OK;
}

static int
gasnetc_xrc_modify_qp(
        struct ibv_xrc_domain *xrc_domain,
        gasnetc_qpn_t xrc_qp_num,
        struct ibv_qp_attr *attr,
        enum ibv_qp_attr_mask attr_mask)
{
  int retval;

  retval = ibv_modify_xrc_rcv_qp(xrc_domain, xrc_qp_num, attr, attr_mask);

  if_pf (retval == EINVAL) {
    struct ibv_qp_attr      qp_attr;
    struct ibv_qp_init_attr qp_init_attr;
    int rc;

    rc = ibv_query_xrc_rcv_qp(xrc_domain, xrc_qp_num, &qp_attr, IBV_QP_STATE, &qp_init_attr);
    if (!rc && (qp_attr.qp_state >= attr->qp_state)) {
      /* No actual error, just a race against another process */
      retval = 0;
    }
  }

  return retval;
}

/* Perform a supernode-scoped broadcast from first node of the supernode.
   Note that 'src' must be a valid address on ALL callers.
 */
static void
gasnetc_supernode_bcast(void *src, size_t len, void *dst) {
  #if GASNET_PSHM
    /* Can do w/ just supernode-scoped broadcast */
    gasneti_assert(gasneti_request_pshmnet != NULL);
    gasneti_pshmnet_bootstrapBroadcast(gasneti_request_pshmnet, src, len, dst, 0);
  #else
    /* Need global Exchange when PSHM is not available */
    /* TODO: If/when is one Bcast per supernode cheaper than 1 Exchange? */
    /* TODO: Push this case down to the Bootstrap support? */
    void *all_data = gasneti_malloc(gasneti_nodes * len);
    void *my_dst = (void *)((uintptr_t )all_data + (len * gasneti_nodemap_local[0]));
    gasneti_bootstrapExchange(src, len, all_data);
    memcpy(dst, my_dst, len);
    gasneti_free(all_data);
  #endif
}

static void
gasnetc_supernode_barrier(void) {
  #if GASNET_PSHM
    gasneti_pshmnet_bootstrapBarrier();
  #else
    gasneti_bootstrapBarrier();
  #endif
}

/* XXX: Requires that at least the first call is collective */
static char*
gasnetc_xrc_tmpname(gasnetc_lid_t mylid, int index) {
  static char *tmpdir = NULL;
  static int tmpdir_len = -1;
  static pid_t pid;
  static const char pattern[] = "/GASNETxrc-%04x%01x-%06x"; /* Max 11 + 5 + 1 + 6 + 1 = 24 */
  const int filename_len = 24;
  char *filename;

  gasneti_assert(index >= 0  &&  index <= 16);

  /* Initialize tmpdir and pid only on first call */
  if (!tmpdir) {
    struct stat s;
    tmpdir = gasneti_getenv_withdefault("TMPDIR", "/tmp");
    if (stat(tmpdir, &s) || !S_ISDIR(s.st_mode)) {
      gasneti_fatalerror("XRC support requires valid $TMPDIR or /tmp");
    }
    tmpdir_len = strlen(tmpdir);

    /* Get PID of first proc per supernode */
    pid = getpid(); /* Redundant, but harmless on other processes */
    gasnetc_supernode_bcast(&pid, sizeof(pid), &pid);
  }

  filename = gasneti_malloc(tmpdir_len + filename_len);
  strcpy(filename, tmpdir);
  sprintf(filename + tmpdir_len, pattern,
          (unsigned int)(mylid & 0xffff),
          (unsigned int)(index & 0xf),
          (unsigned int)(pid & 0xffffff));
  gasneti_assert(strlen(filename) < (tmpdir_len + filename_len));

  return filename;
}

/* Create an XRC domain per HCA (once per supernode) and a shared memory file */
/* XXX: Requires that the call is collective */
static int
gasnetc_xrc_init(void) {
  const gasnetc_lid_t mylid = gasnetc_port_tbl[0].port.lid;
  char *filename[GASNETC_IB_MAX_HCAS+1];
  size_t flen;
  int index, fd;

  /* Use per-supernode filename to create common XRC domain once per HCA */
  GASNETC_FOR_ALL_HCA_INDEX(index) {
    gasnetc_hca_t *hca = &gasnetc_hca[index];
    filename[index] = gasnetc_xrc_tmpname(mylid, index);
    fd = open(filename[index], O_CREAT, S_IWUSR|S_IRUSR);
    if (fd < 0) {
      gasneti_fatalerror("failed to create xrc domain file '%s': %d:%s", filename[index], errno, strerror(errno));
    }
    hca->xrc_domain = ibv_open_xrc_domain(hca->handle, fd, O_CREAT);
    GASNETC_VAPI_CHECK_PTR(hca->xrc_domain, "from ibv_open_xrc_domain()");
    (void) close(fd);
  }

  /* Use one more per-supernode filename to create common shared memory file */
  /* TODO: Should PSHM combine this w/ the AM segment? */
  gasneti_assert(index == gasnetc_num_hcas);
  filename[index] = gasnetc_xrc_tmpname(mylid, index);
  fd = open(filename[index], O_CREAT|O_RDWR, S_IWUSR|S_IRUSR);
  if (fd < 0) {
    gasneti_fatalerror("failed to create xrc shared memory file '%s': %d:%s", filename[index], errno, strerror(errno));
  }
  flen = GASNETI_PAGE_ALIGNUP(sizeof(gasnetc_qpn_t) * gasneti_nodes * gasnetc_alloc_qps);
  if (ftruncate(fd, flen) < 0) {
    gasneti_fatalerror("failed to resize xrc shared memory file '%s': %d:%s", filename[index], errno, strerror(errno));
  }
  /* XXX: Is there anything else that can/should be packed into the same shared memory file? */
  gasnetc_xrc_rcv_qpn = mmap(NULL, flen, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (gasnetc_xrc_rcv_qpn == MAP_FAILED) {
    gasneti_fatalerror("failed to mmap xrc shared memory file '%s': %d:%s", filename[index], errno, strerror(errno));
  }
  (void) close(fd);

  /* Clean up once everyone is done w/ all files */
  gasnetc_supernode_barrier();
  GASNETC_FOR_ALL_HCA_INDEX(index) {
    (void)unlink(filename[index]); gasneti_free(filename[index]);
  }
  gasneti_assert(index == gasnetc_num_hcas);
  (void)unlink(filename[index]); gasneti_free(filename[index]);

  return GASNET_OK;
}
#endif /* GASNETC_IBV_XRC */

/* Distribute the qps to each peer round-robin over the ports.
   Returns NULL for cases that should not have any connection */
static const gasnetc_port_info_t *
gasnetc_select_port(gasnet_node_t node, int qpi) {
    if (gasnetc_non_ib(node)) {
      return NULL;
    }
    if (GASNETC_QPI_IS_REQ(qpi)) {
      /* Second half of table (if any) duplicates first half. */
      qpi -= gasnetc_num_qps;
    }
    
    /* XXX: At some point we lost any attempt at true "round-robin"
     * that could have balanced port use when (num_qps % num_ports) != 0.
     * The problem there was that getting a distribution that both ends
     * agreed to was a big mess.  Perhaps we bring that idea back later.
     * However, for now the same sequence of gasnetc_num_qps values is
     * repeated for each node.
     * XXX: If this changes, gasnetc_sndrcv_limits() must change to match.
     */
    return  &gasnetc_port_tbl[qpi % gasnetc_num_ports];
}

/* Setup ports array for one node */
static int
gasnetc_setup_ports(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
  static const gasnetc_port_info_t **ports = NULL;

  if_pf (!ports) {
    /* Distribute the qps over the ports, same for each node. */
    int qpi;
    ports = gasneti_malloc(gasnetc_alloc_qps * sizeof(gasnetc_port_info_t *));
    for (qpi = 0; qpi < gasnetc_num_qps; ++qpi) {
      ports[qpi] = gasnetc_select_port(node, qpi);
    #if GASNETC_IBV_SRQ
      if (gasnetc_use_srq) {
        /* Second half of table (if any) duplicates first half.
           This might NOT be the same as extending the loop */
        ports[qpi + gasnetc_num_qps] = ports[qpi];
      }
    #endif
    }
  }

  conn_info->port = ports;
  return GASNET_OK;
}

/* Create endpoint(s) for a given peer
 * Outputs the qpn values in the array provided.
 */
static int
gasnetc_qp_create(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_cep_t *cep;
    int qpi;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_init_attr_t qp_init_attr;
    VAPI_qp_prop_t      qp_prop;
    int rc;

    qp_init_attr.cap.max_oust_wr_rq = gasnetc_am_oust_pp * 2;
    qp_init_attr.cap.max_oust_wr_sq = gasnetc_op_oust_pp;
    qp_init_attr.cap.max_sg_size_rq = 1;
    qp_init_attr.cap.max_sg_size_sq = GASNETC_SND_SG;
    qp_init_attr.rdd_hndl           = 0;
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type            = VAPI_TS_RC;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];
      gasnetc_hca_t *hca = &gasnetc_hca[port->hca_index];

      cep->hca = hca;
      cep->hca_handle = hca->handle;
    #if GASNETC_IB_MAX_HCAS > 1
      cep->hca_index = hca->hca_index;
    #endif

      qp_init_attr.pd_hndl         = hca->pd;
      qp_init_attr.rq_cq_hndl      = hca->rcv_cq;
      qp_init_attr.sq_cq_hndl      = hca->snd_cq;
      rc = VAPI_create_qp(hca->handle, &qp_init_attr, &cep->qp_handle, &qp_prop);
      GASNETC_VAPI_CHECK(rc, "from VAPI_create_qp()");
      gasneti_assert(qp_prop.cap.max_oust_wr_rq >= gasnetc_am_oust_pp * 2);
      gasneti_assert(qp_prop.cap.max_oust_wr_sq >= gasnetc_op_oust_pp);
      /* XXX: When could/should we use the ENTIRE allocated length? */
      gasneti_semaphore_init(&cep->sq_sema, gasnetc_op_oust_pp, gasnetc_op_oust_pp);

      conn_info->local_qpn[qpi] = qp_prop.qp_num;
    }
#else
    struct ibv_qp_init_attr     qp_init_attr;
    const int                   max_recv_wr = gasnetc_use_srq ? 0 : gasnetc_am_oust_pp * 2;
    int                         max_send_wr = gasnetc_op_oust_pp;
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t       *xrc_snd_qp;

    if_pf (gasnetc_use_xrc && !gasnetc_xrc_snd_qp) {
      gasnetc_xrc_snd_qp = gasneti_calloc(gasneti_nodemap_global_count * gasnetc_alloc_qps,
                                          sizeof(gasnetc_xrc_snd_qp_t));
    }
    xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_init_attr.cap.max_send_wr     = max_send_wr;
    qp_init_attr.cap.max_recv_wr     = max_recv_wr;
    qp_init_attr.cap.max_send_sge    = GASNETC_SND_SG;
    qp_init_attr.cap.max_recv_sge    = 1;
    qp_init_attr.qp_context          = NULL; /* XXX: Can/should we use this? */
  #if GASNETC_IBV_XRC
    qp_init_attr.qp_type             = gasnetc_use_xrc ? IBV_QPT_XRC : IBV_QPT_RC;
  #else
    qp_init_attr.qp_type             = IBV_QPT_RC;
  #endif
    qp_init_attr.sq_sig_all          = 1; /* XXX: Unless we drop 1-to-1 WQE/CQE relationship */
    qp_init_attr.srq                 = NULL;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      gasnetc_qp_hndl_t hndl;
      const gasnetc_port_info_t *port = conn_info->port[qpi];
      gasnetc_hca_t *hca = &gasnetc_hca[port->hca_index];

      cep->hca = hca;
    #if GASNETC_IB_MAX_HCAS > 1
      cep->hca_index = hca->hca_index;
    #endif

      qp_init_attr.send_cq         = hca->snd_cq;
      qp_init_attr.recv_cq         = hca->rcv_cq;

    #if GASNETC_IBV_SRQ
      if (gasnetc_use_srq) {
        if (GASNETC_QPI_IS_REQ(qpi)) {
          qp_init_attr.srq = hca->rqst_srq;
          qp_init_attr.cap.max_send_wr = gasnetc_am_oust_pp;
          qp_init_attr.cap.max_send_sge = 1; /* only AMs on this QP */
        } else {
          qp_init_attr.srq = hca->repl_srq;
          qp_init_attr.cap.max_send_wr = gasnetc_op_oust_pp;
          qp_init_attr.cap.max_send_sge = GASNETC_SND_SG;
        }
        cep->srq = qp_init_attr.srq;
        max_send_wr = qp_init_attr.cap.max_send_wr;
      }
    #endif
    #if GASNETC_IBV_XRC
      cep->sq_sema_p = &cep->sq_sema;
      if (gasnetc_use_xrc) {
        gasnetc_xrc_create_qp(hca->xrc_domain, node, qpi);

        hndl = xrc_snd_qp[qpi].handle;
        if (hndl) {
            /* per-supernode QP was already created - just reference it */
            cep->sq_sema_p = xrc_snd_qp[qpi].sq_sema_p;
            goto finish;
        }
  
        qp_init_attr.xrc_domain = hca->xrc_domain;
        qp_init_attr.srq        = NULL;
      }
    #endif
  
      while (1) { /* No query for max_inline_data limit */
        qp_init_attr.cap.max_inline_data = gasnetc_inline_limit;
        hndl = ibv_create_qp(hca->pd, &qp_init_attr);
        if (hndl != NULL) break;
        if (qp_init_attr.cap.max_inline_data == -1) {
          /* Automatic max not working, fall back on manual search */
          gasnetc_inline_limit = 1024;
          continue;
        }
        if ((errno != EINVAL) || (gasnetc_inline_limit == 0)) {
          GASNETC_VAPI_CHECK_PTR(hndl, "from ibv_create_qp()");
          /* NOT REACHED */
        }
        gasnetc_inline_limit = MIN(1024, gasnetc_inline_limit - 1);
        /* Try again */
      }
      gasneti_assert(qp_init_attr.cap.max_recv_wr >= max_recv_wr);
      gasneti_assert(qp_init_attr.cap.max_send_wr >= max_send_wr);

      /* XXX: When could/should we use the ENTIRE allocated length? */
      gasneti_semaphore_init(GASNETC_CEP_SQ_SEMA(cep), max_send_wr, max_send_wr);
  
    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        xrc_snd_qp[qpi].handle = hndl;
        xrc_snd_qp[qpi].state = IBV_QPS_RESET;
        xrc_snd_qp[qpi].sq_sema_p = GASNETC_CEP_SQ_SEMA(cep);
      }

    finish:
      cep->rcv_qpn = gasnetc_use_xrc ? conn_info->local_xrc_qpn[qpi] : hndl->qp_num;
    #elif GASNETC_IBV_SRQ
      cep->rcv_qpn = hndl->qp_num;
    #endif

      cep->qp_handle = hndl;
      conn_info->local_qpn[qpi] = hndl->qp_num;
    }
#endif

    return GASNET_OK;
} /* create */

/* Advance QP state from RESET to INIT */
static int
gasnetc_qp_reset2init(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi;
    int rc;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_cap_t       qp_cap;

    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PKEY_IX);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PORT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);
    qp_attr.qp_state            = VAPI_INIT;
    qp_attr.pkey_ix             = 0;
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

      qp_attr.port = port->port_num;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(INIT)");
    }
#else
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t *xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

    #if GASNETC_IBV_SRQ
      qp_attr.qp_access_flags = GASNETC_QPI_IS_REQ(qpi)
                                    ? IBV_ACCESS_REMOTE_WRITE
                                    : IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    #endif
      qp_attr.port_num = port->port_num;

    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        rc = gasnetc_xrc_modify_qp(cep->hca->xrc_domain, conn_info->local_xrc_qpn[qpi], &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from gasnetc_xrc_modify_qp(INIT)");
      }
    #endif

      if (GASNETC_SND_QP_NEEDS_MODIFY(xrc_snd_qp[qpi], IBV_QPS_INIT)) {
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(INIT)");
      #if GASNETC_IBV_XRC
        if (gasnetc_use_xrc) xrc_snd_qp[qpi].state = IBV_QPS_INIT;
      #endif
      }
    }
#endif

    return GASNET_OK;
} /* reset2init */

/* Advance QP state from INIT to RTR */
static int
gasnetc_qp_init2rtr(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi;
    int rc;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_cap_t       qp_cap;

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
    qp_attr.av.grh_flag      = 0;
    qp_attr.av.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.av.src_path_bits = 0;
    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

      qp_attr.qp_ous_rd_atom = port->rd_atom;
      qp_attr.path_mtu       = MIN(GASNETC_QP_PATH_MTU, port->port.max_mtu);
      qp_attr.rq_psn         = GASNETC_PSN(node, qpi);
      qp_attr.av.dlid        = port->remote_lids[node];
      qp_attr.dest_qp_num    = conn_info->remote_qpn[qpi];
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTR)");
    }
#else
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t *xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_DEST_QPN | IBV_QP_MIN_RNR_TIMER);
    qp_attr.qp_state         = IBV_QPS_RTR;
    qp_attr.ah_attr.sl            = 0;
    qp_attr.ah_attr.is_global     = 0;
    qp_attr.ah_attr.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.ah_attr.src_path_bits = 0;

    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

      qp_attr.max_dest_rd_atomic = GASNETC_QPI_IS_REQ(qpi) ? 0 : port->rd_atom;
      qp_attr.path_mtu           = MIN(GASNETC_QP_PATH_MTU, port->port.max_mtu);
      qp_attr.rq_psn             = GASNETC_PSN(node, qpi);
      qp_attr.ah_attr.dlid       = port->remote_lids[node];
      qp_attr.ah_attr.port_num   = port->port_num;
      qp_attr.dest_qp_num        = conn_info->remote_qpn[qpi];

    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        rc = gasnetc_xrc_modify_qp(cep->hca->xrc_domain, cep->rcv_qpn, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from gasnetc_xrc_modify_qp(RTR)");

        /* The normal QP will connect, below, to the peer's XRC rcv QP */
        qp_attr.dest_qp_num = conn_info->remote_xrc_qpn[qpi];
      }
    #endif

      if (GASNETC_SND_QP_NEEDS_MODIFY(xrc_snd_qp[qpi], IBV_QPS_RTR)) {
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTR)");
      #if GASNETC_IBV_XRC
        if (gasnetc_use_xrc) xrc_snd_qp[qpi].state = IBV_QPS_RTR;
      #endif
      }
    }
#endif

    return GASNET_OK;
} /* init2rtr */

/* Advance QP state from RTR to RTS */
static int
gasnetc_qp_rtr2rts(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi;
    int rc;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_cap_t       qp_cap;

    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_SQ_PSN);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_TIMEOUT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RETRY_COUNT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RNR_RETRY);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_OUS_DST_RD_ATOM);
    qp_attr.qp_state         = VAPI_RTS;
    qp_attr.timeout          = gasnetc_qp_timeout;
    qp_attr.retry_count      = gasnetc_qp_retry_count;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

      qp_attr.sq_psn           = GASNETC_PSN(gasneti_mynode, qpi);
      qp_attr.ous_dst_rd_atom  = port->rd_atom;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTS)");
      gasnetc_inline_limit = MIN(gasnetc_inline_limit, qp_cap.max_inline_data_sq);
    }
#else
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t *xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
    qp_attr.qp_state         = IBV_QPS_RTS;
    qp_attr.timeout          = gasnetc_qp_timeout;
    qp_attr.retry_cnt        = gasnetc_qp_retry_count;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        cep->xrc_remote_srq_num = conn_info->xrc_remote_srq_num[qpi];
      }
    #endif

      if (GASNETC_SND_QP_NEEDS_MODIFY(xrc_snd_qp[qpi], IBV_QPS_RTS)) {
        const gasnetc_port_info_t *port = conn_info->port[qpi];

        qp_attr.sq_psn           = GASNETC_PSN(gasneti_mynode, qpi);
        qp_attr.max_rd_atomic    = GASNETC_QPI_IS_REQ(qpi) ? 0 : port->rd_atom;
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTS)");
      #if GASNETC_IBV_XRC
        if (gasnetc_use_xrc) xrc_snd_qp[qpi].state = IBV_QPS_RTS;
      #endif

        {
          struct ibv_qp_attr qp_attr2;
          struct ibv_qp_init_attr qp_init_attr;
          rc = ibv_query_qp(cep->qp_handle, &qp_attr2, IBV_QP_CAP, &qp_init_attr);
          GASNETC_VAPI_CHECK(rc, "from ibv_query_qp(RTS)");
          gasnetc_inline_limit = MIN(gasnetc_inline_limit, qp_attr2.cap.max_inline_data);
        }
      }
    }
#endif

    return GASNET_OK;
} /* rtr2rts */

#if GASNETC_DEBUG_CONNECT
#include <stdlib.h>
static void
permute_remote_nodes(gasnet_node_t *node_list)
{
  static int done_init = 0;
  gasnet_node_t idx,node,swap;

  if_pf (!done_init) srand(getpid());
  done_init = 1;

  for (node = idx = 0; node < gasneti_nodes; ++node) {
    if (gasnetc_non_ib(node)) continue;
    swap = (gasnet_node_t)(((double)(idx+1))*rand()/(RAND_MAX+1.0));
    gasneti_assert(swap <= idx);
    if (swap == idx) {
      node_list[idx] = node;
    } else {
      node_list[idx] = node_list[swap];
      node_list[swap] = node;
    }
    ++idx;
  }
  gasneti_assert(idx == gasnetc_remote_nodes);
}
#endif /* GASNETC_DEBUG_CONNECT */

/* Setup fully-connected communication */
extern int
gasnetc_connect_all(void)
{
  const int             ceps = gasneti_nodes * gasnetc_alloc_qps;
  gasnetc_qpn_t         *local_qpn = gasneti_calloc(ceps, sizeof(gasnetc_qpn_t));
  gasnetc_qpn_t         *remote_qpn = gasneti_calloc(ceps, sizeof(gasnetc_qpn_t));
  gasnetc_conn_info_t   *conn_info = gasneti_calloc(gasneti_nodes, sizeof(gasnetc_conn_info_t));
#if GASNETC_IBV_XRC
  gasnetc_qpn_t         *xrc_remote_rcv_qpn = NULL;
  uint32_t              *xrc_remote_srq_num = NULL;
#endif
  gasnet_node_t         node;
  int                   i, qpi;
  size_t                orig_inline_limit = gasnetc_inline_limit;
  gasnetc_cep_t         *cep; /* First cep of given node */

  if_pf (!gasnetc_remote_nodes) return GASNET_OK;

  /* Debug build loops in random order to help ensure connect code is order-independent */
#if GASNETC_DEBUG_CONNECT
  gasnet_node_t *node_list = gasneti_malloc(gasnetc_remote_nodes * sizeof(gasnet_node_t));
  gasnet_node_t node_idx;

  #define GASNETC_FOR_EACH_REMOTE_NODE(_node) \
    for (permute_remote_nodes(node_list), node_idx=0, (_node) = node_list[0]; \
         node_idx < gasnetc_remote_nodes; (_node) = node_list[++node_idx])
#else
  #define GASNETC_FOR_EACH_REMOTE_NODE(_node) \
    for ((_node) = 0; (_node) < gasneti_nodes; ++(_node)) \
      if (!gasnetc_non_ib(_node))
#endif

  /* Allocate the dense CEP table and populate the node2cep table. */
  {
    gasnetc_cep_t *cep_table = (gasnetc_cep_t *)
      gasnett_malloc_aligned(GASNETI_CACHE_LINE_BYTES,
                             gasnetc_remote_nodes * gasnetc_alloc_qps * sizeof(gasnetc_cep_t));
    for (node = 0, cep = cep_table; node < gasneti_nodes; ++node) { /* NOT randomized */
      if (gasnetc_non_ib(node)) continue;
      gasnetc_node2cep[node] = cep;
      memset(cep, 0, gasnetc_alloc_qps * sizeof(gasnetc_cep_t));
      cep +=  gasnetc_alloc_qps;
    }
    gasneti_assert((cep - cep_table) == (gasnetc_remote_nodes * gasnetc_alloc_qps));
  }

  /* Initialize connection tracking info */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    i = node * gasnetc_alloc_qps;
    conn_info[node].cep            = GASNETC_NODE2CEP(node);
    conn_info[node].local_qpn      = &local_qpn[i];
  #if GASNETC_IBV_XRC
    conn_info[node].local_xrc_qpn  = &gasnetc_xrc_rcv_qpn[i];
  #endif
    gasnetc_setup_ports(node, &conn_info[node]);
  }

  /* create all QPs */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_create(node, &conn_info[node]);
  }

  /* exchange address info */
#if GASNETC_IBV_XRC
  if (gasnetc_use_xrc) {
    /* Use single larger exchange rather then multiple smaller ones */
    struct exchange {
      uint32_t srq_num;
      gasnetc_qpn_t xrc_qpn;
      gasnetc_qpn_t qpn;
    };
    struct exchange *local_tmp  = gasneti_calloc(ceps,  sizeof(struct exchange));
    struct exchange *remote_tmp = gasneti_malloc(ceps * sizeof(struct exchange));
    for (i = node = 0; node < gasneti_nodes; ++node) {
      cep = GASNETC_NODE2CEP(node);
      for (qpi = 0; qpi < gasnetc_alloc_qps; ++qpi, ++i) {
        if (!gasnetc_non_ib(node)) {
          gasnetc_hca_t *hca = cep[qpi].hca;
          struct ibv_srq *srq = GASNETC_QPI_IS_REQ(qpi) ? hca->rqst_srq : hca->repl_srq;
          local_tmp[i].srq_num = srq->xrc_srq_num;
        }
        local_tmp[i].xrc_qpn = gasnetc_xrc_rcv_qpn[i];
        local_tmp[i].qpn     = local_qpn[i];
      }
    }
    gasneti_bootstrapAlltoall(local_tmp,
                              gasnetc_alloc_qps * sizeof(struct exchange),
                              remote_tmp);
    xrc_remote_rcv_qpn = gasneti_malloc(ceps * sizeof(gasnetc_qpn_t));
    xrc_remote_srq_num = gasneti_malloc(ceps * sizeof(uint32_t));
    for (i = 0; i < ceps; ++i) {
      xrc_remote_srq_num[i] = remote_tmp[i].srq_num;
      xrc_remote_rcv_qpn[i] = remote_tmp[i].xrc_qpn;
      remote_qpn[i]         = remote_tmp[i].qpn;
    }
    gasneti_free(remote_tmp);
    gasneti_free(local_tmp);
  } else
#endif
  gasneti_bootstrapAlltoall(local_qpn, gasnetc_alloc_qps*sizeof(gasnetc_qpn_t), remote_qpn);

  /* Advance state RESET -> INIT and perform local endpoint init.
     This could be overlapped with the AlltoAll if it were non-blocking*/
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_reset2init(node, &conn_info[node]);
    gasnetc_sndrcv_init_peer(node);
  }

  /* Would sync the AlltoAll here if it were non-blocking */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    i = node * gasnetc_alloc_qps;
    conn_info[node].remote_qpn     = &remote_qpn[i];
  #if GASNETC_IBV_XRC
    conn_info[node].remote_xrc_qpn = &xrc_remote_rcv_qpn[i];
    conn_info[node].xrc_remote_srq_num = &xrc_remote_srq_num[i];
  #endif
  }

  /* Advance state INIT -> RTR */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_init2rtr(node, &conn_info[node]);
  }

  /* QPs must reach RTS before we may continue
     (not strictly necessary in practice as long as we don't try to send until peers do.) */
  gasneti_bootstrapBarrier();

  /* Advance state RTR -> RTS */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_rtr2rts(node, &conn_info[node]);
  }

#if GASNETC_IBV_XRC
  gasneti_free(xrc_remote_srq_num);
  gasneti_free(xrc_remote_rcv_qpn);
#endif
  gasneti_free(conn_info);
  gasneti_free(remote_qpn);
  gasneti_free(local_qpn);
#if GASNETC_DEBUG_CONNECT
  gasneti_free(node_list);
#endif

  /* check inline limit */
  if ((orig_inline_limit != (size_t)-1) && (gasnetc_inline_limit < orig_inline_limit)) {
#if GASNET_CONDUIT_IBV
    if (gasnet_getenv("GASNET_INLINESEND_LIMIT") != NULL)
#endif
      fprintf(stderr,
              "WARNING: Requested GASNET_INLINESEND_LIMIT %d reduced to HCA limit %d\n",
              (int)orig_inline_limit, (int)gasnetc_inline_limit);
  }
  GASNETI_TRACE_PRINTF(I, ("Final/effective GASNET_INLINESEND_LIMIT = %d", (int)gasnetc_inline_limit));
  gasnetc_sndrcv_init_inline();

  #if GASNET_DEBUG
  /* Verify that we are actually connected. */
  { /* Each node sends an AM to node self-1 and then waits for local completion. */
    gasnetc_counter_t counter = GASNETC_COUNTER_INITIALIZER;
    gasnet_node_t peer;
    peer = (gasneti_mynode ? gasneti_mynode : gasneti_nodes) - 1;
  #if GASNET_PSHM
    /* Despite a comment to the contrary in earlier versions of this code, it is
     * "safe" to send over pshmnet despite the non-AM use via pshmnet_bootstrapBroadcast.
     * One probably needs to drain the recvd ping and ack to ensure this is true.
     * However, we still skip this for a minor efficiency gain.
     */
    if (!gasneti_pshm_in_supernode(peer))
  #endif
    {
      GASNETI_SAFE(gasnetc_RequestSystem(peer, &counter, gasneti_handleridx(gasnetc_SYS_init_ping), 0));
      gasnetc_counter_wait(&counter, gasnetc_use_rcv_thread); /* BLOCKING AM Send */
    }
  }
  #endif

  return GASNET_OK;
} /* gasnetc_connect_all */

/* Early setup for connection resources */
extern int
gasnetc_connect_init(void)
{

  /* Allocate node->cep lookup table */
  { size_t size = gasneti_nodes*sizeof(gasnetc_cep_t *);
    gasnetc_node2cep = (gasnetc_cep_t **)
      gasnett_malloc_aligned(GASNETI_CACHE_LINE_BYTES, size);
    memset(gasnetc_node2cep, 0, size);
  }

#if GASNETC_IBV_XRC
  if (gasnetc_use_xrc) {
    int ret = gasnetc_xrc_init();
    if (ret) return ret;
  }
#endif

  return GASNET_OK;
} /* gasnetc_connect_init */

static char dump_conn_line[80] = "";
static char dump_conn_elem[16] = "";
static gasnet_node_t dump_conn_first = GASNET_MAXNODES;
static gasnet_node_t dump_conn_prev;

static void
dump_conn_outln(FILE *file) {
#if GASNETC_DEBUG_CONNECT
  fprintf(file, "%x:%s\n", gasneti_mynode, dump_conn_line);
#endif
  GASNETI_TRACE_PRINTF(C, ("Network traffic sent to (hex)%s", dump_conn_line));
}

static void
dump_conn_out(FILE *file) {
  switch (dump_conn_prev - dump_conn_first) {
    case 0:
      snprintf(dump_conn_elem, sizeof(dump_conn_elem), " %x", dump_conn_first);
      break;
    case 1:
      snprintf(dump_conn_elem, sizeof(dump_conn_elem), " %x %x", dump_conn_first, dump_conn_prev);
      break;
    default:
      snprintf(dump_conn_elem, sizeof(dump_conn_elem), " %x-%x", dump_conn_first, dump_conn_prev);
  }

  if (strlen(dump_conn_line) + strlen(dump_conn_elem) < sizeof(dump_conn_line)) {
    strcat(dump_conn_line, dump_conn_elem);
  } else {
    dump_conn_outln(file);
    strcpy(dump_conn_line, dump_conn_elem);
  }
}

static void
dump_conn_next(FILE *file, gasnet_node_t n)
{
  if (dump_conn_first == GASNET_MAXNODES) {
    dump_conn_first = dump_conn_prev = n;
    return;
  } else if (n == dump_conn_prev+1) {
    dump_conn_prev = n;
    return;
  }

  dump_conn_out(file);
  dump_conn_first = dump_conn_prev = n;
}

static void
dump_conn_done(FILE *file)
{
  if (dump_conn_first == GASNET_MAXNODES) return;
  dump_conn_out(file);
  dump_conn_outln(file);
}

/* Fini currently just dumps connection table. */
extern int
gasnetc_connect_fini(FILE *file)
{
  gasnet_node_t n, count = 0;
  for (n = 0; n < gasneti_nodes; ++n) {
    gasnetc_cep_t *cep = GASNETC_NODE2CEP(n);
    int qpi;
    if (!cep) continue;
    for (qpi=0; qpi<gasnetc_alloc_qps; ++qpi, ++cep) {
      if (cep->used) {
        dump_conn_next(file, n);
        ++count;
        break;
      }
    }
  }
  dump_conn_done(file);
  GASNETI_TRACE_PRINTF(C, ("Network traffic sent to %d of %d remote nodes", (int)count, (int)gasnetc_remote_nodes));

  return GASNET_OK;
} /* gasnetc_connect_dump */
