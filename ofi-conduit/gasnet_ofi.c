/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ofi-conduit/gasnet_ofi.c $
 * Description: GASNet libfabric (OFI) conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright 2015, Intel Corporation
 * Terms of use are as specified in license.txt
 */
#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>
#include <gasnet_ofi.h>

#include <pmi-spawner/gasnet_bootstrap_internal.h>
#include <ssh-spawner/gasnet_bootstrap_internal.h>
#include <mpi-spawner/gasnet_bootstrap_internal.h>

#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_errno.h>

#define USE_AV_MAP 0
addr_table_t  *addr_table;
#if USE_AV_MAP
#define GET_DEST(dest) (fi_addr_t)(addr_table->table[dest].dest)
#else
#define GET_DEST(dest) (fi_addr_t)dest
#endif

#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
#define GET_DSTADDR(dest_addr, dest) (uintptr_t)((char*)dest_addr - (char*)gasneti_seginfo[dest].addr)
#define GET_SRCADDR(src_addr, dest) (uintptr_t)((char*)src_addr - (char*)gasneti_seginfo[dest].addr)
#else
#define GET_DSTADDR(dest_addr, dest) (uintptr_t)dest_addr
#define GET_SRCADDR(src_addr, dest) (uintptr_t)src_addr
#endif

static gasneti_lifo_head_t ofi_am_pool = GASNETI_LIFO_INITIALIZER;

#define OFI_AM_MAX_DATA_LENGTH                                        \
  GASNETI_ALIGNUP(GASNETI_ALIGNUP(sizeof(gasnet_handlerarg_t) *		  \
			  gasnet_AMMaxArgs(), GASNETI_MEDBUF_ALIGNMENT) +		  \
		  	  gasnet_AMMaxMedium(), GASNETI_MEDBUF_ALIGNMENT)
#define OFI_AM_NUM_BLOCK 	8
#define OFI_AM_BLOCK_SIZE 	1*1024*1024

static uint64_t             	max_buffered_send;
static uint64_t             	min_multi_recv;

extern void (*gasneti_bootstrapBarrier_p)(void);
extern void (*gasneti_bootstrapExchange_p)(void *src, size_t len, void *dest);
extern void (*gasneti_bootstrapFini_p)(void);
extern void (*gasneti_bootstrapAbort_p)(int exitcode);
extern void (*gasneti_bootstrapAlltoall_p)(void *src, size_t len, void *dest);
extern void (*gasneti_bootstrapBroadcast_p)(void *src, size_t len, void *dest, int rootnode);
extern void (*gasneti_bootstrapCleanup_p)(void);

struct iovec *am_iov;
struct fi_msg *am_buff_msg;
ofi_ctxt_t *am_buff_ctxt = NULL;

#ifdef GASNET_PAR
static gasneti_weakatomic_t pending_rdma;
#else
static int pending_rdma = 0;
#endif
static int rdma_empty_poll = 0;

#define OFI_CONDUIT_VERSION FI_VERSION(1, 0)
#define NUM_CQ_ENTRIES 2

/*------------------------------------------------
 * Initialize OFI conduit
 * ----------------------------------------------*/

