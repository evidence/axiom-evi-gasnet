/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_connect.c,v $
 *     $Date: 2011/02/15 21:08:32 $
 * $Revision: 1.15 $
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

/* Convenience iterator */
#define GASNETC_FOR_EACH_QPI(_conn_info, _qpi, _cep)  \
  for((_cep) = (_conn_info)->cep, (_qpi) = 0; \
      (_qpi) < gasnetc_alloc_qps; ++(_cep), ++(_qpi))

/* Convenience macro */
#if GASNETC_IBV_SRQ 
  #define GASNETC_QPI_IS_REQ(_qpi) ((_qpi) >= gasnetc_num_qps)
#else
  #define GASNETC_QPI_IS_REQ(_qpi) (0)
#endif

/* Flag bit(s) used in conn_info->state */
#define GASNETC_CONN_STATE_QP_DUP   1

/* Common types */
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_t,       struct ibv_qp_attr)     gasnetc_qp_attr_t;
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_mask_t,  enum ibv_qp_attr_mask)  gasnetc_qp_mask_t;


#if GASNETC_IBV_XRC
gasnetc_qpn_t *gasnetc_xrc_rcv_qpn = NULL;

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

/* Create an XRC domain (one per supernode) */
/* XXX: Requires that at least the first call is collective */
extern int
gasnetc_alloc_xrc_domain(gasnetc_hca_t *hca, gasnetc_lid_t mylid) {
  char *filename1, *filename2 = NULL;
  size_t flen;
  int fd, rc;

  /* Use per-supernode filename to create common XRC domain */
  filename1 = gasnetc_xrc_tmpname(mylid, hca->hca_index);
  fd = open(filename1, O_CREAT, S_IWUSR|S_IRUSR);
  if (fd < 0) {
    gasneti_fatalerror("failed to create xrc domain file '%s': %d:%s", filename1, errno, strerror(errno));
  }
  hca->xrc_domain = ibv_open_xrc_domain(hca->handle, fd, O_CREAT);
  GASNETC_VAPI_CHECK_PTR(hca->xrc_domain, "from ibv_open_xrc_domain()");
  (void) close(fd);

  /* Use another per-supernode filename to create common shared memory file */
  if (!gasnetc_xrc_rcv_qpn) { /* Exactly once when using multiple HCAs */
    /* TODO: Should PSHM combine this w/ the AM segment? */
    filename2 = gasnetc_xrc_tmpname(mylid, 0xf);
    fd = open(filename2, O_CREAT|O_RDWR, S_IWUSR|S_IRUSR);
    if (fd < 0) {
      gasneti_fatalerror("failed to create xrc shared memory file '%s': %d:%s", filename2, errno, strerror(errno));
    }
    flen = GASNETI_PAGE_ALIGNUP(sizeof(gasnetc_qpn_t) * gasneti_nodes * gasnetc_alloc_qps);
    rc = ftruncate(fd, flen);
    if (rc < 0) {
      gasneti_fatalerror("failed to resize xrc shared memory file '%s': %d:%s", filename2, errno, strerror(errno));
    }
    /* XXX: Is there anything else that can/should be packed into the same shared memory file? */
    gasnetc_xrc_rcv_qpn = mmap(NULL, flen, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (gasnetc_xrc_rcv_qpn == MAP_FAILED) {
      gasneti_fatalerror("failed to mmap xrc shared memory file '%s': %d:%s", filename2, errno, strerror(errno));
    }
    (void) close(fd);
  }

  /* Clean up once everyone is done w/ both files */
  gasnetc_supernode_barrier();
  (void)unlink(filename1); gasneti_free(filename1);
  if (filename2) {
    (void)unlink(filename2); gasneti_free(filename2);
  }

  return GASNET_OK;
}

/* Create the XRC rcv QPs and advance to INIT state.
 * Work is done just once per supernode for each remote QP. */
extern int
gasnetc_create_xrc_rcv_qps(void) {
  const int ceps = gasneti_nodes * gasnetc_alloc_qps;
  int i;

  gasneti_assert(gasnetc_xrc_rcv_qpn != NULL);

  /* Create/INIT the RCV QPs once per supernode and register in the non-creating nodes */
  if (!gasneti_nodemap_local_rank) {
    for (i = 0; i < ceps; ++i) {
      gasnetc_hca_t *hca = gasnetc_cep[i].hca;
      if (hca) {
        struct ibv_qp_init_attr init_attr;
        int ret;

        memset(&init_attr, 0, sizeof(init_attr));
        init_attr.xrc_domain = hca->xrc_domain;
        ret = ibv_create_xrc_rcv_qp(&init_attr, &(gasnetc_xrc_rcv_qpn[i]));
        GASNETC_VAPI_CHECK(ret, "from ibv_create_xrc_rcv_qp()");
      }
    }
  }
  gasnetc_supernode_barrier();
  if (gasneti_nodemap_local_rank) {
    for (i = 0; i < ceps; ++i) {
      gasnetc_hca_t *hca = gasnetc_cep[i].hca;
      if (hca) {
        int ret = ibv_reg_xrc_rcv_qp(hca->xrc_domain, gasnetc_xrc_rcv_qpn[i]);
        GASNETC_VAPI_CHECK(ret, "from ibv_reg_xrc_rcv_qp()");
      }
    }
  }

  return GASNET_OK;
}
#endif /* GASNETC_IBV_XRC */

/* Distribute the qps to each peer round-robin over the ports.
   Returns NULL for cases that should not have any connection */
extern const gasnetc_port_info_t *
gasnetc_select_port(gasnet_node_t node, int qpi) {
  #if !GASNET_PSHM
    if (gasnetc_use_xrc && (gasneti_nodemap_local_count != 1)) {
      /* XRC w/o PSHM needs a rcv QP for local peers, if any.
         So, we skip over the gasnetc_non_ib() check. */
    } else
  #endif
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

/* Create endpoint(s) for a given peer
 * Outputs the qpn values in the array provided.
 */
extern int
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
      gasnetc_hca_t *hca = cep->hca;
      gasneti_assert(hca);

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
    #if GASNETC_IBV_XRC
      gasnetc_cep_t *first_cep = NULL;
    #endif
      gasnetc_qp_hndl_t hndl;
      gasnetc_hca_t *hca = cep->hca;
      gasneti_assert(hca);

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
        const gasnet_node_t first = gasneti_nodemap[node];
        first_cep = &gasnetc_cep[(first * gasnetc_alloc_qps) + qpi];
  
        cep->sq_sema_p = &first_cep->sq_sema;
        hndl = first_cep->qp_handle;

        if (hndl) {
            /* per-supernode QP was already created - just reference it */
            conn_info->state |= GASNETC_CONN_STATE_QP_DUP;
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
      if (first_cep) {
        first_cep->qp_handle = hndl; /* harmless duplication if cep == first_cep */
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
extern int
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
      const gasnetc_port_info_t *port = gasnetc_select_port(node, qpi);
      qp_attr.port = port->port_num;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(INIT)");
    }
#else
    const int have_qp = (!gasnetc_use_xrc || !(GASNETC_CONN_STATE_QP_DUP & conn_info->state));

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = gasnetc_select_port(node, qpi);

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

      if (have_qp) {
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(INIT)");
      }
    }
#endif

    return GASNET_OK;
} /* reset2init */

/* Advance QP state from INIT to RTR */
extern int
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
      const gasnetc_port_info_t *port = gasnetc_select_port(node, qpi);
      qp_attr.qp_ous_rd_atom = port->rd_atom;
      qp_attr.path_mtu       = MIN(GASNETC_QP_PATH_MTU, port->port.max_mtu);
      qp_attr.rq_psn         = GASNETC_PSN(node, qpi);
      qp_attr.av.dlid        = port->remote_lids[node];
      qp_attr.dest_qp_num    = conn_info->remote_qpn[qpi];
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTR)");
    }
