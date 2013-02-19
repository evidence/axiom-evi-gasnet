#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_handler.h>
#include <gasnet_gemini.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>

#define GASNETC_NETWORKDEPTH_DEFAULT 12

#ifdef GASNET_CONDUIT_GEMINI
  /* Use dual-events + PI_FLUSH to get "proper" ordering w/ relaxed and default PI ordering */
  #define FIX_HT_ORDERING 1
  static uint16_t gasnetc_fma_put_cq_mode = GNI_CQMODE_GLOBAL_EVENT;
#else
  #define FIX_HT_ORDERING 0
#endif

int      gasnetc_dev_id;
uint32_t gasnetc_cookie;
uint32_t gasnetc_address;
uint8_t  gasnetc_ptag;

static uint32_t gasnetc_memreg_flags;
static int gasnetc_mem_consistency;

int gasnetc_poll_burst = 10;

static double shutdown_max;
static uint32_t sys_exit_rcvd;

typedef struct peer_struct {
  struct peer_struct *next; /* pointer to next when queue, GC_NOT_QUEUED otherwise */
  gni_ep_handle_t ep_handle;
  gni_mem_handle_t mem_handle;
  gasneti_weakatomic_t am_credit;
  gasnet_node_t rank;
} peer_struct_t;

static peer_struct_t *peer_data;

static gni_mem_handle_t my_smsg_handle;
static gni_mem_handle_t my_mem_handle;

static gni_cdm_handle_t cdm_handle;
static gni_nic_handle_t nic_handle;
static gni_cq_handle_t bound_cq_handle;
static gni_cq_handle_t smsg_cq_handle;
static gni_cq_handle_t destination_cq_handle;

static void *smsg_mmap_ptr;
static size_t smsg_mmap_bytes;

static gasnet_seginfo_t gasnetc_bounce_buffers;
static gasnet_seginfo_t gasnetc_pd_buffers;

/*------ Resource accounting ------*/

/* Limit the number of active dynamic memory registrations */
static gasneti_weakatomic_t reg_credit;
#define gasnetc_alloc_reg_credit()         gasnetc_weakatomic_dec_if_positive(&reg_credit)
#if GASNET_DEBUG && 0
static gasneti_weakatomic_val_t reg_credit_max;
#define gasnetc_init_reg_credit(_val)      gasneti_weakatomic_set(&reg_credit,reg_credit_max=(_val),0)
  static void gasnetc_return_reg_credit(void) {
    gasneti_weakatomic_val_t new_val = gasneti_weakatomic_add(&reg_credit,1,0);
    gasneti_assert(new_val <= reg_credit_max);
  }
#else
  #define gasnetc_init_reg_credit(_val)    gasneti_weakatomic_set(&reg_credit,(_val),0)
  #define gasnetc_return_reg_credit()      gasneti_weakatomic_increment(&reg_credit,0)
#endif

/*------ Convience functions for printing error messages ------*/

static const char *gni_return_string(gni_return_t status)
{
  if (status == GNI_RC_SUCCESS) return ("GNI_RC_SUCCESS");
  if (status == GNI_RC_NOT_DONE) return ("GNI_RC_NOT_DONE");
  if (status == GNI_RC_INVALID_PARAM) return ("GNI_RC_INVALID_PARAM");
  if (status == GNI_RC_ERROR_RESOURCE) return ("GNI_RC_ERROR_RESOURCE");
  if (status == GNI_RC_TIMEOUT) return ("GNI_RC_TIMEOU");
  if (status == GNI_RC_PERMISSION_ERROR) return ("GNI_RC_PERMISSION_ERROR");
  if (status == GNI_RC_DESCRIPTOR_ERROR) return ("GNI_RC_DESCRIPTOR ERROR");
  if (status == GNI_RC_ALIGNMENT_ERROR) return ("GNI_RC_ALIGNMENT_ERROR");
  if (status == GNI_RC_INVALID_STATE) return ("GNI_RC_INVALID_STATE");
  if (status == GNI_RC_NO_MATCH) return ("GNI_RC_NO_MATCH");
  if (status == GNI_RC_SIZE_ERROR) return ("GNI_RC_SIZE_ERROR");
  if (status == GNI_RC_TRANSACTION_ERROR) return ("GNI_RC_TRANSACTION_ERROR");
  if (status == GNI_RC_ILLEGAL_OP) return ("GNI_RC_ILLEGAL_OP");
  if (status == GNI_RC_ERROR_NOMEM) return ("GNI_RC_ERROR_NOMEM");
  return("unknown");
}


const char *gasnetc_type_string(int type)
{
  if (type == GC_CMD_NULL) return("GC_CMD_AM_NULL");
  if (type == GC_CMD_AM_SHORT) return ("GC_CMD_AM_SHORT");
  if (type == GC_CMD_AM_LONG) return ("GC_CMD_AM_LONG");
  if (type == GC_CMD_AM_MEDIUM) return ("GC_CMD_AM_MEDIUM");
  if (type == GC_CMD_AM_SHORT_REPLY) return ("GC_CMD_AM_SHORT_REPLY");
  if (type == GC_CMD_AM_LONG_REPLY) return ("GC_CMD_AM_LONG_REPLY");
  if (type == GC_CMD_AM_MEDIUM_REPLY) return ("GC_CMD_AM_MEDIUM_REPLY");
  if (type == GC_CMD_AM_NOP_REPLY) return ("GC_CMD_AM_NOP_REPLY");
  if (type == GC_CMD_SYS_SHUTDOWN_REQUEST) return ("GC_CMD_SYS_SHUTDOWN_REQUEST");
  return("unknown");
}

const char *gasnetc_post_type_string(gni_post_type_t type)
{
  if (type == GNI_POST_RDMA_PUT) return("GNI_POST_RDMA_PUT");
  if (type == GNI_POST_RDMA_GET) return("GNI_POST_RDMA_GET");
  if (type == GNI_POST_FMA_PUT) return("GNI_POST_FMA_PUT");
  if (type == GNI_POST_FMA_GET) return("GNI_POST_FMA_GET");
  return("unknown");
}

/*------ Work Queue threading peer_struct_t with pending AM receives ------*/

struct {
  peer_struct_t *head;
  peer_struct_t *tail;
  gasnetc_queuelock_t lock;
} smsg_work_queue;

#define GC_NOT_QUEUED ((struct peer_struct*)(uintptr_t)1)
#define GC_IS_QUEUED(_qi) \
        (gasneti_assert((_qi) != NULL), ((_qi)->next != GC_NOT_QUEUED))

void gasnetc_work_queue_init(void)
{
  smsg_work_queue.head = NULL;
  smsg_work_queue.tail = NULL;
  GASNETC_INITLOCK_QUEUE(&smsg_work_queue);
}

/* Remove qi from HEAD of work queue - acquires and releases lock */
GASNETI_INLINE(gasnetc_work_dequeue)
peer_struct_t *gasnetc_work_dequeue(void)
{
  peer_struct_t *qi;
  GASNETC_LOCK_QUEUE(&smsg_work_queue);
  qi = smsg_work_queue.head;
  if (qi != NULL) {
    smsg_work_queue.head = qi->next;
    if (smsg_work_queue.head == NULL) smsg_work_queue.tail = NULL;
    qi->next = GC_NOT_QUEUED;
  }
  GASNETC_UNLOCK_QUEUE(&smsg_work_queue);
  return(qi);
}

/* Add qi at TAIL of work queue - caller must hold lock */
GASNETI_INLINE(gasnetc_work_enqueue_nolock)
void gasnetc_work_enqueue_nolock(peer_struct_t *qi)
{
  gasneti_assert(qi != NULL);
  gasneti_assert(qi->next == GC_NOT_QUEUED);
  /* GASNETC_LOCK_QUEUE(&smsg_work_queue); */
  if (smsg_work_queue.head == NULL) {
    smsg_work_queue.head = qi;
  } else {
    gasneti_assert(smsg_work_queue.tail != NULL);
    smsg_work_queue.tail->next = qi;
  }
  smsg_work_queue.tail = qi;
  qi->next = NULL;
  /* GASNETC_UNLOCK_QUEUE(&smsg_work_queue); */
}

/*-------------------------------------------------*/
/* We don't allocate resources for comms w/ self or PSHM-reachable peers */

#if GASNET_PSHM
  #define node_is_local(_i) gasneti_pshm_in_supernode(_i)
#else
  #define node_is_local(_i) ((_i) == gasneti_mynode)
#endif

/* From point-of-view of a remote node, what is MY index as an Smsg peer? */
GASNETI_INLINE(my_smsg_index)
int my_smsg_index(gasnet_node_t remote_node) {
#if GASNET_PSHM
  int i, result = 0;

  gasneti_assert(NULL != gasneti_nodemap);
  for (i = 0; i < gasneti_mynode; ++i) {
    /* counts nodes that are not local to remote_node */
    result += (gasneti_nodemap[i] != gasneti_nodemap[remote_node]);
  }
  return result;
#else
  /* we either fall before or after the remote node skips over itself */
  return gasneti_mynode - (gasneti_mynode > remote_node);
#endif
}

/*-------------------------------------------------*/

static uint32_t *gather_nic_addresses(void)
{
  uint32_t *result = gasneti_malloc(gasneti_nodes * sizeof(uint32_t));

  if (gasnetc_dev_id == -1) {
    /* no value was found in environment */
    gni_return_t status;
    uint32_t cpu_id;

    gasnetc_dev_id  = 0;
    status = GNI_CdmGetNicAddress(gasnetc_dev_id, &gasnetc_address, &cpu_id);
    if (status != GNI_RC_SUCCESS) {
      gasnetc_GNIT_Abort("GNI_CdmGetNicAddress failed: %s", gni_return_string(status));
    }
  } else {
    /* use gasnetc_address taken from the environment */
  }

  gasnetc_bootstrapExchange(&gasnetc_address, sizeof(uint32_t), result);

  return result;
}

