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

static uint32_t gasnetc_memreg_flags;
static int gasnetc_mem_consistency;

static unsigned int gasnetc_mb_maxcredit;

int gasnetc_poll_burst = 10;

static double shutdown_max;
static uint32_t sys_exit_rcvd;

typedef struct peer_struct {
  struct peer_struct *next; /* pointer to next when queue, GC_NOT_QUEUED otherwise */
  gasneti_weakatomic_t am_credit;
  gasnet_node_t rank;
} peer_struct_t;

static peer_struct_t *peer_data;

static gni_mem_handle_t my_smsg_handle;
static gni_mem_handle_t my_mem_handle;
static gni_mem_handle_t *peer_mem_handle;

static gni_cdm_handle_t cdm_handle;
static gni_nic_handle_t nic_handle;
static gni_ep_handle_t *bound_ep_handles;
static gni_cq_handle_t bound_cq_handle;
static gni_cq_handle_t smsg_cq_handle;
static gni_cq_handle_t destination_cq_handle;

static void *smsg_mmap_ptr;
static size_t smsg_mmap_bytes;

#if GASNETC_OPTIMIZE_LIMIT_CQ
static int  numpes_on_smp;
static int  max_outstanding_req;
static int  outstanding_req;
#endif

/* Limit the number of active dynamic memory registrations */
static gasneti_weakatomic_t reg_credit;

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