#else
    const int have_qp = (!gasnetc_use_xrc || !(GASNETC_CONN_STATE_QP_DUP & conn_info->state));

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_DEST_QPN | IBV_QP_MIN_RNR_TIMER);
    qp_attr.qp_state         = IBV_QPS_RTR;
    qp_attr.ah_attr.sl            = 0;
    qp_attr.ah_attr.is_global     = 0;
    qp_attr.ah_attr.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.ah_attr.src_path_bits = 0;

    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = gasnetc_select_port(node, qpi);

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

      if (have_qp) {
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTR)");
      }
    }
#endif

    return GASNET_OK;
} /* init2rtr */

/* Advance QP state from RTR to RTS */
extern int
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
      const gasnetc_port_info_t *port = gasnetc_select_port(node, qpi);
      qp_attr.sq_psn           = GASNETC_PSN(gasneti_mynode, qpi);
      qp_attr.ous_dst_rd_atom  = port->rd_atom;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTS)");
      gasnetc_inline_limit = MIN(gasnetc_inline_limit, qp_cap.max_inline_data_sq);
    }
#else
    const int have_qp = (!gasnetc_use_xrc || !(GASNETC_CONN_STATE_QP_DUP & conn_info->state));

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
    qp_attr.qp_state         = IBV_QPS_RTS;
    qp_attr.timeout          = gasnetc_qp_timeout;
    qp_attr.retry_cnt        = gasnetc_qp_retry_count;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        cep->xrc_remote_srq_num = conn_info->xrc_remote_srq_num[qpi];

      #if !GASNET_PSHM && GASNET_DEBUG
        /* Some cep->hca values were non-NULL just to setup XRC.
           We NULL them here to assist debug assert()ions. */
        if (gasnetc_non_ib(node)) {
          cep->hca = NULL;
        }
      #endif
      }
    #endif

      if (have_qp) {
        const gasnetc_port_info_t *port = gasnetc_select_port(node, qpi);

        qp_attr.sq_psn           = GASNETC_PSN(gasneti_mynode, qpi);
        qp_attr.max_rd_atomic    = GASNETC_QPI_IS_REQ(qpi) ? 0 : port->rd_atom;
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTS)");
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