/*-------------------------------------------------*/
/* called after segment init. See gasneti_seginfo */
/* allgather the memory handles for the segments */
/* create endpoints */
void gasnetc_init_segment(void *segment_start, size_t segment_size)
{
  gni_return_t status;
  /* Map the shared segment */

  { int envval = gasneti_getenv_int_withdefault("GASNETC_GNI_MEMREG", GASNETC_GNI_MEMREG_DEFAULT, 0);
    if (envval < 1) envval = 1;
    gasnetc_init_reg_credit(envval);
  }

  gasnetc_mem_consistency = GASNETC_DEFAULT_RDMA_MEM_CONSISTENCY;
  { char * envval = gasneti_getenv("GASNETC_GNI_MEM_CONSISTENCY");
    if (!envval || !envval[0]) {
      /* No value given - keep default */
    } else if (!strcmp(envval, "strict") || !strcmp(envval, "STRICT")) {
      gasnetc_mem_consistency = GASNETC_STRICT_MEM_CONSISTENCY;
    } else if (!strcmp(envval, "relaxed") || !strcmp(envval, "RELAXED")) {
      gasnetc_mem_consistency = GASNETC_RELAXED_MEM_CONSISTENCY;
    } else if (!strcmp(envval, "default") || !strcmp(envval, "DEFAULT")) {
      gasnetc_mem_consistency = GASNETC_DEFAULT_MEM_CONSISTENCY;
    } else if (!gasneti_mynode) {
      fflush(NULL);
      fprintf(stderr, "WARNING: ignoring unknown value '%s' for environment "
                      "variable GASNETC_GNI_MEM_CONSISTENCY\n", envval);
      fflush(NULL);
    }
  }

  switch (gasnetc_mem_consistency) {
    case GASNETC_STRICT_MEM_CONSISTENCY:
      gasnetc_memreg_flags = GNI_MEM_STRICT_PI_ORDERING;
      break;
    case GASNETC_RELAXED_MEM_CONSISTENCY:
      gasnetc_memreg_flags = GNI_MEM_RELAXED_PI_ORDERING;
      break;
    case GASNETC_DEFAULT_MEM_CONSISTENCY:
      gasnetc_memreg_flags = 0;
      break;
  }
  gasnetc_memreg_flags |= GNI_MEM_READWRITE;

#if FIX_HT_ORDERING
  if (gasnetc_mem_consistency != GASNETC_STRICT_MEM_CONSISTENCY) {
    gasnetc_memreg_flags |= GNI_MEM_PI_FLUSH; 
    gasnetc_fma_put_cq_mode |= GNI_CQMODE_REMOTE_EVENT;

    /* With 1 completion entry this queue is INTENDED to always overflow */
    status = GNI_CqCreate(nic_handle, 1, 0, GNI_CQ_NOBLOCK, NULL, NULL, &destination_cq_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
  }
#endif

  {
    int count = 0;
    for (;;) {
      status = GNI_MemRegister(nic_handle, (uint64_t) segment_start, 
			       (uint64_t) segment_size, destination_cq_handle,
			       gasnetc_memreg_flags, -1, 
			       &my_mem_handle);
      if (status == GNI_RC_SUCCESS) break;
      if (status == GNI_RC_ERROR_RESOURCE) {
	fprintf(stderr, "MemRegister segment fault %d at  %p %lx, code %s\n", 
		count, segment_start, segment_size, gni_return_string(status));
	count += 1;
	if (count >= 10) break;
      } else {
	break;
      }
    }
  }
			       
  assert (status == GNI_RC_SUCCESS);
#if GASNETC_DEBUG
  gasnetc_GNIT_Log("segment mapped %p, %p", segment_start, (void *) segment_size);
#endif
  
  {
    gni_mem_handle_t *all_mem_handle = gasneti_malloc(gasneti_nodes * sizeof(gni_mem_handle_t));
    gasnet_node_t i;
    gasnetc_bootstrapExchange(&my_mem_handle, sizeof(gni_mem_handle_t), all_mem_handle);
    for (i = 0; i < gasneti_nodes; ++i) {
      peer_data[i].mem_handle = all_mem_handle[i];
    }
    gasneti_free(all_mem_handle);
  }
}

uintptr_t gasnetc_init_messaging(void)
{
  const gasnet_node_t remote_nodes = gasneti_nodes - (GASNET_PSHM ? gasneti_nodemap_local_count : 1);
  gni_return_t status;
  gni_smsg_attr_t my_smsg_attr;
  gni_smsg_type_t smsg_type;
  uint32_t *all_addr;
  uint32_t remote_addr;
  uint32_t local_address;
  uint32_t i;
  unsigned int bytes_per_mbox;
  unsigned int bytes_needed;
  unsigned int mb_maxcredit;
  unsigned int am_maxcredit;
  int modes = 0;

#if GASNETC_DEBUG
  gasnetc_GNIT_Log("entering");
  modes |= GNI_CDM_MODE_ERR_NO_KILL;
#endif

  GASNETC_INITLOCK_GNI();

  gasnetc_work_queue_init();

  status = GNI_CdmCreate(gasneti_mynode,
			 gasnetc_ptag, gasnetc_cookie,
			 modes,
			 &cdm_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  status = GNI_CdmAttach(cdm_handle,
			 gasnetc_dev_id,
			 &local_address,
			 &nic_handle);

  gasneti_assert_always (status == GNI_RC_SUCCESS);

#if GASNETC_DEBUG
  gasnetc_GNIT_Log("cdmattach");
#endif

#if GASNETC_SMSG_RETRANSMIT
  smsg_type = GNI_SMSG_TYPE_MBOX_AUTO_RETRANSMIT;
#else
  smsg_type = GNI_SMSG_TYPE_MBOX;
#endif
 
  { /* Determine credits for AMs: GASNET_NETWORKDEPTH */
    int depth = gasneti_getenv_int_withdefault("GASNET_NETWORKDEPTH",
                                               GASNETC_NETWORKDEPTH_DEFAULT, 0);
    am_maxcredit = MAX(1,depth); /* Min is 1 */
    mb_maxcredit = 2 * am_maxcredit + 2; /* (req + reply) = 2 , +2 for "lag"?*/
  }

  { /* Determine Cq size: GASNETC_GNI_NUM_PD */
    int num_pd, cq_entries;
    num_pd = gasneti_getenv_int_withdefault("GASNETC_GNI_NUM_PD",
                                            GASNETC_GNI_NUM_PD_DEFAULT,1);

    cq_entries = num_pd+2; /* XXX: why +2 ?? */

    status = GNI_CqCreate(nic_handle, cq_entries, 0, GNI_CQ_NOBLOCK, NULL, NULL, &bound_cq_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
  }

  /* init peer_data */
  all_addr = gather_nic_addresses();
  peer_data = gasneti_malloc(gasneti_nodes * sizeof(peer_struct_t));
  for (i = 0; i < gasneti_nodes; i += 1) {
    if (node_is_local(i)) continue; /* no connection to self or PSHM-reachable peers */
    peer_data[i].next = GC_NOT_QUEUED;
    status = GNI_EpCreate(nic_handle, bound_cq_handle, &peer_data[i].ep_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
    status = GNI_EpBind(peer_data[i].ep_handle, all_addr[i], i);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
#if GASNETC_DEBUG
    gasnetc_GNIT_Log("ep bound to %d, addr %d", i, all_addr[i]);
#endif
    /* mem_handle will be set at end of gasnetc_init_segment */
    gasneti_weakatomic_set(&peer_data[i].am_credit, am_maxcredit, 0);
    peer_data[i].rank = i;
  }
  gasneti_free(all_addr);

  /* Initialize the short message system */

  /*
   * allocate a CQ in which to receive message notifications
   */
  /* TODO: is "* 2" still correct given mb_maxcredit has been halved since the original code? */
  /* MAX(1,) avoids complication for remote_nodes==0 */
  status = GNI_CqCreate(nic_handle,MAX(1,remote_nodes*mb_maxcredit* 2),0,GNI_CQ_NOBLOCK,NULL,NULL,&smsg_cq_handle);
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("GNI_CqCreate returned error %s\n", gni_return_string(status));
  }
  
  /*
   * Set up an mmap region to contain all of my mailboxes.
   * The GNI_SmsgBufferSizeNeeded is used to determine how
   * much memory is needed for each mailbox.
   */

  my_smsg_attr.msg_type = smsg_type;
  my_smsg_attr.mbox_maxcredit = mb_maxcredit;
  my_smsg_attr.msg_maxsize = GASNETC_MSG_MAXSIZE;
#if GASNETC_DEBUG
  fprintf(stderr,"r %d maxcredit %d msg_maxsize %d\n", gasneti_mynode, mb_maxcredit, (int)GASNETC_MSG_MAXSIZE);
#endif

  status = GNI_SmsgBufferSizeNeeded(&my_smsg_attr,&bytes_per_mbox);
  if (status != GNI_RC_SUCCESS){
    gasnetc_GNIT_Abort("GNI_GetSmsgBufferSize returned error %s\n",gni_return_string(status));
  }
#if GASNETC_DEBUG
  fprintf(stderr,"r %d GetSmsgBufferSize says %d bytes for each mailbox\n", gasneti_mynode, bytes_per_mbox);
#endif
  bytes_per_mbox = GASNETI_ALIGNUP(bytes_per_mbox, GASNETC_CACHELINE_SIZE);
  /* test */
  bytes_per_mbox += my_smsg_attr.mbox_maxcredit * my_smsg_attr.msg_maxsize;
  /* TODO: remove MAX(1,) while still avoiding "issues" on single-(super)node runs */
  bytes_needed = MAX(1,remote_nodes) * bytes_per_mbox;
  
#if GASNETC_DEBUG
  fprintf(stderr,"Allocating %d bytes for each mailbox\n",bytes_per_mbox);
  fprintf(stderr,"max medium %d, sizeof medium %d\n",(int)gasnet_AMMaxMedium(),
	  (int)sizeof(gasnetc_am_medium_packet_t));
#endif
  smsg_mmap_ptr = gasneti_huge_mmap(NULL, bytes_needed);
  smsg_mmap_bytes = bytes_needed;
  if (smsg_mmap_ptr == (char *)MAP_FAILED) {
    gasnetc_GNIT_Abort("hugepage mmap failed: ");
  }
  
#if GASNETC_DEBUG
  gasnetc_GNIT_Log("mmap %p", smsg_mmap_ptr);
#endif
  {
    int count = 0;
    for (;;) {
      status = GNI_MemRegister(nic_handle, 
			       (unsigned long)smsg_mmap_ptr, 
			       bytes_needed,
			       smsg_cq_handle,
			       GNI_MEM_STRICT_PI_ORDERING | GNI_MEM_PI_FLUSH | GNI_MEM_READWRITE,
			       -1,
			       &my_smsg_attr.mem_hndl);
      if (status == GNI_RC_SUCCESS) break;
      if (status == GNI_RC_ERROR_RESOURCE) {
	fprintf(stderr, "MemRegister smsg fault %d at  %p %x, code %s\n", 
		count, smsg_mmap_ptr, bytes_needed, gni_return_string(status));
	count += 1;
	if (count >= 10) break;
      } else {
	break;
      }
    }
  }
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("GNI_MemRegister returned error %s\n",gni_return_string(status));
  }
  my_smsg_handle = my_smsg_attr.mem_hndl;

#if GASNETC_DEBUG
  gasnetc_GNIT_Log("smsg region registered");
#endif

  my_smsg_attr.msg_type = smsg_type;
  my_smsg_attr.msg_buffer = smsg_mmap_ptr;
  my_smsg_attr.buff_size = bytes_per_mbox;
  my_smsg_attr.mbox_maxcredit = mb_maxcredit;
  my_smsg_attr.msg_maxsize = GASNETC_MSG_MAXSIZE;
  my_smsg_attr.mbox_offset = 0;

  /* exchange peer data and initialize smsg */
  { struct smsg_exchange { void *addr; gni_mem_handle_t handle; };
    struct smsg_exchange my_smsg_exchg = { smsg_mmap_ptr, my_smsg_handle };
    struct smsg_exchange *all_smsg_exchg = gasneti_malloc(gasneti_nodes * sizeof(struct smsg_exchange));
    gni_smsg_attr_t remote_attr;

    gasnetc_bootstrapExchange(&my_smsg_exchg, sizeof(struct smsg_exchange), all_smsg_exchg);

    remote_attr.msg_type = smsg_type;
    remote_attr.buff_size = bytes_per_mbox;
    remote_attr.mbox_maxcredit = mb_maxcredit;
    remote_attr.msg_maxsize = GASNETC_MSG_MAXSIZE;
  
    /* At this point all_smsg_exchg has the required information for everyone */
    for (i = 0; i < gasneti_nodes; i += 1) {
      if (node_is_local(i)) continue; /* no connection to self or PSHM-reachable peers */

      remote_attr.msg_buffer  = all_smsg_exchg[i].addr;
      remote_attr.mem_hndl    = all_smsg_exchg[i].handle;
      remote_attr.mbox_offset = bytes_per_mbox * my_smsg_index(i);

      status = GNI_SmsgInit(peer_data[i].ep_handle, &my_smsg_attr, &remote_attr);
      if (status != GNI_RC_SUCCESS) {
        gasnetc_GNIT_Abort("GNI_SmsgInit returned error %s\n", gni_return_string(status));
      }

      my_smsg_attr.mbox_offset += bytes_per_mbox;
    }

    gasneti_free(all_smsg_exchg);
  }

  /* Now make sure everyone is ready */
#if GASNETC_DEBUG
  gasnetc_GNIT_Log("finishing");
#endif
  /* set the number of seconds we poll until forceful shutdown.
   * May be over-ridden by env-vars.
   */
  gasnetc_shutdown_seconds = gasneti_get_exittimeout(shutdown_max, 3., 0.125, 0.);

  gasnetc_fma_rdma_cutover = 
    gasneti_getenv_int_withdefault("GASNETC_GNI_FMA_RDMA_CUTOVER",
				   GASNETC_GNI_FMA_RDMA_CUTOVER_DEFAULT,1);
  if (gasnetc_fma_rdma_cutover > GASNETC_GNI_FMA_RDMA_CUTOVER_MAX)
    gasnetc_fma_rdma_cutover = GASNETC_GNI_FMA_RDMA_CUTOVER_MAX;

  gasnetc_bootstrapBarrier();

  return bytes_needed;
}

void gasnetc_shutdown(void)
{
  int i;
  int tries;
  int left;
  gni_return_t status;
  /* seize gni lock and hold it  */
  GASNETC_LOCK_GNI();
  /* Do other threads need to be killed off here?
     release resources in the reverse order of acquisition
   */

  /* for each rank */
  tries = 0;
  left = gasneti_nodes - (GASNET_PSHM ? gasneti_nodemap_local_count : 1);
  while (left > 0) {
    tries += 1;
    for (i = 0; i < gasneti_nodes; i += 1) {
      if (node_is_local(i)) continue; /* no connection to self or PSHM-reachable peers */
      if (peer_data[i].ep_handle != NULL) {
	status = GNI_EpUnbind(peer_data[i].ep_handle);
	if (status != GNI_RC_SUCCESS) {
	  fprintf(stderr, "node %d shutdown epunbind %d try %d  got %s\n",
		  gasneti_mynode, i, tries, gni_return_string(status));
	} 
	status = GNI_EpDestroy(peer_data[i].ep_handle);
	if (status != GNI_RC_SUCCESS) {
	  fprintf(stderr, "node %d shutdown epdestroy %d try %d  got %s\n",
		  gasneti_mynode, i, tries, gni_return_string(status));
	} else {
	  peer_data[i].ep_handle = NULL;
	  left -= 1;
	}
      }
    }
    if (tries >= 10) break;
  }
  if (left > 0) {
    fprintf(stderr, "node %d %d endpoints left after 10 tries\n", 
	    gasneti_mynode, left);
  }

  if (gasneti_attach_done) {
    status = GNI_MemDeregister(nic_handle, &my_mem_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
  }

  gasneti_huge_munmap(smsg_mmap_ptr, smsg_mmap_bytes);

  status = GNI_MemDeregister(nic_handle, &my_smsg_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  if (destination_cq_handle) {
    status = GNI_CqDestroy(destination_cq_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
  }

  status = GNI_CqDestroy(smsg_cq_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  status = GNI_CqDestroy(bound_cq_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);


  status = GNI_CdmDestroy(cdm_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);
  /*  fprintf(stderr, "node %d gasnetc_shutdown done\n", gasneti_mynode); */
}


GASNETI_INLINE(gasnetc_handle_am_short_packet)
void gasnetc_handle_am_short_packet(int req, gasnet_node_t source, 
			       gasnetc_am_short_packet_t *am)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnetc_token_t the_token = { source, req };
  gasnet_token_t token = (gasnet_token_t)&the_token; /* RUN macro needs al lvalue */
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_SHORT(req, 
			    handlerindex,
			    handler,
			    token,
			    pargs,
			    numargs);
  if (!req) {
    /* TODO: would returning credit before running handler help or hurt? */
    gasnetc_return_am_credit(source);
  } else if (the_token.need_reply) {
    gasnetc_send_am_nop(source);
  }
}

GASNETI_INLINE(gasnetc_handle_am_medium_packet)
void gasnetc_handle_am_medium_packet(int req, gasnet_node_t source, 
				gasnetc_am_medium_packet_t *am, void* data)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnetc_token_t the_token = { source, req };
  gasnet_token_t token = (gasnet_token_t)&the_token; /* RUN macro needs al lvalue */
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_MEDIUM(req, 
			     handlerindex,
			     handler,
			     token,
			     pargs,
			     numargs,
			     data,
			     am->header.misc);
  if (!req) {
    /* TODO: would returning credit before running handler help or hurt? */
    gasnetc_return_am_credit(source);
  } else if (the_token.need_reply) {
    gasnetc_send_am_nop(source);
  }
}

GASNETI_INLINE(gasnetc_handle_am_long_packet)
void gasnetc_handle_am_long_packet(int req, gasnet_node_t source, 
			      gasnetc_am_long_packet_t *am)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnetc_token_t the_token = { source, req };
  gasnet_token_t token = (gasnet_token_t)&the_token; /* RUN macro needs al lvalue */
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_LONG(req, 
			   handlerindex,
			   handler,
			   token,
			   pargs,
			   numargs,
			   am->data,
			   am->data_length);
  if (!req) {
    /* TODO: would returning credit before running handler help or hurt? */
    gasnetc_return_am_credit(source);
  } else if (the_token.need_reply) {
    gasnetc_send_am_nop(source);
  }
}


extern void gasnetc_handle_sys_shutdown_packet(uint32_t source, gasnetc_sys_shutdown_packet_t *sys);
extern void  gasnetc_poll_smsg_completion_queue(void);

void gasnetc_process_smsg_q(gasnet_node_t pe)
{
  peer_struct_t * const peer = &peer_data[pe];
  gni_return_t status;
  GC_Header_t *recv_header;
  union {
    gasnetc_packet_t packet;
    uint8_t raw[GASNETC_HEADLEN(medium, gasnet_AMMaxArgs()) + gasnet_AMMaxMedium()];
    uint64_t dummy_for_alignment;
  } buffer;
  size_t head_length;
  size_t length;
  uint32_t numargs;
  int is_req;
  for (;;) {
    GASNETC_LOCK_GNI();
    status = GNI_SmsgGetNext(peer->ep_handle, 
			     (void **) &recv_header);
    GASNETC_UNLOCK_GNI_IF_SEQ();
    if (status == GNI_RC_SUCCESS) {
      gasneti_assert((((uintptr_t) recv_header) & 7) == 0);
      numargs = recv_header->numargs;
      gasneti_assert(numargs <= gasnet_AMMaxArgs());
      GASNETI_TRACE_PRINTF(A, ("smsg r from %d to %d type %s\n", pe, gasneti_mynode, gasnetc_type_string(recv_header->command)));
      is_req = (1 & recv_header->command); /* Requests have ODD values */
      switch (recv_header->command) {
      case GC_CMD_AM_NOP_REPLY: {
	status = GNI_SmsgRelease(peer->ep_handle);
        GASNETC_UNLOCK_GNI_IF_PAR();
	gasnetc_return_am_credit(pe);
	break;
      }
      case GC_CMD_AM_SHORT:
      case GC_CMD_AM_SHORT_REPLY: {
	head_length = GASNETC_HEADLEN(short, numargs);
	memcpy(&buffer, recv_header, head_length);
	status = GNI_SmsgRelease(peer->ep_handle);
	GASNETC_UNLOCK_GNI_IF_PAR();
	gasnetc_handle_am_short_packet(is_req, pe, &buffer.packet.gasp);
	break;
      }
      case GC_CMD_AM_MEDIUM:
      case GC_CMD_AM_MEDIUM_REPLY: {
	head_length = GASNETC_HEADLEN(medium, numargs);
	length = head_length + recv_header->misc;
	memcpy(&buffer, recv_header, length);
	gasneti_assert(recv_header->misc <= gasnet_AMMaxMedium());
	status = GNI_SmsgRelease(peer->ep_handle);
	GASNETC_UNLOCK_GNI_IF_PAR();
	gasnetc_handle_am_medium_packet(is_req, pe, &buffer.packet.gamp, &buffer.raw[head_length]);
	break;
      }
      case GC_CMD_AM_LONG:
      case GC_CMD_AM_LONG_REPLY: {
	head_length = GASNETC_HEADLEN(long, numargs);
	memcpy(&buffer, recv_header, head_length);
	if (buffer.packet.galp.header.misc) { /* payload follows header - copy it into place */
	  void *im_data = (void *) (((uintptr_t) recv_header) + head_length);
	  memcpy(buffer.packet.galp.data, im_data, buffer.packet.galp.data_length);
	}
	status = GNI_SmsgRelease(peer->ep_handle);
	GASNETC_UNLOCK_GNI_IF_PAR();
	gasnetc_handle_am_long_packet(is_req, pe, &buffer.packet.galp);
	break;
      }
      case GC_CMD_SYS_SHUTDOWN_REQUEST: {
	memcpy(&buffer, recv_header, sizeof(buffer.packet.gssp));
	status = GNI_SmsgRelease(peer->ep_handle);
	GASNETC_UNLOCK_GNI_IF_PAR();
	gasnetc_handle_sys_shutdown_packet(pe, &buffer.packet.gssp);
	break;
      }
      default: {
	gasnetc_GNIT_Abort("unknown packet type");
      }
      }
      /* now check the SmsgRelease status */      
      if (status == GNI_RC_SUCCESS) {
	/* LCS nothing to do */
      } else {
	/* LCS SmsgRelease Failed */
	/* GNI_RC_INVALID_PARAM here means bad endpoint */
	/* GNI_RC_NOT_DONE here means there was no smsg */
	gasnetc_GNIT_Log("SmsgRelease from pe %d fail with %s\n",
		   pe, gni_return_string(status));
      }
    } else if (status == GNI_RC_NOT_DONE) {
      GASNETC_UNLOCK_GNI_IF_PAR();
      break;  /* GNI_RC_NOT_DONE here means there was no smsg */
    } else {
      gasnetc_GNIT_Abort("SmsgGetNext from pe %d fail with %s", 
		 pe, gni_return_string(status));
    }
    gasnetc_poll_smsg_completion_queue();
  }
}


#define SMSG_BURST 20
#define SMSG_PEER_BURST 4

/* algorithm, imagining that several threads call this at once
 * seize the GNI lock
 *   call CqGetEvent BURST times, into a local array
 * release the GNI lock
 * if any messages found:
 * seize the smsq_work_queue lock
 *   scan the local array, adding peers to the work queue if needed
 * release the smsg work queue lock
 * check the final status from the CqGetEvents
 *   if queue overflow
 *   seize the GNI lock and drain the cq
 *   seize the smsg_work_queue_lock and enqueue everyone not already there
 * up to PEER_BURST times
 *  try to dequeue a peer and process messages from them
 */

void gasnetc_poll_smsg_completion_queue(void)
{
  gni_return_t status;
  gasnet_node_t source;
  gni_cq_entry_t event_data[SMSG_BURST];
  int messages;
  int i;

  /* grab the gni lock, then spin through SMSG_BURST calls to
   * CqGetEvent as fast as possible, saving the interpretation
   * for later.  The GNI lock is global, for all GNI api activity.
   */
  messages = 0;
  GASNETC_LOCK_GNI();
  for (;;) {
    status = GNI_CqGetEvent(smsg_cq_handle,&event_data[messages]);
    if (status != GNI_RC_SUCCESS) break;
    messages += 1;
    if (messages >= SMSG_BURST) break;
  }
  GASNETC_UNLOCK_GNI();
  /* Now run through what you found */
  if (messages > 0) {
    GASNETC_LOCK_QUEUE(&smsg_work_queue);
    for (i = 0; i < messages; i += 1) {
      source = gni_cq_get_inst_id(event_data[i]);
      gasneti_assert(source < gasneti_nodes);
      /* atomically enqueue the peer on the smsg queue if it isn't
	 already there.  */
      if (!GC_IS_QUEUED(&peer_data[source]))
	gasnetc_work_enqueue_nolock(&peer_data[source]);
    }
    GASNETC_UNLOCK_QUEUE(&smsg_work_queue);
  }
  /* Now check the final status from the CqGetEvent */
  /* the most likely case is no more messages */
  /* The next most likely case is a full BURST, ending in success */
  /* Next is queue overflow, which shouldn't happen but is recoverable */
  if (status == GNI_RC_NOT_DONE) {
    /* nothing to do */
  } else if (status == GNI_RC_SUCCESS) {
    /* nothing to do */
  } else if (status == GNI_RC_ERROR_RESOURCE) {
    /* drain the cq completely */
    GASNETC_LOCK_GNI();
    for (;;) {
      status = GNI_CqGetEvent(smsg_cq_handle,&event_data[0]);
      if (status == GNI_RC_SUCCESS) continue;
      if (status == GNI_RC_ERROR_RESOURCE) continue;
      if (status == GNI_RC_NOT_DONE) break;
      gasnetc_GNIT_Abort("CqGetEvent(smsg_cq) drain returns error %s", gni_return_string(status));
    }
    GASNETC_UNLOCK_GNI();
    /* and enqueue everyone on the work queue, who isn't already */
    GASNETC_LOCK_QUEUE(&smsg_work_queue);
    for (source = 0; source < gasneti_nodes; source += 1) {
      if (node_is_local(source)) continue; /* no AMs for self or PSHM-reachable peers */
      if (!GC_IS_QUEUED(&peer_data[source]))
	gasnetc_work_enqueue_nolock(&peer_data[source]);
    }
    GASNETC_UNLOCK_QUEUE(&smsg_work_queue);
    gasnetc_GNIT_Log("smsg queue overflow");
  } else {
    /* anything else is a fatal error */
    gasnetc_GNIT_Abort("CqGetEvent(smsg_cq) returns error %s", gni_return_string(status));
  }
}

void gasnetc_poll_smsg_queue(void)
{
  int i;
  gasnetc_poll_smsg_completion_queue();

  /* Now see about processing some peers off the smsg_work_queue */
  for (i = 0; i < SMSG_PEER_BURST; i += 1) {
    peer_struct_t *peer = gasnetc_work_dequeue();
    if (peer == NULL) break;
    gasnetc_process_smsg_q(peer->rank);
  }
}

#if GASNETC_SMSG_RETRANSMIT

static gasneti_lifo_head_t gasnetc_smsg_buffers = GASNETI_LIFO_INITIALIZER;

/* For exit code, here for use in gasnetc_free_smsg */
static gasneti_weakatomic_t shutdown_smsg_counter = gasneti_weakatomic_init(0);

GASNETI_INLINE(gasnetc_smsg_buffer)
void * gasnetc_smsg_buffer(size_t buffer_len) {
  void *result = gasneti_lifo_pop(&gasnetc_smsg_buffers);
  return result ? result : gasneti_malloc(GASNETC_MSG_MAXSIZE); /* XXX: less? */
}

GASNETI_INLINE(gasnetc_free_smsg)
void gasnetc_free_smsg(uint32_t msgid)
{
  gasnetc_post_descriptor_t * const gpd = msgid + (gasnetc_post_descriptor_t *) gasnetc_pd_buffers.addr;
  gasnetc_smsg_t * const smsg = &gpd->u.smsg;
  if (smsg->buffer) {
    gasneti_lifo_push(&gasnetc_smsg_buffers, smsg->buffer);
  } else if_pf (smsg->smsg_header.header.command == GC_CMD_SYS_SHUTDOWN_REQUEST) {
    gasneti_weakatomic_increment(&shutdown_smsg_counter, GASNETI_ATOMIC_NONE);
  }
  gasnetc_free_post_descriptor(gpd);
}

gasnetc_smsg_t *gasnetc_alloc_smsg(void)
{
  gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
  gasnetc_smsg_t * const result = &gpd->u.smsg;
  /* TODO: allocate space in the gpd for a persistent msgid? (avoids integer division here) */
  result->msgid = gpd - (gasnetc_post_descriptor_t *) gasnetc_pd_buffers.addr;
  return result;
}

#endif /* GASNETC_SMSG_RETRANSMIT*/


int
gasnetc_send_smsg(gasnet_node_t dest, 
                  gasnetc_smsg_t *smsg, int header_length, 
                  void *data, int data_length, int do_copy)
{
  void * const header = &smsg->smsg_header;
  gni_return_t status;
  const int max_trials = 4;
  int trial = 0;

#if GASNETC_SMSG_RETRANSMIT
  const uint32_t msgid = smsg->msgid;
  smsg->buffer = !do_copy ? NULL :
    (data = data_length ? memcpy(gasnetc_smsg_buffer(data_length), data, data_length) : NULL);
#else
  const uint32_t msgid = 0;
#endif

  gasneti_assert(!node_is_local(dest));

  GASNETI_TRACE_PRINTF(A, ("smsg s from %d to %d type %s\n", gasneti_mynode, dest, gasnetc_type_string(((GC_Header_t *) header)->command)));

  for (;;) {
    GASNETC_LOCK_GNI();
    status = GNI_SmsgSend(peer_data[dest].ep_handle,
                          header, header_length,
                          data, data_length, msgid);
    GASNETC_UNLOCK_GNI();

    if_pt (status == GNI_RC_SUCCESS) {
      if (trial) GASNETC_STAT_EVENT_VAL(SMSG_SEND_RETRY, trial);
      return GNI_RC_SUCCESS;
    }

    if (status != GNI_RC_NOT_DONE) {
      gasnetc_GNIT_Abort("GNI_SmsgSend returned error %s\n", gni_return_string(status));
    }

    if_pf (++trial == max_trials) {
      return GASNET_ERR_RESOURCE;
    }

    /* XXX: GNI_RC_NOT_DONE should NOT happen due to our flow control and Cq credits.
       However, it DOES rarely happen, expecially when using all the cores.
       Of course the fewer credits we allocate the more likely it is.
       So, we retry a finite number of times.  -PHH 2012.05.12
       TODO: Determine why/how we see NOT_DONE.
     */
    GASNETI_WAITHOOK();
    gasnetc_poll_local_queue();
  }
  return(GASNET_OK);
}


GASNETI_NEVER_INLINE(gasnetc_poll_local_queue,
void gasnetc_poll_local_queue(void))
{
  gni_return_t status;
  gni_cq_entry_t event_data;
  int i;
  gni_post_descriptor_t *pd;
  gasnetc_post_descriptor_t *gpd;

  GASNETC_LOCK_GNI(); /* TODO: can/should we reduce length of this critical section? */
  for (i = 0; i < gasnetc_poll_burst; i += 1) {
    /* Poll the bound_ep completion queue */
    status = GNI_CqGetEvent(bound_cq_handle,&event_data);
    if_pt (status == GNI_RC_SUCCESS) {
      gasneti_assert(!GNI_CQ_OVERRUN(event_data));

#if GASNETC_SMSG_RETRANSMIT
      if (GNI_CQ_GET_TYPE(event_data) == GNI_CQ_EVENT_TYPE_SMSG) {
        gasnetc_free_smsg(GNI_CQ_GET_MSG_ID(event_data));
	continue;
      }
#endif

      status = GNI_GetCompleted(bound_cq_handle, event_data, &pd);
      if (status != GNI_RC_SUCCESS)
	gasnetc_GNIT_Abort("GetCompleted(%p) failed %s\n",
		   (void *) event_data, gni_return_string(status));
      gpd = gasnetc_get_struct_addr_from_field_addr(gasnetc_post_descriptor_t, pd, pd);

      /* handle remaining work */
      if (gpd->flags & GC_POST_SEND) {
#if GASNETC_SMSG_RETRANSMIT
        gasnetc_smsg_t *smsg = gpd->u.smsg_p;
        const uint32_t msgid = smsg->msgid;
#else
        gasnetc_smsg_t *smsg = &gpd->u.smsg;
        const uint32_t msgid = 0;
#endif
        gasnetc_am_long_packet_t *galp = &smsg->smsg_header.galp;
        const size_t header_length = GASNETC_HEADLEN(long, galp->header.numargs);
#if GASNETC_SMSG_RETRANSMIT
        smsg->buffer = NULL;
#endif
        /* TODO: Use retry loop? */
        status = GNI_SmsgSend(peer_data[gpd->dest].ep_handle,
                              &smsg->smsg_header, header_length,
                              NULL, 0,  msgid);
        gasneti_assert_always (status == GNI_RC_SUCCESS);
      } else if (gpd->flags & GC_POST_COPY) {
        void * const buffer = gpd->bounce_buffer;
        size_t length = gpd->pd.length - (gpd->flags & GC_POST_COPY_TRIM);
	memcpy(gpd->get_target, gpd->bounce_buffer, length);
	gpd->bounce_buffer = (void*)(~3 & (uintptr_t)buffer); /* fixup for possible UNBOUNCE */
      }

      /* indicate completion */
      if (gpd->flags & GC_POST_COMPLETION_FLAG) {
	gasneti_weakatomic_set(gpd->completion.flag, 1, 0);
      } else if(gpd->flags & GC_POST_COMPLETION_OP) {
	gasnete_op_markdone(gpd->completion.op, (gpd->flags & GC_POST_GET) != 0);
      }

      /* release resources */
      if (gpd->flags & GC_POST_UNREGISTER) {
	status = GNI_MemDeregister(nic_handle, &gpd->pd.local_mem_hndl);
	gasneti_assert_always (status == GNI_RC_SUCCESS);
	gasnetc_return_reg_credit();
      } else if (gpd->flags & GC_POST_UNBOUNCE) {
	gasnetc_free_bounce_buffer(gpd->bounce_buffer);
      }
      if (!(gpd->flags & GC_POST_KEEP_GPD)) {
        gasnetc_free_post_descriptor(gpd);
      }
    } else if (status == GNI_RC_NOT_DONE) {
      break;
    } else {
      gasnetc_GNIT_Log("bound CqGetEvent %s\n", gni_return_string(status));
    }
  }
  GASNETC_UNLOCK_GNI();
}
  
void gasnetc_poll(void)
{
  gasnetc_poll_smsg_queue();
  gasnetc_poll_local_queue();
}

void gasnetc_send_am_nop(uint32_t pe)
{
  int rc;
#if GASNETC_SMSG_RETRANSMIT
  gasnetc_smsg_t *smsg = gasnetc_alloc_smsg();
  gasnetc_am_nop_packet_t *ganp = &smsg->smsg_header.ganp;
  ganp->header.command = GC_CMD_AM_NOP_REPLY;
 #if GASNET_DEBUG
  ganp->header.numargs = 0;
 #endif
#else
  static gasnetc_smsg_t m = { { {GC_CMD_AM_NOP_REPLY, } } };
  gasnetc_smsg_t * smsg = &m;
#endif
  rc = gasnetc_send_smsg(pe, smsg, sizeof(gasnetc_am_nop_packet_t), NULL, 0, 0);
  if_pf (rc) {
    gasnetc_GNIT_Abort("Failed to return AM credit\n");
  }
}


GASNETI_NEVER_INLINE(print_post_desc,
static void print_post_desc(const char *title, gni_post_descriptor_t *cmd)) {
  const int in_seg = gasneti_in_segment(gasneti_mynode, (void *) cmd->local_addr, cmd->length);
  printf("r %d %s-segment %s, desc addr %p\n", gasneti_mynode, (in_seg?"in":"non"), title, cmd);
  printf("r %d status: %ld\n", gasneti_mynode, cmd->status);
  printf("r %d cq_mode_complete: 0x%x\n", gasneti_mynode, cmd->cq_mode_complete);
  printf("r %d type: %d (%s)\n", gasneti_mynode, cmd->type, gasnetc_post_type_string(cmd->type));
  printf("r %d cq_mode: 0x%x\n", gasneti_mynode, cmd->cq_mode);
  printf("r %d dlvr_mode: 0x%x\n", gasneti_mynode, cmd->dlvr_mode);
  printf("r %d local_address: %p(0x%lx, 0x%lx)\n", gasneti_mynode, (void *) cmd->local_addr, 
	 cmd->local_mem_hndl.qword1, cmd->local_mem_hndl.qword2);
  printf("r %d remote_address: %p(0x%lx, 0x%lx)\n", gasneti_mynode, (void *) cmd->remote_addr, 
	 cmd->remote_mem_hndl.qword1, cmd->remote_mem_hndl.qword2);
  printf("r %d length: 0x%lx\n", gasneti_mynode, cmd->length);
  printf("r %d rdma_mode: 0x%x\n", gasneti_mynode, cmd->rdma_mode);
  printf("r %d src_cq_hndl: %p\n", gasneti_mynode, cmd->src_cq_hndl);
  printf("r %d sync: (0x%lx,0x%lx)\n", gasneti_mynode, cmd->sync_flag_value, cmd->sync_flag_addr);
  printf("r %d amo_cmd: %d\n", gasneti_mynode, cmd->amo_cmd);
  printf("r %d amo: 0x%lx, 0x%lx\n", gasneti_mynode, cmd->first_operand, cmd->second_operand);
  printf("r %d cqwrite_value: 0x%lx\n", gasneti_mynode, cmd->cqwrite_value);
}

static gni_return_t myPostRdma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  const int max_trials = 1000;
  int trial = 0;

  do {
      GASNETC_LOCK_GNI();
      status = GNI_PostRdma(ep, pd);
      GASNETC_UNLOCK_GNI();
      if_pt (status == GNI_RC_SUCCESS) {
        if (trial) GASNETC_STAT_EVENT_VAL(POST_RDMA_RETRY, trial);
        return GNI_RC_SUCCESS;
      }
      if (status != GNI_RC_ERROR_RESOURCE) break; /* Fatal */
      GASNETI_WAITHOOK();
      gasnetc_poll_local_queue();
  } while (++trial < max_trials);
  if (status == GNI_RC_ERROR_RESOURCE) {
    gasnetc_GNIT_Log("PostRdma retry failed");
  }
  return status;
}

static gni_return_t myPostFma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  const int max_trials = 1000;
  int trial = 0;

  do {
      GASNETC_LOCK_GNI();
      status = GNI_PostFma(ep, pd);
      GASNETC_UNLOCK_GNI();
      if_pt (status == GNI_RC_SUCCESS) {
        if (trial) GASNETC_STAT_EVENT_VAL(POST_FMA_RETRY, trial);
        return GNI_RC_SUCCESS;
      }
      if (status != GNI_RC_ERROR_RESOURCE) break; /* Fatal */
      GASNETI_WAITHOOK();
      gasnetc_poll_local_queue();
  } while (++trial < max_trials);
  if (status == GNI_RC_ERROR_RESOURCE) {
    gasnetc_GNIT_Log("PostFma retry failed");
  }
  return status;
}

/* Register local side of a pd, with bounded retry */
static gni_return_t myRegisterPd(gni_post_descriptor_t *pd)
{
  const uint64_t addr = pd->local_addr;
  const size_t nbytes = pd->length;
  const int max_trials = 10;
  int trial = 0;
  gni_return_t status;

  if_pf (!gasnetc_alloc_reg_credit()) {
    /* We may simple not have polled the Cq recently.
       So, WAITHOOK and STALL tracing only if still nothing after first poll */
    GASNETC_TRACE_WAIT_BEGIN();
    int stall = 0;
    goto first;
    do {
      GASNETI_WAITHOOK();
      stall = 1;
first:
      gasnetc_poll_local_queue();
    } while (!gasnetc_alloc_reg_credit());
    if_pf (stall) GASNETC_TRACE_WAIT_END(MEM_REG_STALL);
  }

  do {
    GASNETC_LOCK_GNI();
    status = GNI_MemRegister(nic_handle, addr, nbytes, NULL,
                             gasnetc_memreg_flags, -1, &pd->local_mem_hndl);
    GASNETC_UNLOCK_GNI();
    if_pt (status == GNI_RC_SUCCESS) {
      if (trial) GASNETC_STAT_EVENT_VAL(MEM_REG_RETRY, trial);
      return GNI_RC_SUCCESS;
    }
    if (status != GNI_RC_ERROR_RESOURCE) break; /* Fatal */
    GASNETI_WAITHOOK();
  } while (++trial < max_trials);
  if (status == GNI_RC_ERROR_RESOURCE) {
    gasnetc_GNIT_Log("MemRegister retry failed");
  }
  return status;
}

/* Perform an rdma/fma Put with no concern for local completion */
void gasnetc_rdma_put_bulk(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t *pd;
  gni_return_t status;

  gasneti_assert(!node_is_local(dest));

  pd = &gpd->pd;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;

  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, dest_addr, nbytes);

  if_pf (!gasneti_in_segment(gasneti_mynode, source_addr, nbytes)) {
    /* source not (entirely) in segment */
    /* if (nbytes <= gasnetc_bounce_register_cutover)  then use bounce buffer
     * else mem-register
     */
    /* first deal with the memory copy and bounce buffer assignment */
    if (nbytes <= GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
      gpd->bounce_buffer = gpd->u.immediate;
      memcpy(gpd->bounce_buffer, source_addr, nbytes);
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = my_mem_handle;
    } else if (nbytes <= gasnetc_bounce_register_cutover) {
      gpd->flags |= GC_POST_UNBOUNCE;
      gpd->bounce_buffer = gasnetc_alloc_bounce_buffer();
      memcpy(gpd->bounce_buffer, source_addr, nbytes);
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = my_mem_handle;
    } else {
      gpd->flags |= GC_POST_UNREGISTER;
      pd->local_addr = (uint64_t) source_addr;
      status = myRegisterPd(pd);
      gasneti_assert_always (status == GNI_RC_SUCCESS);
    }
  } else {
    pd->local_addr = (uint64_t) source_addr;
    pd->local_mem_hndl = my_mem_handle;
  }

  /* now initiate the transfer according to fma/rdma cutover */
  /*  TODO: distnict Put and Get cut-overs */
  if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_PUT;
#if FIX_HT_ORDERING
      pd->cq_mode = gasnetc_fma_put_cq_mode;
#endif
      status = myPostFma(peer->ep_handle, pd);
  } else {
      pd->type = GNI_POST_RDMA_PUT;
      status = myPostRdma(peer->ep_handle, pd);
  }

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Put_bulk", pd);
    gasnetc_GNIT_Abort("Put_bulk failed with %s\n", gni_return_string(status));
  }
}

