/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_connect.c,v $
 *     $Date: 2011/02/16 19:44:40 $
 * $Revision: 1.19 $
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

/* Flag bit(s) used in conn_info->state */
#define GASNETC_CONN_STATE_QP_DUP   1

/* Common types */
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_t,       struct ibv_qp_attr)     gasnetc_qp_attr_t;
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_mask_t,  enum ibv_qp_attr_mask)  gasnetc_qp_mask_t;


#if GASNETC_IBV_XRC
gasnetc_qpn_t *gasnetc_xrc_rcv_qpn = NULL;

/* Create one XRC rcv QP */
static int
gasnetc_xrc_create_qp(gasnetc_cep_t *cep) {
  const int cep_idx = (int)(cep - gasnetc_cep);
  gasneti_atomic32_t *rcv_qpn_p = (gasneti_atomic32_t *)(&gasnetc_xrc_rcv_qpn[cep_idx]);
  gasnetc_qpn_t rcv_qpn = 0;
  struct ibv_xrc_domain *xrc_domain = cep->hca->xrc_domain;
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
extern int
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
  
        gasnetc_xrc_create_qp(cep);

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

  /* Initialize connection tracking info */
  for (node = 0; node < gasneti_nodes; ++node) {
    i = node * gasnetc_alloc_qps;
    if (!gasnetc_cep[i].hca) continue;

    conn_info[node].cep            = &gasnetc_cep[i];
    conn_info[node].local_qpn      = &local_qpn[i];
  #if GASNETC_IBV_XRC
    conn_info[node].local_xrc_qpn  = &gasnetc_xrc_rcv_qpn[i];
  #endif
  }

  /* create all QPs */
#if GASNET_DEBUG
  /* Loop in reverse order to help ensure connect code is order-independent */
  for (node = gasneti_nodes-1; node < gasneti_nodes /* unsigned! */; --node)
#else
  for (node = 0; node < gasneti_nodes; ++node)
#endif
  {
    i = node * gasnetc_alloc_qps;
    if (!gasnetc_cep[i].hca) continue;

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
    GASNETC_FOR_EACH_CEP(i, node, qpi) {
      gasnetc_hca_t *hca = gasnetc_cep[i].hca;
      if (hca) {
        struct ibv_srq *srq = GASNETC_QPI_IS_REQ(qpi) ? hca->rqst_srq : hca->repl_srq;
        local_tmp[i].srq_num = srq->xrc_srq_num;
      }
      local_tmp[i].xrc_qpn = gasnetc_xrc_rcv_qpn[i];
      local_tmp[i].qpn     = local_qpn[i];
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

  /* perform local endpoint init and advance state RESET -> INIT -> RTR -> RTS */
#if GASNET_DEBUG
  /* Loop order must match that used for the create step */
  for (node = gasneti_nodes-1; node < gasneti_nodes /* unsigned! */; --node)
#else
  for (node = 0; node < gasneti_nodes; ++node)
#endif
  {
    i = node * gasnetc_alloc_qps;
    if (!gasnetc_cep[i].hca) {
        gasnetc_sndrcv_init_peer(node);
        continue;
    }

    conn_info[node].remote_qpn     = &remote_qpn[i];
  #if GASNETC_IBV_XRC
    conn_info[node].remote_xrc_qpn = &xrc_remote_rcv_qpn[i];
    conn_info[node].xrc_remote_srq_num = &xrc_remote_srq_num[i];
  #endif

    (void)gasnetc_qp_reset2init(node, &conn_info[node]);
    gasnetc_sndrcv_init_peer(node);
    (void)gasnetc_qp_init2rtr(node, &conn_info[node]);
    (void)gasnetc_qp_rtr2rts(node, &conn_info[node]);
  }

#if GASNETC_IBV_XRC
  gasneti_free(xrc_remote_srq_num);
  gasneti_free(xrc_remote_rcv_qpn);
#endif
  gasneti_free(conn_info);
  gasneti_free(remote_qpn);
  gasneti_free(local_qpn);

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

  /* All QPs must reach RTS before we may continue */
  gasneti_bootstrapBarrier();

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