int gasnetc_ofi_init(int *argc, char ***argv, 
                     gasnet_node_t *nodes_p, gasnet_node_t *mynode_p)
{
  int ret = GASNET_OK;
  int result = GASNET_ERR_NOT_INIT;
  struct fi_info		*rdma_hints, *am_hints, *rdma_info, *am_info;
  struct fi_cq_attr   	cq_attr 	= {0};
  struct fi_av_attr   	av_attr 	= {0};
  char   sockname[128], *alladdrs;
  size_t socknamelen = sizeof(sockname);
  conn_entry_t *mapped_table;

  char *spawner = gasneti_getenv_withdefault("GASNET_OFI_SPAWNER", "(not set)");

  /* Bootstrapinit */
#if HAVE_SSH_SPAWNER
  if (GASNET_OK == (result = gasneti_bootstrapInit_ssh(argc, argv, nodes_p, mynode_p))) {
    gasneti_bootstrapFini_p     = &gasneti_bootstrapFini_ssh;
    gasneti_bootstrapAbort_p    = &gasneti_bootstrapAbort_ssh;
    gasneti_bootstrapBarrier_p  = &gasneti_bootstrapBarrier_ssh;
    gasneti_bootstrapExchange_p = &gasneti_bootstrapExchange_ssh;
    gasneti_bootstrapAlltoall_p = &gasneti_bootstrapAlltoall_ssh;
    gasneti_bootstrapBroadcast_p= &gasneti_bootstrapBroadcast_ssh;
    gasneti_bootstrapCleanup_p  = &gasneti_bootstrapCleanup_ssh;
  } else
#endif
#if HAVE_MPI_SPAWNER
  if (!strcmp(spawner, "mpi")) {
    result = gasneti_bootstrapInit_mpi(argc, argv, nodes_p, mynode_p);
    gasneti_bootstrapFini_p     = &gasneti_bootstrapFini_mpi;
    gasneti_bootstrapAbort_p    = &gasneti_bootstrapAbort_mpi;
    gasneti_bootstrapBarrier_p  = &gasneti_bootstrapBarrier_mpi;
    gasneti_bootstrapExchange_p = &gasneti_bootstrapExchange_mpi;
    gasneti_bootstrapAlltoall_p = &gasneti_bootstrapAlltoall_mpi;
    gasneti_bootstrapBroadcast_p= &gasneti_bootstrapBroadcast_mpi;
    gasneti_bootstrapCleanup_p  = &gasneti_bootstrapCleanup_mpi;
  } else
#endif
#if HAVE_PMI_SPAWNER
  if (GASNET_OK == (result = gasneti_bootstrapInit_pmi(argc, argv, nodes_p, mynode_p))) {
    gasneti_bootstrapFini_p     = &gasneti_bootstrapFini_pmi;
    gasneti_bootstrapAbort_p    = &gasneti_bootstrapAbort_pmi;
    gasneti_bootstrapBarrier_p  = &gasneti_bootstrapBarrier_pmi;
    gasneti_bootstrapExchange_p = &gasneti_bootstrapExchange_pmi;
    gasneti_bootstrapAlltoall_p = &gasneti_bootstrapAlltoall_pmi;
    gasneti_bootstrapBroadcast_p= &gasneti_bootstrapBroadcast_pmi;
    gasneti_bootstrapCleanup_p  = &gasneti_bootstrapCleanup_pmi;
  } else
#endif
  {
    gasneti_fatalerror("Requested spawner \"%s\" is unknown or not supported in this build", spawner);
  }

  /* OFI initialization */

  /* Alloc hints*/
  rdma_hints = fi_allocinfo();
  if (!rdma_hints) gasneti_fatalerror("fi_allocinfo for rdma_hints failed\n");
  am_hints = fi_allocinfo();
  if (!am_hints) gasneti_fatalerror("fi_allocinfo for am_hints failed\n");

  /* caps: fabric interface capabilities */
  rdma_hints->caps			= FI_RMA;		/* RMA read/write operations */
  /* mode: convey requirements for application to use fabric interfaces */
  rdma_hints->mode			= FI_CONTEXT;	/* fi_context is used for per
											   operation context parameter */
  /* addr_format: expected address format for AV/CM calls */
  rdma_hints->addr_format		= FI_FORMAT_UNSPEC;
  rdma_hints->tx_attr->op_flags	= FI_DELIVERY_COMPLETE|FI_COMPLETION;
  rdma_hints->ep_attr->type		= FI_EP_RDM; /* Reliable datagram */
  rdma_hints->domain_attr->threading
	  							= FI_THREAD_SAFE;
  rdma_hints->domain_attr->control_progress 
	  							= FI_PROGRESS_AUTO;
  /* resource_mgmt: FI_RM_ENABLED - provider protects against overrunning 
	 local and remote resources. */
  rdma_hints->domain_attr->resource_mgmt	
	  							= FI_RM_ENABLED;
  /* av_type: type of address vectores that are usable with this domain */
  rdma_hints->domain_attr->av_type
	  							= FI_AV_TABLE; /* type AV index */

  ret = fi_getinfo(OFI_CONDUIT_VERSION, NULL, NULL, 0ULL, rdma_hints, &rdma_info);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_getinfo failed: %d\n", ret);
  if (rdma_info == NULL) gasneti_fatalerror("fi_getinfo didn't find any providers for rdma\n");

  /* Hints for Active Message requirements */
  am_hints->caps				= FI_MSG; 		/* send/recv messages */
  am_hints->caps				|= FI_MULTI_RECV; /* support posting multi-recv buffer */
  am_hints->mode				= FI_CONTEXT;                                	/* Reliable datagram */
  am_hints->addr_format			= FI_FORMAT_UNSPEC;
  am_hints->tx_attr->op_flags	= FI_DELIVERY_COMPLETE|FI_COMPLETION;
  am_hints->rx_attr->op_flags	= FI_MULTI_RECV|FI_COMPLETION;	
  am_hints->ep_attr->type		= FI_EP_RDM; /* Reliable datagram */
  am_hints->domain_attr->threading			= FI_THREAD_SAFE;
  am_hints->domain_attr->control_progress	= FI_PROGRESS_AUTO;
  /* Enable resource management: provider protects against overrunning local and
   * remote resources. */
  am_hints->domain_attr->resource_mgmt		= FI_RM_ENABLED;
  am_hints->domain_attr->av_type			= FI_AV_TABLE; /* type AV index */

  ret = fi_getinfo(OFI_CONDUIT_VERSION, NULL, NULL, 0ULL, am_hints, &am_info);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_getinfo failed: %d\n", ret);
  if (am_info == NULL) gasneti_fatalerror("fi_getinfo didn't find any providers for active message\n");

  /* Open the fabric provider */
  ret = fi_fabric(rdma_info->fabric_attr, &ofi_fabricfd, NULL);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_fabric failed: %d\n", ret);

  /* Open a fabric access domain, also referred to as a resource domain */
  ret = fi_domain(ofi_fabricfd, rdma_info, &ofi_domainfd, NULL);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_domain failed: %d\n", ret);

  /* Allocate a new active endpoint for RDMA operations */
  ret = fi_endpoint(ofi_domainfd, rdma_info, &ofi_rdma_epfd, NULL);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_endpoint for rdma failed: %d\n", ret);

  /* Allocate a new active endpoint for AM operations */
  ret = fi_endpoint(ofi_domainfd, am_info, &ofi_am_epfd, NULL);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_endpoint for am failed: %d\n", ret);

  /* Allocate a new completion queue for RDMA operations */
  memset(&cq_attr, 0, sizeof(cq_attr));
  cq_attr.format    = FI_CQ_FORMAT_DATA; /* Provides data associated with a completion */
  ret = fi_cq_open(ofi_domainfd, &cq_attr, &ofi_rdma_cqfd, NULL);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_cq_open for rdma_eqfd failed: %d\n", ret);

  /* Allocate a new completion queue for AM operations */
  memset(&cq_attr, 0, sizeof(cq_attr));
  cq_attr.format    = FI_CQ_FORMAT_DATA;
  ret = fi_cq_open(ofi_domainfd, &cq_attr, &ofi_am_cqfd, NULL);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_cq_open for am_eqfd failed: %d\n", ret);

  /* Bind CQs to endpoints */
  ret = fi_ep_bind(ofi_rdma_epfd, &ofi_rdma_cqfd->fid, FI_SEND);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_ep_bind for rdma_cqfd to rdma_epfd failed: %d\n", ret);

  ret = fi_ep_bind(ofi_am_epfd, &ofi_am_cqfd->fid, FI_SEND | FI_RECV);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_ep_bind for am_cqfd to am_epfd failed: %d\n", ret);

  /* Low-water mark for shared receive buffer */
  min_multi_recv = OFI_AM_MAX_DATA_LENGTH + offsetof(ofi_am_send_buf_t,data);
  size_t optlen = min_multi_recv;
  ret	 = fi_setopt(&ofi_am_epfd->fid, FI_OPT_ENDPOINT, FI_OPT_MIN_MULTI_RECV,
		  &optlen,
		  sizeof(optlen));
  if (GASNET_OK != ret) gasneti_fatalerror("fi_setopt for am_epfd failed: %d\n", ret);

  /* Open Address Vector and bind the AV to the domain */
#if USE_AV_MAP
  av_attr.type        = FI_AV_MAP;
  addr_table          = (addr_table_t*)gasneti_malloc(gasneti_nodes * sizeof(conn_entry_t) + sizeof(addr_table_t));
  addr_table->size    = gasneti_nodes;
  mapped_table        = (conn_entry_t*)addr_table->table;
#else
  av_attr.type        = FI_AV_TABLE;
  mapped_table        = NULL;
#endif
  ret = fi_av_open(ofi_domainfd, &av_attr, &ofi_avfd, NULL);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_av_open failed: %d\n", ret);

  /* Bind AV to endpoints, both RDMA/AM endpoints share the same AV object */
  ret = fi_ep_bind(ofi_rdma_epfd, &ofi_avfd->fid, 0);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_ep_bind for avfd to rdma_epfd failed: %d\n", ret);

  ret = fi_ep_bind(ofi_am_epfd, &ofi_avfd->fid, 0);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_ep_bind for avfd to am_epfd failed: %d\n", ret);

  /* Enable endpoints */
  ret = fi_enable(ofi_rdma_epfd);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_enable for rdma failed: %d\n", ret);
  ret = fi_enable(ofi_am_epfd);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_enable for am failed: %d\n", ret);

  gasneti_nodemapInit(gasneti_bootstrapExchange_p, NULL, 0, 0);

  /* Get the address of AM endpoint and publish to other nodes through bootstrap
   * exchange function */
  ret = fi_getname(&ofi_am_epfd->fid, sockname, &socknamelen);
  if (GASNET_OK != ret) gasneti_fatalerror("fi_getepname failed: %d\n", ret);
  alladdrs = gasneti_malloc(gasneti_nodes*socknamelen);
  (*gasneti_bootstrapExchange_p)(&sockname, socknamelen, alladdrs);
  if (gasneti_nodes != fi_av_insert(ofi_avfd, alladdrs, gasneti_nodes,
			  (fi_addr_t*)mapped_table, 0ULL, NULL)) {
  	gasneti_fatalerror("fi_av_insert failed: %d\n", ret);
  }
  gasneti_free(alladdrs);

#ifdef GASNET_PAR
  gasneti_weakatomic_set(&pending_rdma, 0, 0);
#endif

  fi_freeinfo(rdma_hints);
  fi_freeinfo(am_hints);

  return ret;
}