/* Perform an rdma/fma Put which favors rapid local completion
 * The return value is boolean, where 1 means locally complete.
 */
int gasnetc_rdma_put(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t *pd;
  gni_return_t status;
  int result = 1; /* assume local completion */

  gasneti_assert(!node_is_local(dest));

  pd = &gpd->pd;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;

  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, dest_addr, nbytes);

  /* first deal with the memory copy and bounce buffer assignment */
  if (nbytes <= GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
    gpd->bounce_buffer = gpd->u.immediate;
    memcpy(gpd->bounce_buffer, source_addr, nbytes);
    pd->local_addr = (uint64_t) gpd->bounce_buffer;
    pd->local_mem_hndl = my_mem_handle;
  } else if (nbytes <= gasnetc_bounce_register_cutover) {
    gpd->flags |= GC_POST_UNBOUNCE;
    gpd->bounce_buffer = gasnetc_alloc_bounce_buffer();
    memcpy(gpd->bounce_buffer, source_addr, nbytes);
    pd->local_addr = (uint64_t) gpd->bounce_buffer;
    pd->local_mem_hndl = my_mem_handle;
  } else if_pf (!gasneti_in_segment(gasneti_mynode, source_addr, nbytes)) {
    /* source not in segment */
    gpd->flags |= GC_POST_UNREGISTER;
    pd->local_addr = (uint64_t) source_addr;
    status = myRegisterPd(pd);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
    result = 0; /* FMA may override */
  } else {
    pd->local_addr = (uint64_t) source_addr;
    pd->local_mem_hndl = my_mem_handle;
    result = 0; /* FMA may override */
  }

  /* now initiate the transfer according to fma/rdma cutover */
  /*  TODO: distnict Put and Get cut-overs */
  if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_PUT;