/* called after segment init. See gasneti_seginfo */
/* allgather the memory handles for the segments */
/* create endpoints */
void gasnetc_init_segment(void *segment_start, size_t segment_size)
{
  gni_return_t status;
  /* Map the shared segment */

  { int envval = gasneti_getenv_int_withdefault("GASNETC_GNI_MEMREG", GASNETC_GNI_MEMREG_DEFAULT, 0);
    if (envval < 1) envval = 1;
    gasneti_weakatomic_set(&reg_credit, envval, GASNETI_ATOMIC_NONE);
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
  
  peer_mem_handle = gasneti_malloc(gasneti_nodes * sizeof(gni_mem_handle_t));
  gasnetc_GNIT_Allgather(&my_mem_handle, sizeof(gni_mem_handle_t), peer_mem_handle);
}

uintptr_t gasnetc_init_messaging(void)
{
  gni_return_t status;
  gni_smsg_attr_t my_smsg_attr;
  gni_smsg_attr_t *all_smsg_attr;
  gni_smsg_type_t smsg_type;
  uint32_t *all_addr;
  uint32_t remote_addr;
  uint32_t local_address;
  uint32_t i;
  unsigned int bytes_per_mbox;
  unsigned int bytes_needed;
  int modes = 0;

#if GASNETC_DEBUG
  gasnetc_GNIT_Log("entering");
  modes |= GNI_CDM_MODE_ERR_NO_KILL;
#endif

  GASNETC_INITLOCK_GNI();

  gasnetc_work_queue_init();

  status = GNI_CdmCreate(gasneti_mynode, gasnetc_GNIT_Ptag(), gasnetc_GNIT_Cookie(), 
			 modes,
			 &cdm_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  status = GNI_CdmAttach(cdm_handle, gasnetc_GNIT_Device_Id(),
			 &local_address,
			 &nic_handle);

  gasneti_assert_always (status == GNI_RC_SUCCESS);

#if GASNETC_DEBUG
  gasnetc_GNIT_Log("cdmattach");
#endif

#if GASNETC_SMSG_RETRANSMIT
  smsg_type = GNI_SMSG_TYPE_MBOX_AUTO_RETRANSMIT;
  status = GNI_SmsgSetMaxRetrans(nic_handle, 1);
  gasneti_assert_always (status == GNI_RC_SUCCESS);
#else
  smsg_type = GNI_SMSG_TYPE_MBOX;
#endif
 
#if GASNETC_OPTIMIZE_LIMIT_CQ
  {
    int depth, cpu_count,cq_entries,multiplier;
    depth = gasneti_getenv_int_withdefault("GASNET_NETWORKDEPTH", GASNETC_NETWORKDEPTH_DEFAULT, 0);
    gasnetc_mb_maxcredit = 2 * MAX(1,depth); 
    numpes_on_smp = gasnetc_GNIT_numpes_on_smp();
    cpu_count = gasneti_cpu_count();
    multiplier = MAX(1,cpu_count/numpes_on_smp);  max_outstanding_req = multiplier*depth;
    outstanding_req = 0;  
    cq_entries = max_outstanding_req+2;
    status = GNI_CqCreate(nic_handle, cq_entries, 0, GNI_CQ_NOBLOCK, NULL, NULL, &bound_cq_handle);
  }
#else
  status = GNI_CqCreate(nic_handle, 1024, 0, GNI_CQ_NOBLOCK, NULL, NULL, &bound_cq_handle);
#endif

  gasneti_assert_always (status == GNI_RC_SUCCESS);


  bound_ep_handles = (gni_ep_handle_t *)gasneti_malloc(gasneti_nodes * sizeof(gni_ep_handle_t));

  all_addr = gasnetc_gather_nic_addresses();
  for (i=0; i<gasneti_nodes; i++) {
    status = GNI_EpCreate(nic_handle, bound_cq_handle, &bound_ep_handles[i]);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
    status = GNI_EpBind(bound_ep_handles[i], all_addr[i], i);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
#if GASNETC_DEBUG
    gasnetc_GNIT_Log("ep bound to %d, addr %d", i, all_addr[i]);
#endif
  }


  /* Initialize the short message system */
#if !GASNETC_OPTIMIZE_LIMIT_CQ
  { int tmp = gasneti_getenv_int_withdefault("GASNET_NETWORKDEPTH", GASNETC_NETWORKDEPTH_DEFAULT, 0);
    gasnetc_mb_maxcredit = 2 * MAX(1,tmp); /* silently "fix" zero or negative values */
  }
#endif

  /*
   * allocate a CQ in which to receive message notifications
   */
  /* TODO: is "* 2" still correct given gasnetc_mb_maxcredit has been halved since the original code? */
  status = GNI_CqCreate(nic_handle,gasneti_nodes * gasnetc_mb_maxcredit * 2,0,GNI_CQ_NOBLOCK,NULL,NULL,&smsg_cq_handle);
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("GNI_CqCreate returned error %s\n", gni_return_string(status));
  }
  
  /*
   * Set up an mmap region to contain all of my mailboxes.
   * The GNI_SmsgBufferSizeNeeded is used to determine how
   * much memory is needed for each mailbox.
   */

  my_smsg_attr.msg_type = smsg_type;
  my_smsg_attr.mbox_maxcredit = gasnetc_mb_maxcredit;
  my_smsg_attr.msg_maxsize = GASNETC_MSG_MAXSIZE;
#if GASNETC_DEBUG
  fprintf(stderr,"r %d maxcredit %d msg_maxsize %d\n", gasneti_mynode, gasnetc_mb_maxcredit, (int)GASNETC_MSG_MAXSIZE);
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
  bytes_needed = gasneti_nodes * bytes_per_mbox;
  
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

  /* exchange peer smsg data */
  
  my_smsg_attr.msg_type = smsg_type;
  my_smsg_attr.msg_buffer = smsg_mmap_ptr;
  my_smsg_attr.buff_size = bytes_per_mbox;
  my_smsg_attr.mbox_maxcredit = gasnetc_mb_maxcredit;
  my_smsg_attr.msg_maxsize = GASNETC_MSG_MAXSIZE;

  all_smsg_attr = gasneti_malloc(gasneti_nodes * sizeof(gni_smsg_attr_t));
  gasnetc_GNIT_Allgather(&my_smsg_attr, sizeof(gni_smsg_attr_t), all_smsg_attr);
  /* At this point all_smsg_attr has information for everyone */
  /* We need to patch up the smsg data, fixing the remote start addresses */
  for (i = 0; i < gasneti_nodes; i += 1) {
    all_smsg_attr[i].mbox_offset = bytes_per_mbox * gasneti_mynode;
    my_smsg_attr.mbox_offset = bytes_per_mbox * i;
    status = GNI_SmsgInit(bound_ep_handles[i], &my_smsg_attr, &all_smsg_attr[i]);
    if (status != GNI_RC_SUCCESS) {
      gasnetc_GNIT_Abort("GNI_SmsgInit returned error %s\n", gni_return_string(status));
    }
  }
  gasneti_free(all_smsg_attr);

  /* init peer_data */
  peer_data = gasneti_malloc(gasneti_nodes * sizeof(peer_struct_t));
  for (i = 0; i < gasneti_nodes; i += 1) {
    peer_data[i].next = GC_NOT_QUEUED;
    gasneti_weakatomic_set(&peer_data[i].am_credit, gasnetc_mb_maxcredit / 2, 0); /* (req + reply) = 2 */
    peer_data[i].rank = i;
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

  gasnetc_GNIT_Barrier();

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
  left = gasneti_nodes;
  while (left > 0) {
    tries += 1;
    for (i = 0; i < gasneti_nodes; i += 1) {
      if (bound_ep_handles[i] != NULL) {
	status = GNI_EpUnbind(bound_ep_handles[i]);
	if (status != GNI_RC_SUCCESS) {
	  fprintf(stderr, "node %d shutdown epunbind %d try %d  got %s\n",
		  gasneti_mynode, i, tries, gni_return_string(status));
	} 
	status = GNI_EpDestroy(bound_ep_handles[i]);
	if (status != GNI_RC_SUCCESS) {
	  fprintf(stderr, "node %d shutdown epdestroy %d try %d  got %s\n",
		  gasneti_mynode, i, tries, gni_return_string(status));
	} else {
	  bound_ep_handles[i] = NULL;
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
    status = GNI_SmsgGetNext(bound_ep_handles[pe], 
			     (void **) &recv_header);
    GASNETC_UNLOCK_GNI_IF_SEQ();
    if (status == GNI_RC_SUCCESS) {
      gasneti_assert((((uintptr_t) recv_header) & 7) == 0);
      numargs = recv_header->numargs;
      if (numargs > gasnet_AMMaxArgs()) {
	gasnetc_GNIT_Abort("numargs %d, max is %ld\n", numargs, gasnet_AMMaxArgs());
      }
      GASNETI_TRACE_PRINTF(A, ("smsg r from %d to %d type %s\n", pe, gasneti_mynode, gasnetc_type_string(recv_header->command)));
      is_req = (1 & recv_header->command); /* Requests have ODD values */
      switch (recv_header->command) {
      case GC_CMD_AM_NOP_REPLY: {
	status = GNI_SmsgRelease(bound_ep_handles[pe]);
        GASNETC_UNLOCK_GNI_IF_PAR();
	gasnetc_return_am_credit(pe);
	break;
      }
      case GC_CMD_AM_SHORT:
      case GC_CMD_AM_SHORT_REPLY: {
	head_length = GASNETC_HEADLEN(short, numargs);
	memcpy(&buffer, recv_header, head_length);
	status = GNI_SmsgRelease(bound_ep_handles[pe]);
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
	status = GNI_SmsgRelease(bound_ep_handles[pe]);
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
	status = GNI_SmsgRelease(bound_ep_handles[pe]);
	GASNETC_UNLOCK_GNI_IF_PAR();
	gasnetc_handle_am_long_packet(is_req, pe, &buffer.packet.galp);
	break;
      }
      case GC_CMD_SYS_SHUTDOWN_REQUEST: {
	memcpy(&buffer, recv_header, sizeof(buffer.packet.gssp));
	status = GNI_SmsgRelease(bound_ep_handles[pe]);
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

static gasneti_lifo_head_t gasnetc_smsg_pool = GASNETI_LIFO_INITIALIZER;
static gasneti_lifo_head_t gasnetc_smsg_buffers = GASNETI_LIFO_INITIALIZER;
static gasnetc_smsg_t **gasnetc_smsg_table = NULL;
#define GC_SMGS_POOL_CHUNKLEN 64
#define GC_SMGS_LAST_MSGID 0xFFFF0000 /* Values above this are "special" */
#define GC_SMGS_NOP      0xFFFF0001 /* No completion action(s) */
#define GC_SMGS_SHUTDOWN 0xFFFF0002

/* For exit code, here for use in gasnetc_free_smsg */
static gasneti_weakatomic_t shutdown_smsg_counter = gasneti_weakatomic_init(0);

GASNETI_INLINE(gasnetc_smsg_buffer)
void * gasnetc_smsg_buffer(size_t buffer_len) {
  void *result = gasneti_lifo_pop(&gasnetc_smsg_buffers);
  return result ? result : gasneti_malloc(GASNETC_MSG_MAXSIZE); /* XXX: less? */
}

/* MUST call with GNI lock held */
GASNETI_INLINE(gasnetc_free_smsg)
void gasnetc_free_smsg(uint32_t msgid)
{
  if_pt (msgid < GC_SMGS_LAST_MSGID) {
    const uint32_t chunk = msgid / GC_SMGS_POOL_CHUNKLEN;
    const uint32_t index = msgid % GC_SMGS_POOL_CHUNKLEN;
    gasnetc_smsg_t *smsg = &gasnetc_smsg_table[chunk][index];
    if (smsg->buffer) {
      gasneti_lifo_push(&gasnetc_smsg_buffers, smsg->buffer);
    }
    gasneti_lifo_push(&gasnetc_smsg_pool, smsg);
  } else if_pf (msgid == GC_SMGS_SHUTDOWN) {
    gasneti_weakatomic_increment(&shutdown_smsg_counter, GASNETI_ATOMIC_NONE);
  }
}

gasnetc_smsg_t *gasnetc_alloc_smsg(void)
{
  gasnetc_smsg_t *result = (gasnetc_smsg_t *)gasneti_lifo_pop(&gasnetc_smsg_pool);

  if_pf (NULL == result) {
    GASNETC_LOCK_GNI();

    /* retry holding lock to avoid redundant growers */
    result = (gasnetc_smsg_t *)gasneti_lifo_pop(&gasnetc_smsg_pool);

    if_pt (NULL == result) {
      static uint32_t next_msgid = 0;

      gasnetc_smsg_t *new_chunk = gasneti_malloc(GC_SMGS_POOL_CHUNKLEN * sizeof(gasnetc_smsg_t));
      unsigned int chunks = next_msgid / GC_SMGS_POOL_CHUNKLEN;
      int i;

      gasnetc_smsg_table = gasneti_realloc(gasnetc_smsg_table, (chunks+1) * sizeof(gasnetc_smsg_t *));
      gasnetc_smsg_table[chunks] = new_chunk;

    #if GC_SMGS_POOL_CHUNKLEN > 1
      for (i=0; i<GC_SMGS_POOL_CHUNKLEN; ++i) {
        new_chunk[i].msgid = next_msgid++;
        gasneti_lifo_link(new_chunk+i, new_chunk+i+1);
      }
      gasneti_lifo_push_many(&gasnetc_smsg_pool, new_chunk+1, new_chunk+GC_SMGS_POOL_CHUNKLEN-1);
    #else
      new_chunk[0].msgid = next_msgid++;
    #endif

      GASNETI_TRACE_PRINTF(A, ("smsg pool grew to %u\n", (unsigned int)next_msgid));

      result = &new_chunk[0];
    }

    GASNETC_UNLOCK_GNI();
  }

  gasneti_assert(NULL != result);
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
  const int max_trial = 4;
  int trial = 0;

#if GASNETC_SMSG_RETRANSMIT
  const uint32_t msgid = smsg->msgid;
  smsg->buffer = !do_copy ? NULL :
    (data = data_length ? memcpy(gasnetc_smsg_buffer(data_length), data, data_length) : NULL);
#else
  const uint32_t msgid = 0;
#endif

  GASNETI_TRACE_PRINTF(A, ("smsg s from %d to %d type %s\n", gasneti_mynode, dest, gasnetc_type_string(((GC_Header_t *) header)->command)));

  for (;;) {
    GASNETC_LOCK_GNI();
    status = GNI_SmsgSend(bound_ep_handles[dest],
                          header, header_length,
                          data, data_length, msgid);
    GASNETC_UNLOCK_GNI();

    if_pt (status == GNI_RC_SUCCESS) break;
    if (status != GNI_RC_NOT_DONE) {
      gasnetc_GNIT_Abort("GNI_SmsgSend returned error %s\n", gni_return_string(status));
    }

    if (++trial == max_trial) return GASNET_ERR_RESOURCE;

    /* XXX: On Gemini GNI_RC_NOT_DONE should NOT happen due to our flow control.
       However, it DOES rarely happen, expecially when using all the cores.
       Of course the fewer credits we allocate the more likely it is.
       So, we retry a finite number of times.  -PHH 2012.05.12
       TODO: Determine why/how we see NOT_DONE.
       On Aries GNI_RC_NOT_DONE will occur "normally" whenever the Cq is full
     */
    GASNETI_TRACE_PRINTF(A, ("smsg send got GNI_RC_NOT_DONE on trial %d\n", trial+1));
    gasnetc_poll_local_queue();
  }
  return(GASNET_OK);
}


void gasnetc_poll_local_queue(void)
{
  gni_return_t status;
  gni_cq_entry_t event_data;
  int i;
  gni_post_descriptor_t *pd;
  gasnetc_post_descriptor_t *gpd;

  GASNETC_LOCK_GNI();
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
#if GASNETC_OPTIMIZE_LIMIT_CQ
      outstanding_req--;  /* already lock protected */
#endif


      /* handle remaining work */
      if (gpd->flags & GC_POST_COPY) {
	memcpy(gpd->get_target, gpd->bounce_buffer, gpd->get_nbytes);
      } else if (gpd->flags & GC_POST_SEND) {
#if GASNETC_SMSG_RETRANSMIT
        gasnetc_smsg_t *smsg = gpd->u.smsg_p;
        const uint32_t msgid = smsg->msgid;
#else
        gasnetc_smsg_t *smsg = &gpd->u.smsg;
        const uint32_t msgid = 0;
#endif
        gasnetc_am_long_packet_t *galp = &smsg->smsg_header.galp;
        const size_t header_length = GASNETC_HEADLEN(long, galp->header.numargs);
        status = GNI_SmsgSend(bound_ep_handles[gpd->dest],
                              &smsg->smsg_header, header_length,
                              NULL, 0,  msgid);
        gasneti_assert_always (status == GNI_RC_SUCCESS);
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
	gasneti_weakatomic_increment(&reg_credit, GASNETI_ATOMIC_NONE);
      } else if (gpd->flags & GC_POST_UNBOUNCE) {
	gasnetc_free_bounce_buffer(gpd->bounce_buffer);
      }
      gasnetc_free_post_descriptor(gpd);
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
#if GASNETC_SMSG_RETRANSMIT
  static gasnetc_smsg_t m = { { { {GC_CMD_AM_NOP_REPLY, } } }, NULL, GC_SMGS_NOP };
#else
  static gasnetc_smsg_t m = { { { {GC_CMD_AM_NOP_REPLY, } } } };
#endif
  int rc = gasnetc_send_smsg(pe, &m, sizeof(gasnetc_am_nop_packet_t), NULL, 0, 0);
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
  printf("r %d cq_mode_type: %d\n", gasneti_mynode, cmd->type);
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

/* These are here due to transient resource problems in PostRdma
 *  They are not expected to happen (often)
 */

static gni_return_t myPostRdma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  int i;
  i = 0;
#if GASNETC_OPTIMIZE_LIMIT_CQ
  while (outstanding_req >= max_outstanding_req) {
    GASNETC_UNLOCK_GNI();
    gasnetc_poll_local_queue();
    GASNETC_LOCK_GNI();
  }
  outstanding_req++;
#endif
  for (;;) {
      status = GNI_PostRdma(ep, pd);
      if (status == GNI_RC_SUCCESS) return status;
      if (status != GNI_RC_ERROR_RESOURCE) break;
      if (++i >= 1000) {
	fprintf(stderr, "postrdma retry failed\n");
	break;
      }
      GASNETC_UNLOCK_GNI();
      gasnetc_poll_local_queue();
      GASNETC_LOCK_GNI();
  }
#if GASNETC_OPTIMIZE_LIMIT_CQ
  gasneti_assert(status != GNI_RC_SUCCESS);
  outstanding_req--;  /* failed */
#endif
  return (status);
}

static gni_return_t myPostFma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  int i;
  i = 0;
#if GASNETC_OPTIMIZE_LIMIT_CQ
  while (outstanding_req >= max_outstanding_req) {
    GASNETC_UNLOCK_GNI();
    gasnetc_poll_local_queue();
    GASNETC_LOCK_GNI();
  }
  outstanding_req++;
#endif
  for (;;) {
      status = GNI_PostFma(ep, pd);
      if (status == GNI_RC_SUCCESS) return status;
      if (status != GNI_RC_ERROR_RESOURCE) break;
      if (++i >= 1000) {
	fprintf(stderr, "postfma retry failed\n");
	break;
      }
      GASNETC_UNLOCK_GNI();
      gasnetc_poll_local_queue();
      GASNETC_LOCK_GNI();
  }
#if GASNETC_OPTIMIZE_LIMIT_CQ
  gasneti_assert(status != GNI_RC_SUCCESS);
  outstanding_req--;  /* failed */
#endif
  return (status);
}

/* Register local side of a pd, with bounded retry */
static gni_return_t myRegisterPd(gni_post_descriptor_t *pd)
{
  const uint64_t addr = pd->local_addr;
  const size_t nbytes = pd->length;
  const int limit = 10;
  int count = 0;
  gni_return_t status;

  while (gasnetc_atomic_dec_if_positive(&reg_credit) == 0) gasnetc_poll_local_queue();

  GASNETC_LOCK_GNI();
  do {
    status = GNI_MemRegister(nic_handle, addr, nbytes, NULL,
                             gasnetc_memreg_flags, -1, &pd->local_mem_hndl);
    if_pt (status == GNI_RC_SUCCESS) break;
    fprintf(stderr, "MemRegister fault %d at %p %lx, code %s\n",
            count, (void*)addr, (unsigned long)nbytes, gni_return_string(status));
    if (status != GNI_RC_ERROR_RESOURCE) break; /* Fatal */
  } while (++count < limit);
  GASNETC_UNLOCK_GNI();

  return status;
}

/* Perform an rdma/fma Put with no concern for local completion */
void gasnetc_rdma_put_bulk(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  gni_post_descriptor_t *pd;
  gni_return_t status;

  /*   if (nbytes == 0) return;  shouldn't happen */
  pd = &gpd->pd;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer_mem_handle[dest];
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
      GASNETC_LOCK_GNI();
      status = myPostFma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if_pf (status != GNI_RC_SUCCESS) {
	print_post_desc("postfma", pd);
	gasnetc_GNIT_Abort("PostFma(Put) failed with %s\n", gni_return_string(status));
      }
  } else {
      pd->type = GNI_POST_RDMA_PUT;
      GASNETC_LOCK_GNI();
      status =myPostRdma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if_pf (status != GNI_RC_SUCCESS) {
	print_post_desc("postrdma", pd);
	gasnetc_GNIT_Abort("PostRdma(Put) failed with %s\n", gni_return_string(status));
      }
  }
}

/* Perform an rdma/fma Put which favors rapid local completion
 * The return value is boolean, where 1 means locally complete.
 */
int gasnetc_rdma_put(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  gni_post_descriptor_t *pd;
  gni_return_t status;
  int result = 1; /* assume local completion */

  /*   if (nbytes == 0) return;  shouldn't happen */
  pd = &gpd->pd;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer_mem_handle[dest];
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
      GASNETC_LOCK_GNI();
      status = myPostFma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if_pf (status != GNI_RC_SUCCESS) {
	print_post_desc("postfma", pd);
	gasnetc_GNIT_Abort("PostFma(Put) failed with %s\n", gni_return_string(status));
      }
#if GASNET_CONDUIT_GEMINI
    /* On Gemini (only) return from PostFma follows local completion */
    result = 1; /* even if was zeroed by choice of zero-copy */
#endif
  } else {
      pd->type = GNI_POST_RDMA_PUT;
      GASNETC_LOCK_GNI();
      status =myPostRdma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if_pf (status != GNI_RC_SUCCESS) {
	print_post_desc("postrdma", pd);
	gasnetc_GNIT_Abort("PostRdma(Put) failed with %s\n", gni_return_string(status));
      }
  }

  gasneti_assert((result == 0) || (result == 1)); /* ensures caller can use "&=" or "+=" */
  return result;
}

/* Algorithm

   get reuqires 4 byte alignment for source, dst, and length

   if ((src | dst | len) & 3) == 0 {  // case aligned 
      if (in segment) {
        if (length < YYY) PostFMA
	else PostRdma
      } else { // out of semgnt
        if (length < XXX) use bounce buffer
        else call memregister
      }
      } else { //unaligned
        if (length < XXX) use bounce buffer
	   if (length < YYY) PostFMA
	   else PostRdma
	else use active message
      }
 */


/* for get, source_addr is remote */
void gasnetc_rdma_get(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  gni_post_descriptor_t *pd;
  gni_return_t status;

  /*  if (nbytes == 0) return; */
  pd = &gpd->pd;
  gpd->flags |= GC_POST_GET;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) source_addr;
  pd->remote_mem_hndl = peer_mem_handle[dest];
  pd->length = nbytes;

  /* confirm that the destination is in-segment on the far end */
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
      gpd->get_nbytes = nbytes;
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = my_mem_handle;
    } else if (nbytes <= gasnetc_bounce_register_cutover) {
      gpd->flags |= GC_POST_UNBOUNCE | GC_POST_COPY;
      gpd->bounce_buffer = gasnetc_alloc_bounce_buffer();
      gpd->get_target = dest_addr;
      gpd->get_nbytes = nbytes;
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
      GASNETC_LOCK_GNI();
      status = myPostFma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if_pf (status != GNI_RC_SUCCESS) {
	print_post_desc("postfma", pd);
	gasnetc_GNIT_Abort("PostFma(Get) failed with %s\n", gni_return_string(status));
      }
  } else {
      pd->type = GNI_POST_RDMA_GET;
      GASNETC_LOCK_GNI();
      status =myPostRdma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if_pf (status != GNI_RC_SUCCESS) {
	print_post_desc("postrdma", pd);
	gasnetc_GNIT_Abort("PostRdma(Get) failed with %s\n", gni_return_string(status));
      }
  }
}


void gasnetc_get_am_credit(uint32_t pe)
{
  gasneti_weakatomic_t *p = &peer_data[pe].am_credit;
#if GASNETC_DEBUG
  fprintf(stderr, "r %d get am credit for %d, before is %d\n",
	 gasneti_mynode, pe, (int)gasneti_weakatomic_read(&peer_data[pe].am_credit, 0));
#endif
  while (gasnetc_atomic_dec_if_positive(p) == 0) {
    gasneti_AMPoll();
    gasneti_spinloop_hint();
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
  gasnetc_post_descriptor_t *gpd;
  while ((gpd = (gasnetc_post_descriptor_t *) 
	  gasneti_lifo_pop(&post_descriptor_pool)) == NULL)
    gasnetc_poll_local_queue();
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


/* XXX: probably need to obtain am_credit or otherwise guard against
   the possibility of GNI_SmsgSend() returning GNI_RC_NOT_DONE. */
extern void gasnetc_sys_SendShutdownMsg(gasnet_node_t node, int shift, int exitcode)
{
#if GASNETC_SMSG_RETRANSMIT
  static gasnetc_smsg_t shutdown_smsg[32];
  gasnetc_smsg_t *smsg = &shutdown_smsg[shift];
#else
  static gasnetc_smsg_t shutdown_smsg;
  gasnetc_smsg_t *smsg = &shutdown_smsg;
#endif
  int result;

  gasnetc_sys_shutdown_packet_t *gssp = &smsg->smsg_header.gssp;
  GASNETI_TRACE_PRINTF(C,("Send SHUTDOWN Request to node %d w/ shift %d, exitcode %d",node,shift,exitcode));
  gssp->header.command = GC_CMD_SYS_SHUTDOWN_REQUEST;
  gssp->header.misc    = exitcode; /* only 15 bits, but exit() only preserves low 8-bits anyway */
  gssp->header.numargs = 0;
  gssp->header.handler = shift; /* log(distance) */
#if GASNETC_SMSG_RETRANSMIT
  smsg->msgid = GC_SMGS_SHUTDOWN;
#endif
  result = gasnetc_send_smsg(node, smsg, sizeof(gasnetc_sys_shutdown_packet_t), NULL, 0, 0);
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
  gasneti_assert_always(((uint32_t)sender + distance) % gasneti_nodes == gasneti_mynode);
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
  uint32_t goal = 0;
  uint32_t distance;
  int result = 0; /* success */
  int exitcode = *exitcode_p;
  int oldcode;
  int shift;
  gasneti_tick_t timeout_us = 1e6 * gasnetc_shutdown_seconds;
  gasneti_tick_t starttime = gasneti_ticks_now();

  GASNETI_TRACE_PRINTF(C,("Entering SYS EXIT"));

  /*  gasneti_assert(portals_sysqueue_initialized); */

  for (distance = 1, shift = 0; distance < gasneti_nodes; distance *= 2, ++shift) {
    gasnet_node_t peer;

    if (distance >= gasneti_nodes - gasneti_mynode) {
      peer = gasneti_mynode - (gasneti_nodes - distance);
    } else {
      peer = gasneti_mynode + distance;
    }

    oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
    exitcode = MAX(exitcode, oldcode);

    gasnetc_sys_SendShutdownMsg(peer, shift, exitcode);

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
 * As of 2013.02.08 I have systems with
 *     Gemini = 336 bytes
 *     Aries  = 352 bytes
 */
#if GASNET_CONDUIT_GEMINI
  #define GASNETC_SIZEOF_GDP 336
#else
  #define GASNETC_SIZEOF_GDP 352
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
  void *buf;
  while ((buf = gasneti_lifo_pop(&gasnetc_bounce_buffer_pool)) == NULL) 
	 gasnetc_poll_local_queue();
  return(buf);
}

void gasnetc_free_bounce_buffer(void *gcb)
{
  gasneti_lifo_push(&gasnetc_bounce_buffer_pool, gcb);
}