/*------------------------------------------------
 * OFI conduit exit function
 * ----------------------------------------------*/
void gasnetc_ofi_exit(void)
{
  int i;
  int ret = GASNET_OK;
  /* Drain up CQ before cancel the multi-recv requests */
  struct fi_cq_data_entry re = {0};
  while(fi_cq_read(ofi_am_cqfd, (void *)&re, 1) != -FI_EAGAIN);

  if(am_buff_ctxt) {
    for(i = 0; i < OFI_AM_NUM_BLOCK; i++) {
      /* cancel the multi-recv */
      ret = fi_cancel(&ofi_am_epfd->fid, &am_buff_ctxt[i].ctxt);
      if (GASNET_OK != ret) gasneti_fatalerror("failed fi_cancel the %d am_buff_msg\n", i);
      gasneti_free(am_iov[i].iov_base);
    }
    gasneti_free(am_buff_ctxt);
    gasneti_free(am_iov);
    gasneti_free(am_buff_msg);
  }

  if(fi_close(&ofi_rdma_mrfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close mrfd failed\n");
  }

  if(fi_close(&ofi_am_epfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close am epfd failed\n");
  }

  if(fi_close(&ofi_rdma_epfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close rdma epfd failed\n");
  }

  if(fi_close(&ofi_am_cqfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close am eqfd failed\n");
  }

  if(fi_close(&ofi_rdma_cqfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close write eqfd failed\n");
  }

  if(fi_close(&ofi_avfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close av failed\n");
  }

  if(fi_close(&ofi_domainfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close domainfd failed\n");
  }

  if(fi_close(&ofi_fabricfd->fid)!=GASNET_OK) {
    gasneti_fatalerror("close fabricfd failed\n");
  }

#if USE_AV_MAP
  gasneti_free(addr_table);
#endif
}

/*------------------------------------------------
 * OFI conduit callback functions
 * ----------------------------------------------*/

/* Handle Active Messages from self */
static inline void gasnetc_ofi_handle_local_am(ofi_am_buf_t *buf)
{
	ofi_am_send_buf_t *header = &buf->sendbuf;
	uint8_t *addr;
	int nbytes;
	int isreq = header->isreq;
	int handler = header->handler;
	gasneti_handler_fn_t handler_fn = gasnetc_handler[handler];
	gasnetc_ofi_token_t token; 
	gasnetc_ofi_token_t *token_p = &token; 
	token.sourceid = header->sourceid;
	gasnet_handlerarg_t *args = (gasnet_handlerarg_t *)header->data;
	int numargs = header->argnum;

	switch(header->type) {
		case OFI_AM_SHORT:
			GASNETI_RUN_HANDLER_SHORT(isreq, handler, handler_fn, token_p, args, numargs);
			break;
		case OFI_AM_MEDIUM:
			nbytes = header->nbytes;
			addr = header->data + header->len - nbytes;
			GASNETI_RUN_HANDLER_MEDIUM(isreq, handler, handler_fn, token_p, args, numargs, addr, nbytes);
			break;
		case OFI_AM_LONG:
			addr = header->dest_ptr;
			nbytes = header->nbytes;
			GASNETI_RUN_HANDLER_LONG(isreq, handler, handler_fn, token_p, args, numargs, addr, nbytes);
			break;
		case OFI_AM_LONG_MEDIUM:
			addr = header->dest_ptr;
			nbytes = header->nbytes;
			memcpy(addr, header->data+header->len-nbytes, nbytes);
			GASNETI_RUN_HANDLER_LONG(isreq, handler, handler_fn, token_p, args, numargs, addr, nbytes);
			break;
		default:
			gasneti_fatalerror("undefined header type in gasnetc_ofi_handle_local_am: %d\n", 
					header->type);
	}

	gasneti_lifo_push(&ofi_am_pool, buf);
}

/* Handle incoming Active Messages */
static inline void gasnetc_ofi_handle_am(struct fi_cq_data_entry *re, void *buf)
{
	ofi_am_send_buf_t *header = (ofi_am_send_buf_t*)re->buf;
	uint8_t *addr;
	int nbytes;
	int isreq = header->isreq;
	int handler = header->handler;
	gasneti_handler_fn_t handler_fn = gasnetc_handler[handler];
	gasnetc_ofi_token_t token; 
	gasnetc_ofi_token_t *token_p = &token; 
	token.sourceid = header->sourceid;
	gasnet_handlerarg_t *args = (gasnet_handlerarg_t *)header->data;
	int numargs = header->argnum;

	switch(header->type) {
		case OFI_AM_SHORT:
			GASNETI_RUN_HANDLER_SHORT(isreq, handler, handler_fn, token_p, args, numargs);
			break;
		case OFI_AM_MEDIUM:
			nbytes = header->nbytes;
			addr = header->data + header->len - nbytes;
			GASNETI_RUN_HANDLER_MEDIUM(isreq, handler, handler_fn, token_p, args, numargs, addr, nbytes);
			break;
		case OFI_AM_LONG:
			addr = header->dest_ptr;
			nbytes = header->nbytes;
			GASNETI_RUN_HANDLER_LONG(isreq, handler, handler_fn, token_p, args, numargs, addr, nbytes);
			break;
		case OFI_AM_LONG_MEDIUM:
			addr = header->dest_ptr;
			nbytes = header->nbytes;
			memcpy(addr, header->data+header->len-nbytes, nbytes);
			GASNETI_RUN_HANDLER_LONG(isreq, handler, handler_fn, token_p, args, numargs, addr, nbytes);
			break;
		default:
			gasneti_fatalerror("undefined header type in gasnetc_ofi_handle_am: %d\n", 
					header->type);
	}
}

/* Handle RDMA completion as the initiator */
static inline void gasnetc_ofi_handle_rdma(void *buf)
{
	ofi_op_ctxt_t *ptr = (ofi_op_ctxt_t*)buf;

	switch (ptr->type) {
		case OFI_TYPE_EGET:
		case OFI_TYPE_EPUT:
			{
				gasnete_eop_t *eop = (gasnete_eop_t *)container_of(ptr, gasnete_eop_t, ofi);
				GASNETE_EOP_MARKDONE(eop);
			}
			break;
		case OFI_TYPE_IGET:
			{
				gasnete_iop_t *iop = (gasnete_iop_t *)container_of(ptr, gasnete_iop_t, get_ofi);
				gasneti_weakatomic_increment(&(iop->completed_get_cnt), 0);
			}
			break;
		case OFI_TYPE_IPUT:
			{
				gasnete_iop_t *iop = (gasnete_iop_t *)container_of(ptr, gasnete_iop_t, put_ofi);
				gasneti_weakatomic_increment(&(iop->completed_put_cnt), 0);
			}
			break;
		case OFI_TYPE_AM_DATA:
			{
				ptr->data_sent = 1;
			}
			break;
		default:
			gasneti_fatalerror("receive undefined OP type in gasnetc_ofi_rdma_poll: %d\n", ptr->type);

	}
}

/* Release ACKed send buffer */
static inline void gasnetc_ofi_release_am(struct fi_cq_data_entry *re, void *buf)
{
	ofi_am_buf_t *header = (ofi_am_buf_t*)buf;
	gasneti_lifo_push(&ofi_am_pool, header);
}

/*------------------------------------------------
 * Pre-post or pin-down memory
 * ----------------------------------------------*/
void gasnetc_ofi_attach(void *segbase, uintptr_t segsize)
{
	int ret = GASNET_OK;
	int i;
	int iov_len = OFI_AM_BLOCK_SIZE;
	int num_iov = OFI_AM_NUM_BLOCK;
	am_iov = (struct iovec *) gasneti_malloc(sizeof(struct iovec)*num_iov);
	am_buff_msg = (struct fi_msg *) gasneti_malloc(sizeof(struct fi_msg)*num_iov);
	am_buff_ctxt = (ofi_ctxt_t *) gasneti_malloc(sizeof(ofi_ctxt_t)*num_iov);

	/* Pin-down Memory Region */
#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
	ret = fi_mr_reg(ofi_domainfd, segbase, segsize, FI_REMOTE_READ | FI_REMOTE_WRITE, 0ULL, 0ULL, FI_MR_SCALABLE, &ofi_rdma_mrfd, NULL);
#else
	ret = fi_mr_reg(ofi_domainfd, (void *)0, UINT64_MAX, FI_REMOTE_READ | FI_REMOTE_WRITE, 0ULL, 0ULL, FI_MR_SCALABLE, &ofi_rdma_mrfd, NULL);
#endif
	if (GASNET_OK != ret) gasneti_fatalerror("fi_mr_reg for rdma failed: %d\n", ret);

	/* Bind MR to RMA endpoint */
	ret = fi_ep_bind(ofi_rdma_epfd, &ofi_rdma_mrfd->fid, FI_REMOTE_READ | FI_REMOTE_WRITE);
	if (GASNET_OK != ret) gasneti_fatalerror("fi_ep_bind for rdma_epfd and rdma_mrfd failed: %d\n", ret);

	for(i = 0; i < num_iov; i++) {
		am_iov[i].iov_base		= gasneti_malloc(iov_len);
		am_iov[i].iov_len		= iov_len;
		am_buff_msg[i].msg_iov		= &am_iov[i];
		am_buff_msg[i].iov_count 	= 1;
		am_buff_msg[i].addr 		= FI_ADDR_UNSPEC;
		am_buff_msg[i].desc	  	= NULL;
		am_buff_msg[i].context 		= &am_buff_ctxt[i].ctxt;
		am_buff_msg[i].data 		= 0;
		am_buff_ctxt[i].callback	= gasnetc_ofi_handle_am;
		am_buff_ctxt[i].index		= i;
		/* Post buffers for Active Messages */
		ret = fi_recvmsg(ofi_am_epfd, &am_buff_msg[i], FI_MULTI_RECV);
		if (GASNET_OK != ret) gasneti_fatalerror("fi_recvmsg failed: %d\n", ret);
	}

}


/*------------------------------------------------
 * OFI conduit network poll function
 * ----------------------------------------------*/

/* RDMA progress function */
static void gasnetc_ofi_rdma_poll(int blocking_poll)
{
	int ret = 0;
	struct fi_cq_data_entry re = {0};
	struct fi_cq_err_entry e = {0};
	rdma_empty_poll = 1;

	/* Read from Completion Queue */
	ret = fi_cq_read(ofi_rdma_cqfd, (void *)&re, 1);
	if (ret != -FI_EAGAIN)
	{
		if (ret < 0) {
			int err_sz = 0;
			err_sz = fi_cq_readerr(ofi_rdma_cqfd, &e ,0);
			gasneti_fatalerror("fi_cq_read for rdma_ep failed with error: %s, err_sz %d\n", 
					fi_strerror(e.err), err_sz);
		} else {
			/* got a RDMA ACK message, update handler */
			ofi_op_ctxt_t *ptr = (ofi_op_ctxt_t *)re.op_context;
			ptr->callback(re.op_context);
			rdma_empty_poll = 0;
		}
	}
}

/* AM progress function */
static void gasnetc_ofi_am_poll(int blocking_poll)
{
	int ret = 0;
	struct fi_cq_data_entry re = {0};
	struct fi_cq_err_entry e = {0};

	/* Read from Completion Queue */
	ret = fi_cq_read(ofi_am_cqfd, (void *)&re, 1);
	if (ret != -FI_EAGAIN)
	{
		if (ret < 0) {
			fi_cq_readerr(ofi_am_cqfd, &e ,0);
			gasneti_fatalerror("fi_cq_read for am_ep failed with error: %s\n", fi_strerror(e.err));
		} else {
			if(re.flags & FI_MULTI_RECV)
			{
				/* One pre-post buffer is used up, re-link it */
				ofi_ctxt_t *header;
				header = (ofi_ctxt_t *)re.op_context;
				if (re.len < min_multi_recv && re.len != 0)
					header->callback(&re, header);
				ret = fi_recvmsg(ofi_am_epfd, &(am_buff_msg[header->index]), FI_MULTI_RECV);
				if (GASNET_OK != ret) gasneti_fatalerror("fi_recvmsg failed inside am_poll: %d\n", ret);
			} else {
				ofi_am_buf_t *header;
				header = (ofi_am_buf_t *)re.op_context;
				header->callback(&re, header);
			}
		}
	}
}

/* General progress function */
void gasnetc_ofi_poll(int blocking_poll)
{
       rdma_empty_poll = 0;
#ifdef GASNET_PAR
       while(gasneti_weakatomic_read(&pending_rdma,0) && !rdma_empty_poll)
#else
       while(pending_rdma && !rdma_empty_poll)
#endif
		gasnetc_ofi_rdma_poll(blocking_poll);

	gasnetc_ofi_am_poll(blocking_poll);

	return;
}


/*------------------------------------------------
 * OFI conduit am send functions
 * ----------------------------------------------*/

int gasnetc_ofi_am_send_short(gasnet_node_t dest, gasnet_handler_t handler,
                     int numargs, va_list argptr, int isreq)
{
	int ret = GASNET_OK;
	gasnet_handlerarg_t *arglist;
	int i, len;

	/* Get a send buffer */
	/* If no available buffer, blocking poll */
	ofi_am_buf_t *header = gasneti_lifo_pop(&ofi_am_pool);
	if (NULL == header) {
		header = gasneti_malloc(sizeof(ofi_am_buf_t) + OFI_AM_MAX_DATA_LENGTH);
		header->callback = gasnetc_ofi_release_am;
		gasneti_leak(header);
	}

	/* Fill in the arguments */
	ofi_am_send_buf_t *sendbuf = &header->sendbuf;
	sendbuf->len = 0;
	arglist = (gasnet_handlerarg_t*) sendbuf->data;
	for (i = 0 ; i < numargs ; ++i) {
		arglist[i] = va_arg(argptr, gasnet_handlerarg_t);
		sendbuf->len += sizeof(gasnet_handlerarg_t);
	}

	/* Copy arg and handle into the buffer */
	sendbuf->isreq = isreq;
	sendbuf->handler = (uint8_t) handler;
	sendbuf->sourceid = gasneti_mynode;
	sendbuf->type = OFI_AM_SHORT;
	sendbuf->argnum = numargs;

	len = GASNETI_ALIGNUP(sendbuf->len + offsetof(ofi_am_send_buf_t, data), GASNETI_MEDBUF_ALIGNMENT);

	if (dest == gasneti_mynode) {
		gasnetc_ofi_handle_local_am(header);
		return 0;
	}

	if(len < max_buffered_send) {
		ret = fi_inject(ofi_am_epfd, sendbuf, len, GET_DEST(dest));
		while (ret == -FI_EAGAIN) {
			gasnetc_ofi_am_poll(0);
			ret = fi_inject(ofi_am_epfd, sendbuf, len, GET_DEST(dest));
		}
		if (GASNET_OK != ret) gasneti_fatalerror("fi_inject for short ashort failed: %d\n", ret);

		/* Data buffer is ready for reuse, handle it by callback function */
		header->callback(NULL, header);
	} else {
		ret = fi_send(ofi_am_epfd, sendbuf, len, NULL, GET_DEST(dest), &header->ctxt);
		while (ret == -FI_EAGAIN) {
			gasnetc_ofi_am_poll(0);
			ret = fi_send(ofi_am_epfd, sendbuf, len, NULL, GET_DEST(dest), &header->ctxt);
		}
		if (GASNET_OK != ret) gasneti_fatalerror("fi_send for short am failed: %d\n", ret);
	}
	return ret;
}

int gasnetc_ofi_am_send_medium(gasnet_node_t dest, gasnet_handler_t handler, 
                     void *source_addr, size_t nbytes,   /* data payload */
                     int numargs, va_list argptr, int isreq)
{
	int ret = GASNET_OK;
	gasnet_handlerarg_t *arglist;
	int i, len;

	gasneti_assert (nbytes <= gasnet_AMMaxMedium());

	/* Get a send buffer */
	/* If no available buffer, blocking poll */
	ofi_am_buf_t *header = gasneti_lifo_pop(&ofi_am_pool);
	if (NULL == header) {
		header = gasneti_malloc(sizeof(ofi_am_buf_t) + OFI_AM_MAX_DATA_LENGTH);
		header->callback = gasnetc_ofi_release_am;
		gasneti_leak(header);
	}

	/* Fill in the arguments */
	ofi_am_send_buf_t *sendbuf = &header->sendbuf;
	sendbuf->len = 0;
	arglist = (gasnet_handlerarg_t*) sendbuf->data;
	for (i = 0 ; i < numargs ; ++i) {
		arglist[i] = va_arg(argptr, gasnet_handlerarg_t);
		sendbuf->len += sizeof(gasnet_handlerarg_t);
	}
	sendbuf->len = GASNETI_ALIGNUP(sendbuf->len, GASNETI_MEDBUF_ALIGNMENT);

	memcpy((uint8_t *)(sendbuf->data)+sendbuf->len, source_addr, nbytes);
	sendbuf->len += nbytes;

	/* Copy arg and handle into the buffer */
	sendbuf->isreq = isreq;
	sendbuf->handler = (uint8_t) handler;
	sendbuf->sourceid = gasneti_mynode;
	sendbuf->type = OFI_AM_MEDIUM;
	sendbuf->argnum = numargs;
	sendbuf->nbytes = nbytes;

	len = GASNETI_ALIGNUP(sendbuf->len + offsetof(ofi_am_send_buf_t, data), GASNETI_MEDBUF_ALIGNMENT);

	if (dest == gasneti_mynode) {
		gasnetc_ofi_handle_local_am(header);
		return 0;
	}

	if(len < max_buffered_send) {
		ret = fi_inject(ofi_am_epfd, sendbuf, len, GET_DEST(dest));
		while (ret == -FI_EAGAIN) {
			gasnetc_ofi_am_poll(0);
			ret = fi_inject(ofi_am_epfd, sendbuf, len, GET_DEST(dest));
		}
		if (GASNET_OK != ret) gasneti_fatalerror("fi_inject for medium ashort failed: %d\n", ret);
		header->callback(NULL, header);
	} else {
		ret = fi_send(ofi_am_epfd, sendbuf, len, NULL, GET_DEST(dest), &header->ctxt);
		while (ret == -FI_EAGAIN) {
			gasnetc_ofi_am_poll(0);
			ret = fi_send(ofi_am_epfd, sendbuf, len, NULL, GET_DEST(dest), &header->ctxt);
		}
		if (GASNET_OK != ret) gasneti_fatalerror("fi_send for meduim am failed: %d\n", ret);
	}

	return ret;
}

int gasnetc_ofi_am_send_long(gasnet_node_t dest, gasnet_handler_t handler,
		               void *source_addr, size_t nbytes,   /* data payload */
		               void *dest_addr,
		               int numargs, va_list argptr, int isreq, int isasync)
{
	int ret = GASNET_OK;
	gasnet_handlerarg_t *arglist;
	int i, len;

	if(isreq)
		gasneti_assert (nbytes <= gasnet_AMMaxLongRequest());
	else
		gasneti_assert (nbytes <= gasnet_AMMaxLongReply());

	/* Get a send buffer */
	/* If no available buffer, blocking poll */
	ofi_am_buf_t *header = gasneti_lifo_pop(&ofi_am_pool);
	if (NULL == header) {
		header = gasneti_malloc(sizeof(ofi_am_buf_t) + OFI_AM_MAX_DATA_LENGTH);
		header->callback = gasnetc_ofi_release_am;
		gasneti_leak(header);
	}

	/* Fill in the arguments */
	ofi_am_send_buf_t *sendbuf = &header->sendbuf;
	sendbuf->len = 0;
	arglist = (gasnet_handlerarg_t*) sendbuf->data;
	for (i = 0 ; i < numargs ; ++i) {
		arglist[i] = va_arg(argptr, gasnet_handlerarg_t);
		sendbuf->len += sizeof(gasnet_handlerarg_t);
	}
	sendbuf->len = GASNETI_ALIGNUP(sendbuf->len, GASNETI_MEDBUF_ALIGNMENT);

	if(sendbuf->len + nbytes < OFI_AM_MAX_DATA_LENGTH)
	{
		/* Pack the payload if it's small enough */
		memcpy(sendbuf->data + sendbuf->len, source_addr, nbytes);
		sendbuf->len += nbytes;
		sendbuf->type = OFI_AM_LONG_MEDIUM;
	} else {
		/* Launch the long data payload transfer with RMA operation */
		if (dest == gasneti_mynode) {
			memcpy((void *)GET_DSTADDR(dest_addr, dest), source_addr, nbytes);
		} else {
			ofi_op_ctxt_t lam_ctxt;
			lam_ctxt.type = OFI_TYPE_AM_DATA;
			lam_ctxt.data_sent = 0;
			lam_ctxt.callback = gasnetc_ofi_handle_rdma;
			ret = fi_write(ofi_rdma_epfd, source_addr, nbytes, NULL, 
					GET_DEST(dest), GET_DSTADDR(dest_addr, dest), 0, &lam_ctxt.ctxt);
			while (ret == -FI_EAGAIN) {
				gasnetc_ofi_rdma_poll(0);
				ret = fi_write(ofi_rdma_epfd, source_addr, nbytes, NULL, 
						GET_DEST(dest), GET_DSTADDR(dest_addr, dest), 0, &lam_ctxt.ctxt);
			}
			if (GASNET_OK != ret) 
				gasneti_fatalerror("fi_rdma_writememto failed for AM long: %d\n", ret);
#ifdef GASNET_PAR
			gasneti_weakatomic_increment(&pending_rdma,0);
#else
			pending_rdma++;
#endif

			/* Because the order is not guaranteed between different ep, */
			/* we send the am part after confirming the large rdma operation */
			/* is successful. */
			while(!lam_ctxt.data_sent) {
#if GASNET_PSHM
				gasneti_AMPSHMPoll(0);
#endif
				gasnetc_ofi_rdma_poll(0);
			}
		}
		sendbuf->type = OFI_AM_LONG;
	}

	/* Copy arg and handle into the buffer */
	sendbuf->isreq = isreq;
	sendbuf->handler = (uint8_t) handler;
	sendbuf->sourceid = gasneti_mynode;
	sendbuf->argnum = numargs;
	sendbuf->dest_ptr = dest_addr;
	sendbuf->nbytes = nbytes;

	len = GASNETI_ALIGNUP(sendbuf->len + offsetof(ofi_am_send_buf_t, data), 
			GASNETI_MEDBUF_ALIGNMENT);

	if (dest == gasneti_mynode) {
		gasnetc_ofi_handle_local_am(header);
		return 0;
	}

	if(len < max_buffered_send) {
		ret = fi_inject(ofi_am_epfd, sendbuf, len, GET_DEST(dest));
		while (ret == -FI_EAGAIN) {
			gasnetc_ofi_am_poll(0);
			ret = fi_inject(ofi_am_epfd, sendbuf, len, GET_DEST(dest));
		}
		if (GASNET_OK != ret) gasneti_fatalerror("fi_inject for long ashort failed: %d\n", ret);
		header->callback(NULL, header);
	} else {
		ret = fi_send(ofi_am_epfd, sendbuf, len, NULL, GET_DEST(dest), &header->ctxt);
		while (ret == -FI_EAGAIN) {
			gasnetc_ofi_am_poll(0);
			ret = fi_send(ofi_am_epfd, sendbuf, len, NULL, GET_DEST(dest), &header->ctxt);
		}
		if (GASNET_OK != ret) gasneti_fatalerror("fi_send for long am failed: %d\n", ret);
	}

	return ret;
}

/*------------------------------------------------
 * OFI conduit one-sided put/get functions
 * ----------------------------------------------*/

void
gasnetc_rdma_put(gasnet_node_t dest, void *dest_addr, void *src_addr, size_t nbytes,
		void *ctxt_ptr)
{
	int ret = GASNET_OK;

	((ofi_op_ctxt_t *)ctxt_ptr)->callback = gasnetc_ofi_handle_rdma;
	ret = fi_write(ofi_rdma_epfd, src_addr, nbytes, NULL, GET_DEST(dest), 
			GET_DSTADDR(dest_addr, dest), 0, ctxt_ptr);
	while (ret == -FI_EAGAIN) {
		gasnetc_ofi_rdma_poll(0);
		ret = fi_write(ofi_rdma_epfd, src_addr, nbytes, NULL, GET_DEST(dest), 
				GET_DSTADDR(dest_addr, dest), 0, ctxt_ptr);
	}
	if (GASNET_OK != ret) 
		gasneti_fatalerror("fi_rdma_write for normal message failed: %d\n", ret);
#ifdef GASNET_PAR
	gasneti_weakatomic_increment(&pending_rdma,0);
#else
	pending_rdma++;
#endif
}

void
gasnetc_rdma_get(void *dest_addr, gasnet_node_t dest, void * src_addr, size_t nbytes,
		void *ctxt_ptr)
{
	int ret = GASNET_OK;

	((ofi_op_ctxt_t *)ctxt_ptr)->callback = gasnetc_ofi_handle_rdma;
	ret = fi_read(ofi_rdma_epfd, dest_addr, nbytes, NULL, GET_DEST(dest), 
			GET_SRCADDR(src_addr, dest), 0, ctxt_ptr);
	while (ret == -FI_EAGAIN) {
		gasnetc_ofi_rdma_poll(0);
		ret = fi_read(ofi_rdma_epfd, dest_addr, nbytes, NULL, GET_DEST(dest), 
				GET_SRCADDR(src_addr, dest), 0, ctxt_ptr);
	}
	if (GASNET_OK != ret) 
		gasneti_fatalerror("fi_rdma_readmemfrom failed: %d\n", ret);

#ifdef GASNET_PAR
	gasneti_weakatomic_increment(&pending_rdma,0);
#else
	pending_rdma++;
#endif
}

void
gasnetc_rdma_put_wait(gasnet_handle_t oph)
{
	int ret;
	gasnete_op_t *op = (gasnete_op_t*) oph;

	if (OPTYPE(op) == OPTYPE_EXPLICIT) {
		gasnete_eop_t *eop = (gasnete_eop_t *)op;
		while (!GASNETE_EOP_DONE(eop)) {
#if GASNET_PSHM
			gasneti_AMPSHMPoll(0);
#endif
			gasnetc_ofi_rdma_poll(0);
		}
	} else {
		gasnete_iop_t *iop = (gasnete_iop_t *)op;
		while (!GASNETE_IOP_CNTDONE(iop,put)) {
#if GASNET_PSHM
			gasneti_AMPSHMPoll(0);
#endif
			gasnetc_ofi_rdma_poll(0);
		}
	}
}

void
gasnetc_rdma_get_wait(gasnet_handle_t oph)
{
	int ret;
	gasnete_op_t *op = (gasnete_op_t*) oph;

	if (OPTYPE(op) == OPTYPE_EXPLICIT) {
		gasnete_eop_t *eop = (gasnete_eop_t *)op;
		while (!GASNETE_EOP_DONE(eop)) {
#if GASNET_PSHM
			gasneti_AMPSHMPoll(0);
#endif
			gasnetc_ofi_rdma_poll(0);
		}
	} else {
		gasnete_iop_t *iop = (gasnete_iop_t *)op;
		while (!GASNETE_IOP_CNTDONE(iop,get)) {
#if GASNET_PSHM
			gasneti_AMPSHMPoll(0);
#endif
			gasnetc_ofi_rdma_poll(0);
		}
	}
}