#if FIX_HT_ORDERING
      pd->cq_mode = gasnetc_fma_put_cq_mode;
#endif
      status = myPostFma(peer->ep_handle, pd);
#if GASNET_CONDUIT_GEMINI
    /* On Gemini (only) return from PostFma follows local completion */
    result = 1; /* even if was zeroed by choice of zero-copy */
#endif
  } else {
      pd->type = GNI_POST_RDMA_PUT;
      status = myPostRdma(peer->ep_handle, pd);
  }

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Put", pd);
    gasnetc_GNIT_Abort("Put failed with %s\n", gni_return_string(status));
  }

  gasneti_assert((result == 0) || (result == 1)); /* ensures caller can use "&=" or "+=" */
  return result;
}

/* Put from the bounce buffer indicated in the gpd */
void gasnetc_rdma_put_buff(gasnet_node_t dest, void *dest_addr,
		size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t *pd;
  gni_return_t status;

  gasneti_assert(!node_is_local(dest));
  gasneti_assert(nbytes  <= GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE);

  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, dest_addr, nbytes);

  pd = &gpd->pd;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;
  pd->local_addr = (uint64_t) gpd->bounce_buffer;
  pd->local_mem_hndl = my_mem_handle;

  /* now initiate - *always* FMA for now */
  pd->type = GNI_POST_FMA_PUT;
#if FIX_HT_ORDERING
  pd->cq_mode = gasnetc_fma_put_cq_mode;
#endif
  status = myPostFma(peer->ep_handle, pd);

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("PutBuff", pd);
    gasnetc_GNIT_Abort("PutBuff failed with %s\n", gni_return_string(status));
  }
}

/* for get, source_addr is remote */
void gasnetc_rdma_get(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t *pd;
  gni_return_t status;

  gasneti_assert(!node_is_local(dest));

  pd = &gpd->pd;
  gpd->flags |= GC_POST_GET;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) source_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;

  /* confirm that the source is in-segment on the far end */
  gasneti_boundscheck(dest, source_addr, nbytes);

  /* check where the local addr is */
  if_pf (!gasneti_in_segment(gasneti_mynode, dest_addr, nbytes)) {
    /* dest not (entirely) in segment */
    /* if (nbytes <= gasnetc_bounce_register_cutover)  then use bounce buffer
     * else mem-register
     */
    if (nbytes < GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
      gpd->flags |= GC_POST_COPY;
      gpd->bounce_buffer = gpd->u.immediate;
      gpd->get_target = dest_addr;
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = my_mem_handle;
    } else if (nbytes <= gasnetc_bounce_register_cutover) {
      gpd->flags |= GC_POST_UNBOUNCE | GC_POST_COPY;
      gpd->bounce_buffer = gasnetc_alloc_bounce_buffer();
      gpd->get_target = dest_addr;
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = my_mem_handle;
    } else {
      gpd->flags |= GC_POST_UNREGISTER;
      pd->local_addr = (uint64_t) dest_addr;
      status = myRegisterPd(pd);
      gasneti_assert_always (status == GNI_RC_SUCCESS);
    }
  } else {
    pd->local_addr = (uint64_t) dest_addr;
    pd->local_mem_hndl = my_mem_handle;
  }

  /* now initiate the transfer according to fma/rdma cutover */
  /*  TODO: distnict Put and Get cut-overs */
  if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_GET;
      status = myPostFma(peer->ep_handle, pd);
  } else {
      pd->type = GNI_POST_RDMA_GET;
      status = myPostRdma(peer->ep_handle, pd);
  }

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Get", pd);
    gasnetc_GNIT_Abort("Get failed with %s\n", gni_return_string(status));
  }
}

/* for get in which one or more of dest_addr, source_addr or nbytes is NOT divisible by 4 */
void gasnetc_rdma_get_unaligned(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t *pd;
  gni_return_t status;
  char * buffer;

  /* Compute length of "overfetch" required, if any */
  unsigned int pre = (uintptr_t) source_addr & 3;
  size_t       length = GASNETI_ALIGNUP(nbytes + pre, 4);
  unsigned int overfetch = length - nbytes;

  gasneti_assert(!node_is_local(dest));

  pd = &gpd->pd;
  gasneti_assert(0 == (overfetch & ~GC_POST_COPY_TRIM));
  gpd->flags |= GC_POST_GET | GC_POST_COPY | overfetch;
  gpd->get_target = dest_addr;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) source_addr - pre;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = length;
  pd->local_mem_hndl = my_mem_handle;

  /* confirm that the source is in-segment on the far end */
  gasneti_boundscheck(dest, source_addr, nbytes);

  /* must always use immediate or bounce buffer */
  if (length < GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
    buffer = gpd->u.immediate;
  } else if (length <= gasnetc_bounce_register_cutover) {
    gpd->flags |= GC_POST_UNBOUNCE;
    buffer = gasnetc_alloc_bounce_buffer();
  } else {
    gasneti_fatalerror("get_unaligned called with nbytes too large for bounce buffers");
  }

  pd->local_addr = (uint64_t) buffer;
  gpd->bounce_buffer = buffer + pre;

  /* now initiate the transfer according to fma/rdma cutover */
  /*  TODO: distnict Put and Get cut-overs */
  if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_GET;
      status = myPostFma(peer->ep_handle, pd);
  } else {
      pd->type = GNI_POST_RDMA_GET;
      status = myPostRdma(peer->ep_handle, pd);
  }

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Get", pd);
    gasnetc_GNIT_Abort("Get unaligned failed with %s\n", gni_return_string(status));
  }
}

/* Get into bounce_buffer indicated in the gpd
   Will overfetch if source_addr or nbytes are not 4-byte aligned
   but expects (does not check) that the buffer is aligned.
   Caller must be allow for space (upto 6 bytes) for the overfetch.
   Returns offset to start of data after adjustment for overfetch
 */
int gasnetc_rdma_get_buff(gasnet_node_t dest, void *source_addr,
		size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t *pd;
  gni_return_t status;

  /* Compute length of "overfetch" required, if any */
  unsigned int pre = (uintptr_t) source_addr & 3;
  size_t       length = GASNETI_ALIGNUP(nbytes + pre, 4);
  unsigned int overfetch = length - nbytes;

  gasneti_assert(!node_is_local(dest));
  gasneti_assert(nbytes  <= GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE);

  /* confirm that the source is in-segment on the far end */
  gasneti_boundscheck(dest, source_addr, nbytes);

  pd = &gpd->pd;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) source_addr - pre;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = length;
  pd->local_addr = (uint64_t) gpd->bounce_buffer;
  pd->local_mem_hndl = my_mem_handle;

  /* now initiate - *always* FMA for now */
  pd->type = GNI_POST_FMA_GET;
  status = myPostFma(peer->ep_handle, pd);

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("GetBuff", pd);
    gasnetc_GNIT_Abort("GetBuff failed with %s\n", gni_return_string(status));
  }

  return pre;
}


void gasnetc_get_am_credit(uint32_t pe)
{
  gasneti_weakatomic_t *p = &peer_data[pe].am_credit;
#if GASNETC_DEBUG
  fprintf(stderr, "r %d get am credit for %d, before is %d\n",
	 gasneti_mynode, pe, (int)gasneti_weakatomic_read(&peer_data[pe].am_credit, 0));
#endif
  if_pf (!gasnetc_weakatomic_dec_if_positive(p)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasneti_AMPoll();
    } while (!gasnetc_weakatomic_dec_if_positive(p));
    GASNETC_TRACE_WAIT_END(GET_AM_CREDIT_STALL);
  }
}

void gasnetc_return_am_credit(uint32_t pe)
{
  gasneti_weakatomic_increment(&peer_data[pe].am_credit, GASNETI_ATOMIC_NONE);
#if GASNETC_DEBUG
  fprintf(stderr, "r %d return am credit for %d, after is %d\n",
	 gasneti_mynode, pe, (int)gasneti_weakatomic_read(&peer_data[pe].am_credit, 0));
#endif
}

static gasneti_lifo_head_t post_descriptor_pool = GASNETI_LIFO_INITIALIZER;

/* Needs no lock because it is called only from the init code */
void gasnetc_init_post_descriptor_pool(void)
{
  int i;
  gasnetc_post_descriptor_t *data = gasnetc_pd_buffers.addr;
  gasneti_assert_always(data);
  memset(data, 0, gasnetc_pd_buffers.size); /* Just in case */
  for (i = 0; i < (gasnetc_pd_buffers.size / sizeof(gasnetc_post_descriptor_t)); i += 1) {
    gasneti_lifo_push(&post_descriptor_pool, &data[i]);
  }
}

/* This needs no lock because there is an internal lock in the queue */
gasnetc_post_descriptor_t *gasnetc_alloc_post_descriptor(void)
{
  gasnetc_post_descriptor_t *gpd =
            (gasnetc_post_descriptor_t *) gasneti_lifo_pop(&post_descriptor_pool);
  if_pf (!gpd) {
    /* We may simple not have polled the Cq recently.
       So, WAITHOOK and STALL tracing only if still nothing after first poll */
    GASNETC_TRACE_WAIT_BEGIN();
    int stall = 0;
    goto first;
    do {
      GASNETI_WAITHOOK();
      stall = 1;
first:
      gasnetc_poll_local_queue();
      gpd = (gasnetc_post_descriptor_t *) gasneti_lifo_pop(&post_descriptor_pool);
    } while (!gpd);
    if_pf (stall) GASNETC_TRACE_WAIT_END(ALLOC_PD_STALL);
  }
  return(gpd);
}


/* This needs no lock because there is an internal lock in the queue */
/* LCS inline this */
void gasnetc_free_post_descriptor(gasnetc_post_descriptor_t *gpd)
{
  gasneti_lifo_push(&post_descriptor_pool, gpd);
}

/* exit related */
volatile int gasnetc_shutdownInProgress = 0;
double gasnetc_shutdown_seconds = 0.0;
static double shutdown_max = 120.;  /* 2 minutes */
static uint32_t sys_exit_rcvd = 0;

#if GASNET_PSHM
gasnetc_exitcode_t *gasnetc_exitcodes = NULL;
#endif

/* XXX: probably need to obtain am_credit or otherwise guard against
   the possibility of GNI_SmsgSend() returning GNI_RC_NOT_DONE. */
extern void gasnetc_sys_SendShutdownMsg(gasnet_node_t peeridx, int shift, int exitcode)
{
#if GASNETC_SMSG_RETRANSMIT
  gasnetc_smsg_t *smsg;
#else
  static gasnetc_smsg_t shutdown_smsg;
  gasnetc_smsg_t *smsg = &shutdown_smsg;
#endif
#if GASNET_PSHM
  const gasnet_node_t dest = gasneti_pshm_firsts[peeridx];
#else
  const gasnet_node_t dest = peeridx;
#endif
  gasnetc_sys_shutdown_packet_t *gssp;
  int result;

#if GASNETC_SMSG_RETRANSMIT
  if (0 == gasnetc_pd_buffers.addr) {
    /* Exit before attach, so must populate gdp freelist */
    const int count = 32; /* XXX: any reason to economize? */
    gasnetc_pd_buffers.addr = gasneti_calloc(count, sizeof(gasnetc_post_descriptor_t));
    gasnetc_pd_buffers.size = count * sizeof(gasnetc_post_descriptor_t);
    gasnetc_init_post_descriptor_pool();
  }
  smsg = gasnetc_alloc_smsg();
#endif

  GASNETI_TRACE_PRINTF(C,("Send SHUTDOWN Request to node %d w/ shift %d, exitcode %d",dest,shift,exitcode));
  gssp = &smsg->smsg_header.gssp;
  gssp->header.command = GC_CMD_SYS_SHUTDOWN_REQUEST;
  gssp->header.misc    = exitcode; /* only 15 bits, but exit() only preserves low 8-bits anyway */
#if GASNET_DEBUG
  gssp->header.numargs = 0;
#endif
  gssp->header.handler = shift; /* log(distance) */
  result = gasnetc_send_smsg(dest, smsg, sizeof(gasnetc_sys_shutdown_packet_t), NULL, 0, 0);
#if GASNET_DEBUG
  if_pf (result) {
    fprintf(stderr, "WARNING: gasnetc_send_smsg() call at Shutdown failed on node %i\n", (int)gasneti_mynode);
  }
#endif
}


/* this is called from poll when a shutdown packet arrives */
void gasnetc_handle_sys_shutdown_packet(uint32_t source, gasnetc_sys_shutdown_packet_t *sys)
{
  uint32_t distance = 1 << sys->header.handler;
  uint8_t exitcode = sys->header.misc;
  uint8_t oldcode;
#if GASNET_DEBUG || GASNETI_STATS_OR_TRACE
  int sender = source;
  GASNETI_TRACE_PRINTF(C,("Got SHUTDOWN Request from node %d w/ exitcode %d",sender,exitcode));
#endif
  oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
  if (exitcode > oldcode) {
    gasneti_atomic_set((gasneti_atomic_t *) &gasnetc_exitcode, exitcode, 0);
  } else {
    exitcode = oldcode;
  }
  sys_exit_rcvd |= distance;
  if (!gasnetc_shutdownInProgress) {
    gasneti_sighandlerfn_t handler = gasneti_reghandler(SIGQUIT, SIG_IGN);
    if ((handler != gasneti_defaultSignalHandler) &&
#ifdef SIG_HOLD
	(handler != (gasneti_sighandlerfn_t)SIG_HOLD) &&
#endif
	(handler != (gasneti_sighandlerfn_t)SIG_ERR) &&
	(handler != (gasneti_sighandlerfn_t)SIG_IGN) &&
	(handler != (gasneti_sighandlerfn_t)SIG_DFL)) {
      (void)gasneti_reghandler(SIGQUIT, handler);
      raise(SIGQUIT);
    }
    if (!gasnetc_shutdownInProgress) gasnetc_exit(exitcode);
  }
}


/* Reduction (op=MAX) over exitcodes using dissemination pattern.
   Returns 0 on sucess, or non-zero on error (timeout).
 */
extern int gasnetc_sys_exit(int *exitcode_p)
{
#if GASNET_PSHM                  
  const gasnet_node_t size = gasneti_nodemap_global_count;
  const gasnet_node_t rank = gasneti_nodemap_global_rank;
#else
  const gasnet_node_t size = gasneti_nodes;
  const gasnet_node_t rank = gasneti_mynode;
#endif
  uint32_t goal = 0;
  uint32_t distance;
  int result = 0; /* success */
  int exitcode = *exitcode_p;
  int oldcode;
  int shift;
  gasneti_tick_t timeout_us = 1e6 * gasnetc_shutdown_seconds;
  gasneti_tick_t starttime = gasneti_ticks_now();

  GASNETI_TRACE_PRINTF(C,("Entering SYS EXIT"));

#if GASNET_PSHM
  if (gasneti_nodemap_local_rank) {
    gasnetc_exitcode_t * const self = &gasnetc_exitcodes[gasneti_nodemap_local_rank];
    gasnetc_exitcode_t * const lead = &gasnetc_exitcodes[0];

    /* publish our exit code for leader to read */
    self->exitcode = exitcode;
    gasneti_local_wmb(); /* Release */
    self->present = 1;

    /* wait for leader to publish final result */
    while (! lead->present) {
      gasnetc_poll();
      if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
        result = 1; /* failure */
        goto out;
      }
    }
    gasneti_local_rmb(); /* Acquire */
    exitcode = lead->exitcode;

    goto out;
  } else {
    gasnetc_exitcode_t * const self = &gasnetc_exitcodes[0];
    int i;

    /* collect exit codes */
    for (i = 1; i < gasneti_nodemap_local_count; ++i) {
      gasnetc_exitcode_t * const peer = &gasnetc_exitcodes[i];

      while (! peer->present) {
        gasnetc_poll();
        if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
          result = 1; /* failure */
          goto out;
        }
      }
      gasneti_local_rmb(); /* Acquire */
      exitcode = MAX(exitcode, peer->exitcode);
    }
  }
#endif

  for (distance = 1, shift = 0; distance < size; distance *= 2, ++shift) {
    gasnet_node_t peeridx = (distance >= size - rank) ? rank - (size - distance)
                                                      : rank + distance;

    oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
    exitcode = MAX(exitcode, oldcode);

    gasnetc_sys_SendShutdownMsg(peeridx, shift, exitcode);

    /* wait for completion of the proper receive, which might arrive out of order */
    goal |= distance;
    while ((sys_exit_rcvd & goal) != goal) {
      gasnetc_poll();
      if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
        result = 1; /* failure */
        goto out;
      }
    }
  }

#if GASNET_PSHM
  { /* publish final exit code */
    gasnetc_exitcode_t * const self = &gasnetc_exitcodes[0];
    self->exitcode = exitcode;
    gasneti_local_wmb(); /* Release */
    self->present = 1;
  }
#endif

  #if GASNETC_SMSG_RETRANSMIT
  /* drain send completion events to avoid NOT_DONE at unbind: */
    while (gasneti_weakatomic_read(&shutdown_smsg_counter, GASNETI_ATOMIC_NONE) != shift) {
      gasnetc_poll_local_queue();
      if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
        result = 1; /* failure */
        goto out;
      }
    }
  #endif
    
out:
  oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
  *exitcode_p = MAX(exitcode, oldcode);

  return result;
}






/* AuxSeg setup for registered bounce buffer space*/
GASNETI_IDENT(gasneti_bounce_auxseg_IdentString,
              "$GASNetAuxSeg_bounce: GASNETC_GNI_BOUNCE_SIZE:" _STRINGIFY(GASNETC_GNI_BOUNCE_SIZE_DEFAULT)" $");
gasneti_auxseg_request_t gasnetc_bounce_auxseg_alloc(gasnet_seginfo_t *auxseg_info) {
  gasneti_auxseg_request_t retval;
  
  retval.minsz = gasneti_getenv_int_withdefault("GASNETC_GNI_MIN_BOUNCE_SIZE",
                                                GASNETC_GNI_MIN_BOUNCE_SIZE_DEFAULT,1);
  retval.optimalsz = gasneti_getenv_int_withdefault("GASNETC_GNI_BOUNCE_SIZE",
                                                    GASNETC_GNI_BOUNCE_SIZE_DEFAULT,1);
  if (retval.optimalsz < retval.minsz) retval.optimalsz = retval.minsz;
#if GASNET_DEBUG_VERBOSE
  fprintf(stderr, "auxseg asking for min  %ld opt %ld\n", retval.minsz, retval.optimalsz);
#endif
  if (auxseg_info == NULL){
    return retval; /* initial query */
  }	
  else { /* auxseg granted */
    /* The only one we care about is our own node */
    gasnetc_bounce_buffers = auxseg_info[gasneti_mynode];
#if GASNET_DEBUG_VERBOSE
    fprintf(stderr, "got auxseg %p size %ld\n", gasnetc_bounce_buffers.addr,
	    gasnetc_bounce_buffers.size);
#endif
  }

  return retval;
}

/* AuxSeg setup for registered post descriptors*/
/* This ident string is used by upcrun (and potentially by other tools) to estimate
 * the auxseg requirements, and gets rounded up.
 * So, this doesn't need to be an exact value.
 * As of 2013.02.14 I have systems with
 *     Gemini = 328 bytes
 *     Aries  = 344 bytes
 */
#if GASNET_CONDUIT_GEMINI
  #define GASNETC_SIZEOF_GDP 328
#else
  #define GASNETC_SIZEOF_GDP 344
#endif
GASNETI_IDENT(gasneti_pd_auxseg_IdentString, /* XXX: update if gasnetc_post_descriptor_t changes */
              "$GASNetAuxSeg_pd: " _STRINGIFY(GASNETC_SIZEOF_GDP) "*"
              "(GASNETC_GNI_NUM_PD:" _STRINGIFY(GASNETC_GNI_NUM_PD_DEFAULT) ") $");
gasneti_auxseg_request_t gasnetc_pd_auxseg_alloc(gasnet_seginfo_t *auxseg_info) {
  gasneti_auxseg_request_t retval;
  
  retval.minsz = gasneti_getenv_int_withdefault("GASNETC_GNI_MIN_NUM_PD",
                                                GASNETC_GNI_MIN_NUM_PD_DEFAULT,1)
    * sizeof(gasnetc_post_descriptor_t);
  retval.optimalsz = gasneti_getenv_int_withdefault("GASNETC_GNI_NUM_PD",
                                                    GASNETC_GNI_NUM_PD_DEFAULT,1) 
    * sizeof(gasnetc_post_descriptor_t);
  if (retval.optimalsz < retval.minsz) retval.optimalsz = retval.minsz;
#if GASNET_DEBUG_VERBOSE
  fprintf(stderr, "auxseg post descriptor asking for min  %ld opt %ld\n", retval.minsz, retval.optimalsz);
#endif
  if (auxseg_info == NULL){
    return retval; /* initial query */
  }	
  else { /* auxseg granted */
    /* The only one we care about is our own node */
    gasnetc_pd_buffers = auxseg_info[gasneti_mynode];
#if GASNET_DEBUG_VERBOSE
    fprintf(stderr, "got pd auxseg %p size %ld\n", gasnetc_pd_buffers.addr,
	    gasnetc_pd_buffers.size);
#endif
  }

  return retval;
}

gasneti_lifo_head_t gasnetc_bounce_buffer_pool = GASNETI_LIFO_INITIALIZER;

void gasnetc_init_bounce_buffer_pool(void)
{
  int i;
  int num_bounce;
  gasneti_assert_always(gasnetc_bounce_buffers.addr != NULL);
  gasneti_assert_always(gasnetc_bounce_buffers.size >= GASNETC_GNI_MIN_BOUNCE_SIZE_DEFAULT);
  gasnetc_bounce_register_cutover = 
    gasneti_getenv_int_withdefault("GASNETC_GNI_BOUNCE_REGISTER_CUTOVER",
				   GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_DEFAULT,1);
  if (gasnetc_bounce_register_cutover > GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_MAX)
    gasnetc_bounce_register_cutover = GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_MAX;
  if (gasnetc_bounce_register_cutover > gasnetc_bounce_buffers.size)
    gasnetc_bounce_register_cutover = gasnetc_bounce_buffers.size;
  num_bounce = gasnetc_bounce_buffers.size / gasnetc_bounce_register_cutover;
  for(i = 0; i < num_bounce; i += 1) {
    gasneti_lifo_push(&gasnetc_bounce_buffer_pool, (char *) gasnetc_bounce_buffers.addr + 
		      (gasnetc_bounce_register_cutover * i));
  }
}

void *gasnetc_alloc_bounce_buffer(void)
{
  void *buf = gasneti_lifo_pop(&gasnetc_bounce_buffer_pool);
  if_pf (!buf) {
    /* We may simple not have polled the Cq recently.
       So, WAITHOOK and STALL tracing only if still nothing after first poll */
    GASNETC_TRACE_WAIT_BEGIN();
    int stall = 0;
    goto first;
    do {
      GASNETI_WAITHOOK();
      stall = 1;
first:
      gasnetc_poll_local_queue();
      buf = gasneti_lifo_pop(&gasnetc_bounce_buffer_pool);
    } while (!buf);
    if_pf (stall) GASNETC_TRACE_WAIT_END(ALLOC_BB_STALL);
  }
  return(buf);
}

void gasnetc_free_bounce_buffer(void *gcb)
{
  gasneti_lifo_push(&gasnetc_bounce_buffer_pool, gcb);
}
