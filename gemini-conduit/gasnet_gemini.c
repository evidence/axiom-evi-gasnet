#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_handler.h>
#include <gasnet_gemini.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>

/* LCS MSG_MAXSIZE should be AM medium sized */
#define MB_MAXCREDIT 64
#define CACHELINE_SIZE 64
#define MSG_MAXSIZE (((sizeof(gc_am_medium_packet_max_t) + \
		       gasnet_AMMaxMedium() + CACHELINE_SIZE - 1) \
		     / CACHELINE_SIZE) * CACHELINE_SIZE)
int gc_poll_burst = 10;
gc_queue_t smsg_work_queue;

typedef struct peer_struct {
  gc_queue_item_t qi;;
  uint32_t rank;
  uint32_t nic_address;
  void *heap_base;
  gni_mem_handle_t heap_mem_handle;
  gni_smsg_attr_t smsg_attr;
  gasneti_atomic_t am_credit;
  gasneti_atomic_t fma_credit;
  gasneti_mutex_t lock;
} peer_struct_t;

typedef struct peer_segment_struct {
  void *segment_base;
  uint64_t segment_size;
  gni_mem_handle_t segment_mem_handle;
} peer_segment_struct_t;

peer_struct_t mypeerdata;
peer_struct_t *peer_data;
peer_segment_struct_t mypeersegmentdata;
peer_segment_struct_t *peer_segment_data;

uint32_t *MPID_UGNI_AllAddr;

gni_cdm_handle_t cdm_handle;
gni_nic_handle_t nic_handle;
gni_ep_handle_t *bound_ep_handles;
gni_cq_handle_t bound_cq_handle;
gni_cq_handle_t smsg_cq_handle;

void *smsg_mmap_ptr;
size_t smsg_mmap_bytes;

int smsg_fd;
const char *gni_return_string(gni_return_t status)
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


const char *gc_type_string(int type)
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


/* called after segment init. See gasneti_seginfo */
/* allgather the memory handles for the segments */
/* create endpoints */
void gc_init_segment(void *segment_start, size_t segment_size)
{
  gni_return_t status;
  /* Map the shared segment */

  mypeersegmentdata.segment_base = segment_start;
  mypeersegmentdata.segment_size = segment_size;
  {
    int count = 0;
    for (;;) {
      status = GNI_MemRegister(nic_handle, (uint64_t) segment_start, 
			       (uint64_t) segment_size, NULL,
			       GNI_MEM_STRICT_PI_ORDERING | GNI_MEM_PI_FLUSH 
			       | GNI_MEM_READWRITE, -1, 
			       &mypeersegmentdata.segment_mem_handle);
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
  GNIT_Log("segment mapped %p, %p", segment_start, (void *) segment_size);
#endif
  peer_segment_data = gasneti_malloc(gc_num_ranks * sizeof(peer_segment_struct_t));
  assert(peer_segment_data);
  
  GNIT_Allgather(&mypeersegmentdata, sizeof(peer_segment_struct_t), peer_segment_data);
}

void gc_init_messaging()
{
  gni_return_t status;
  uint32_t peer;
  uint32_t remote_addr;
  uint32_t i;
  unsigned int bytes_per_mbox;
  unsigned int bytes_needed;
  int modes = 0;

#if GASNETC_DEBUG
  GNIT_Log("entering");
#endif

  GASNETC_INITLOCK_GNI();

  gc_queue_init(&smsg_work_queue);

  status = GNI_CdmCreate(gc_rank, GNIT_Ptag(), GNIT_Cookie(), 
			 modes,
			 &cdm_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  status = GNI_CdmAttach(cdm_handle, GNIT_Device_Id(),
			 &mypeerdata.nic_address,
			 &nic_handle);

  gasneti_assert_always (status == GNI_RC_SUCCESS);

#if GASNETC_DEBUG
  GNIT_Log("cdmattach");
#endif

  status = GNI_CqCreate(nic_handle, 1024, 0, GNI_CQ_NOBLOCK, NULL, NULL, &bound_cq_handle);

  gasneti_assert_always (status == GNI_RC_SUCCESS);


  bound_ep_handles = (gni_ep_handle_t *)gasneti_malloc(gc_num_ranks * sizeof(gni_ep_handle_t));
  gasneti_assert_always(bound_ep_handles != NULL);
  MPID_UGNI_AllAddr = (uint32_t *)gather_nic_addresses();

  for (i=0; i<gc_num_ranks; i++) {
    status = GNI_EpCreate(nic_handle, bound_cq_handle, &bound_ep_handles[i]);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
    remote_addr = MPID_UGNI_AllAddr[i];
    status = GNI_EpBind(bound_ep_handles[i], remote_addr, i);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
#if GASNETC_DEBUG
    GNIT_Log("ep bound to %d, addr %d", i, remote_addr);
#endif
  }


  /* Initialize the short message system */

  /*
   * allocate a CQ in which to receive message notifications
   */

  status = GNI_CqCreate(nic_handle,gc_num_ranks * MB_MAXCREDIT * 2,0,GNI_CQ_NOBLOCK,NULL,NULL,&smsg_cq_handle);
  if (status != GNI_RC_SUCCESS) {
    GNIT_Abort("GNI_CqCreate returned error %s\n", gni_return_string(status));
  }
  
  /*
   * Set up an mmap region to contain all of my mailboxes.
   * The GNI_SmsgBufferSizeNeeded is used to determine who
   * much memory is needed for each mailbox.
   */

  mypeerdata.smsg_attr.msg_type = GNI_SMSG_TYPE_MBOX;
  mypeerdata.smsg_attr.mbox_maxcredit = MB_MAXCREDIT;
  mypeerdata.smsg_attr.msg_maxsize = MSG_MAXSIZE;
#if GASNETC_DEBUG
  fprintf(stderr,"r %d maxcredit %d msg_maxsize %d\n", gc_rank, MB_MAXCREDIT, MSG_MAXSIZE);
#endif

  status = GNI_SmsgBufferSizeNeeded(&mypeerdata.smsg_attr,&bytes_per_mbox);
  if (status != GNI_RC_SUCCESS){
    GNIT_Abort("GNI_GetSmsgBufferSize returned error %s\n",gni_return_string(status));
  }
#if GASNETC_DEBUG
  fprintf(stderr,"r %d GetSmsgBufferSize says %d bytes for each mailbox\n", gc_rank, bytes_per_mbox);
#endif
  bytes_per_mbox = ((bytes_per_mbox + CACHELINE_SIZE - 1)/CACHELINE_SIZE) * CACHELINE_SIZE;
  /* test */
  bytes_per_mbox += mypeerdata.smsg_attr.mbox_maxcredit 
    * mypeerdata.smsg_attr.msg_maxsize;
  bytes_needed = gc_num_ranks * bytes_per_mbox;
  
#if GASNETC_DEBUG
  fprintf(stderr,"Allocating %d bytes for each mailbox\n",bytes_per_mbox);
  fprintf(stderr,"max medium %d, sizeof medium %d\n",gasnet_AMMaxMedium(),
	  sizeof(gc_am_medium_packet_max_t));
#endif
  smsg_fd = open("/dev/zero", O_RDWR, 0);
  if (smsg_fd == -1) {
    GNIT_Abort("open of /dev/zero failed: ");
  }
  
  /* do a shared mapping just to test if things can go wrong */
  
  smsg_mmap_ptr = mmap(NULL, bytes_needed, PROT_READ | PROT_WRITE, MAP_PRIVATE, smsg_fd, 0);
  smsg_mmap_bytes = bytes_needed;
  if (smsg_mmap_ptr == (char *)MAP_FAILED) {
    GNIT_Abort("mmap of /dev/zero failed: ");
  }
  
#if GASNETC_DEBUG
  GNIT_Log("mmap %p", smsg_mmap_ptr);
#endif
  {
    int count = 0;
    for (;;) {
      status = GNI_MemRegister(nic_handle, 
			       (unsigned long)smsg_mmap_ptr, 
			       bytes_needed,
			       smsg_cq_handle,
			       GNI_MEM_STRICT_PI_ORDERING | GNI_MEM_PI_FLUSH 
			       |GNI_MEM_READWRITE,
			       -1,
			       &mypeerdata.smsg_attr.mem_hndl);
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
    GNIT_Abort("GNI_MemRegister returned error %s\n",gni_return_string(status));
  }
  
  mypeerdata.smsg_attr.msg_type = GNI_SMSG_TYPE_MBOX;
  mypeerdata.smsg_attr.msg_buffer = smsg_mmap_ptr;
  mypeerdata.smsg_attr.buff_size = bytes_per_mbox;
  mypeerdata.smsg_attr.mbox_maxcredit = MB_MAXCREDIT;
  mypeerdata.smsg_attr.msg_maxsize = MSG_MAXSIZE;
  

#if GASNETC_DEBUG
  GNIT_Log("segment registered");
#endif

    /* exchange peer data */
  mypeerdata.rank = gc_rank;
  mypeerdata.nic_address = MPID_UGNI_AllAddr[gc_rank];
  
  peer_data = gasneti_malloc(gc_num_ranks * sizeof(peer_struct_t));
  gasneti_assert_always(peer_data);
  
  GNIT_Allgather(&mypeerdata, sizeof(peer_struct_t), peer_data);
  
  /* At this point, peer_data has information for everyone */
  /* We need to patch up the smsg data, fixing the remote start addresses */
  for (i = 0; i < gc_num_ranks; i += 1) {
    /* each am takes 2 credits (req and reply) and he pool is split between
     * requests travelling each way
     */
    gasneti_mutex_init(&peer_data[i].lock);
    gasneti_atomic_set(&peer_data[i].am_credit, MB_MAXCREDIT / 4, 0);
    gasneti_atomic_set(&peer_data[i].fma_credit, 1, 0);
    gc_queue_item_init(&peer_data[i].qi);
    peer_data[i].smsg_attr.mbox_offset = bytes_per_mbox * gc_rank;
    mypeerdata.smsg_attr.mbox_offset = bytes_per_mbox * i;
    status = GNI_SmsgInit(bound_ep_handles[i],
			  &mypeerdata.smsg_attr,
			  &peer_data[i].smsg_attr);
    if (status != GNI_RC_SUCCESS) {
      GNIT_Abort("GNI_SmsgInit returned error %s\n", gni_return_string(status));
    }
  }

  /* Now make sure everyone is ready */
#if GASNETC_DEBUG
  GNIT_Log("finishing");
#endif
  /* set the number of seconds we poll until forceful shutdown.  May be over-ridden
   * by env-var when they are processed as part of gasnetc_attach
   */
  gasnetc_shutdown_seconds = 3. + gasneti_nodes/8.;
  gasnetc_shutdown_seconds = (gasnetc_shutdown_seconds > shutdown_max ? shutdown_max : gasnetc_shutdown_seconds);

  gasnetc_fma_rdma_cutover = 
    gasneti_getenv_int_withdefault("GASNETC_GNI_FMA_RDMA_CUTOVER",
				   GASNETC_GNI_FMA_RDMA_CUTOVER_DEFAULT,1);
  if (gasnetc_fma_rdma_cutover > GASNETC_GNI_FMA_RDMA_CUTOVER_MAX)
    gasnetc_fma_rdma_cutover = GASNETC_GNI_FMA_RDMA_CUTOVER_MAX;
  if (gasnetc_fma_rdma_cutover > GASNETC_GNI_FMA_RDMA_CUTOVER_MAX)
    gasnetc_fma_rdma_cutover = GASNETC_GNI_FMA_RDMA_CUTOVER_MAX;

  GNIT_Barrier();
}

void gc_shutdown(void)
{
  int i;
  int tries;
  int left;
  gni_return_t status;
  // seize gni lock and hold it 
  GASNETC_LOCK_GNI();
  // Do other threads need to be killed off here?
  // release resources in the reverse order of acquisition


  // for each rank
  tries = 0;
  left = gc_num_ranks;
  while (left > 0) {
    tries += 1;
    for (i = 0; i < gc_num_ranks; i += 1) {
      if (bound_ep_handles[i] != NULL) {
	status = GNI_EpUnbind(bound_ep_handles[i]);
	if (status != GNI_RC_SUCCESS) {
	  fprintf(stderr, "node %d shutdown epunbind %d try %d  got %s\n",
		  gc_rank, i, tries, gni_return_string(status));
	} 
	status = GNI_EpDestroy(bound_ep_handles[i]);
	if (status != GNI_RC_SUCCESS) {
	  fprintf(stderr, "node %d shutdown epdestroy %d try %d  got %s\n",
		  gc_rank, i, tries, gni_return_string(status));
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
	    gc_rank, left);
  }
  status = GNI_MemDeregister(nic_handle,
			     &mypeersegmentdata.segment_mem_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  i = munmap(smsg_mmap_ptr, smsg_mmap_bytes);
  gasneti_assert_always (i == 0);

  status = GNI_MemDeregister(nic_handle,
			     &mypeerdata.smsg_attr.mem_hndl);
  gasneti_assert_always (status == GNI_RC_SUCCESS);


  status = GNI_CqDestroy(smsg_cq_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  status = GNI_CqDestroy(bound_cq_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);


  status = GNI_CdmDestroy(cdm_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);
  //  fprintf(stderr, "node %d gc_shutdown done\n", gc_rank);
}



void gc_handle_am_short_packet(int req, uint32_t source, 
			       gc_am_short_packet_t *am)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnet_token_t token = GC_CREATE_TOKEN(source);
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_SHORT(req, 
			    handlerindex,
			    handler,
			    token,
			    pargs,
			    numargs);
  __sync_synchronize();
}

void gc_handle_am_medium_packet(int req, uint32_t source, 
				gc_am_medium_packet_t *am, void* data)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnet_token_t token = GC_CREATE_TOKEN(source);
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_MEDIUM(req, 
			     handlerindex,
			     handler,
			     token,
			     pargs,
			     numargs,
			     data,
			     am->data_length);

  __sync_synchronize();
}
void gc_handle_am_long_packet(int req, uint32_t source, 
			      gc_am_long_packet_t *am)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnet_token_t token = GC_CREATE_TOKEN(source);
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
  __sync_synchronize();
}


extern void gc_handle_sys_shutdown_packet(uint32_t source, gc_sys_shutdown_packet_t *sys);
extern void  gc_poll_smsg_completion_queue(void);

#if GASNET_SEQ

void gc_process_smsg_q(gasnet_node_t pe)
{
  gni_return_t status;
  GC_Header_t *recv_header;
  void *im_data;
  uint32_t head_length;
  uint32_t length;
  uint32_t numargs;
  static int first = 0;
  if (first == 0) {
    fprintf(stderr, "sequential version\n");
    first = 1;
  }
  for (;;) {
    GASNETC_LOCK_GNI();
    status = GNI_SmsgGetNext(bound_ep_handles[pe], 
			     (void **) &recv_header);
    GASNETC_UNLOCK_GNI();
    if (status == GNI_RC_SUCCESS) {
      gasneti_assert((((uintptr_t) recv_header) & 7) == 0);
      numargs = recv_header->numargs;
      if (numargs > gasnet_AMMaxArgs()) {
	GNIT_Abort("numargs %d, max is %ld\n", numargs, gasnet_AMMaxArgs());
      }
      /* this only works for fewer than 256 ranks
      if (gc_rank != recv_header->to) {
	GNIT_Abort("rank %d but from %d\n", gc_rank, recv_header->to);
      }
      */
      GASNETI_TRACE_PRINTF(A, ("smsg r from %d to %d type %s\n", pe, gc_rank, gc_type_string(recv_header->command)));
      switch (recv_header->command) {
      case GC_CMD_AM_NOP_REPLY: {
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	gc_return_am_credit(pe);
	break;
      }
      case GC_CMD_AM_SHORT: {
	gc_handle_am_short_packet(1, pe, (gc_am_short_packet_t *) recv_header);
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	gc_send_am_nop(pe);
	break;
      }
      case GC_CMD_AM_MEDIUM: {
	head_length = GASNETI_ALIGNUP(sizeof(gc_am_medium_packet_t) 
				      + (numargs * sizeof(uint32_t)), 8);
	im_data = (void *) ((uintptr_t) recv_header + head_length);
	length = ((gc_am_medium_packet_t *) recv_header)->data_length;
	if (length > gasnet_AMMaxMedium()) {
	  GNIT_Abort("medium data_length %d, max is %ld\n", 
		     length, gasnet_AMMaxMedium());
	}
	gc_handle_am_medium_packet(1, 
				   pe, 
				   (gc_am_medium_packet_t *) recv_header, 
				   im_data);
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	gc_send_am_nop(pe);
	break;
      }
      case GC_CMD_AM_LONG: {
	length = ((gc_am_long_packet_t *) recv_header)->data_length;
	if (length <= gasnet_AMMaxMedium()) {
	  head_length = GASNETI_ALIGNUP(sizeof(gc_am_long_packet_t) 
					+ (numargs * sizeof(uint32_t)), 8);
	  im_data = (void *) (((uintptr_t) recv_header) + head_length);
	  memcpy(((gc_am_long_packet_t *) recv_header)->data, 
		 im_data,
		 length);
	}
	gc_handle_am_long_packet(1, pe, (gc_am_long_packet_t *) recv_header);
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	gc_send_am_nop(pe);
	break;
      }
      case GC_CMD_AM_SHORT_REPLY: {
	gc_handle_am_short_packet(0, pe, (gc_am_short_packet_t *) recv_header);
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	break;
      }
      case GC_CMD_AM_MEDIUM_REPLY: {
	head_length = GASNETI_ALIGNUP(sizeof(gc_am_medium_packet_t) +
				      (numargs * sizeof(uint32_t)), 8);
	im_data = (void *) ((uintptr_t) recv_header + head_length);
	length = ((gc_am_medium_packet_t *) recv_header)->data_length;
	if (length > gasnet_AMMaxMedium()) {
	  GNIT_Abort("medium data_length %d, max is %ld\n", 
		     length, gasnet_AMMaxMedium());
	}
	gc_handle_am_medium_packet(0, 
				   pe, 
				   (gc_am_medium_packet_t *) recv_header, 
				   im_data);
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	break;
      }
      case GC_CMD_AM_LONG_REPLY: {
	length = ((gc_am_long_packet_t *) recv_header)->data_length;
	if (length <= gasnet_AMMaxMedium()) {
	  head_length = GASNETI_ALIGNUP(sizeof(gc_am_long_packet_t) 
					+ (numargs * sizeof(uint32_t)), 8);
	  im_data = (void *) (((uintptr_t) recv_header) + head_length);
	  /* The data is in the packet, so copy it cout */
	  memcpy(((gc_am_long_packet_t *) recv_header)->data, 
		 im_data,
		 length);
	}
	gc_handle_am_long_packet(0, pe, (gc_am_long_packet_t *) recv_header);
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	break;
      }
      case GC_CMD_SYS_SHUTDOWN_REQUEST: {
	gc_sys_shutdown_packet_t packet;
	memcpy(&packet, recv_header, sizeof(gc_sys_shutdown_packet_t));
	GASNETC_SMSGRELEASE(status, bound_ep_handles[pe]);
	gc_handle_sys_shutdown_packet(pe, (gc_sys_shutdown_packet_t *)& packet);
	break;
      }
      default: {
	GNIT_Abort("unknown packet type");
      }
      }
      /* now check the SmsgRelease status */      
      if (status == GNI_RC_SUCCESS) {
	/* LCS nothing to do */
      } else {
	/* LCS SmsgRelease Failed */
	/* GNI_RC_INVALID_PARAM here means bad endpoint */
	/* GNI_RC_NOT_DONE here means there was no smsg */
	GNIT_Log("SmsgRelease from pe %d fail with %s\n",
		   pe, gni_return_string(status));
      }
    } else if (status == GNI_RC_NOT_DONE) {
      break;  /* GNI_RC_NOT_DONE here means there was no smsg */
    } else {
      GNIT_Abort("SmsgGetNext from pe %d fail with %s", 
		 pe, gni_return_string(status));
    }
    gc_poll_smsg_completion_queue();
  }
}

#else
/* PAR and PARSYNC */
void gc_process_smsg_q(gasnet_node_t pe)
{
  gni_return_t status;
  GC_Header_t *recv_header;
  gc_packet_t packet;
  uint64_t buffer[gasnet_AMMaxMedium() / sizeof(uint64_t)];
  void *im_data;
  uint32_t head_length;
  uint32_t numargs;
  static int first = 0;
  if (first == 0) {
    fprintf(stderr, "parallel version\n");
    first = 1;
  }
  for (;;) {
    GASNETC_LOCK_GNI();
    status = GNI_SmsgGetNext(bound_ep_handles[pe], 
			     (void **) &recv_header);
    if (status == GNI_RC_SUCCESS) {
      numargs = recv_header->numargs;
      if (numargs > gasnet_AMMaxArgs()) {
	GNIT_Abort("numargs %d, max is %ld\n", numargs, gasnet_AMMaxArgs());
      }
      GASNETI_TRACE_PRINTF(A, ("smsg r from %d to %d type %s\n", pe, gc_rank, gc_type_string(recv_header->command)));
      switch (recv_header->command) {
      case GC_CMD_AM_NOP_REPLY: {
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_return_am_credit(pe);
	break;
      }
      case GC_CMD_AM_SHORT: {
	memcpy(&packet, recv_header, sizeof(gc_am_short_packet_t) +
	       numargs * sizeof(uint32_t));
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_handle_am_short_packet(1, pe, (gc_am_short_packet_t *) &packet);
	gc_send_am_nop(pe);
	break;
      }
      case GC_CMD_AM_MEDIUM: {
	head_length = GASNETI_ALIGNUP(sizeof(gc_am_medium_packet_t) 
				      + (numargs * sizeof(uint32_t)), 8);
	im_data = (void *) ((uintptr_t) recv_header + head_length);
	memcpy(&packet, recv_header, sizeof(gc_am_medium_packet_t) +
	       numargs * sizeof(uint32_t));

	if (packet.gamp.data_length > gasnet_AMMaxMedium()) {
	  GNIT_Abort("medium data_length %ld, max is %ld\n", 
		     packet.gamp.data_length, gasnet_AMMaxMedium());
	}
	memcpy(buffer, im_data, packet.gamp.data_length);
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_handle_am_medium_packet(1, pe, (gc_am_medium_packet_t *) &packet, buffer);
	gc_send_am_nop(pe);
	break;
      }
      case GC_CMD_AM_LONG: {
	memcpy(&packet, recv_header, sizeof(gc_am_long_packet_t) +
	       numargs * sizeof(uint32_t));
	if (packet.galp.data_length <= gasnet_AMMaxMedium()) {
	  head_length = GASNETI_ALIGNUP(sizeof(gc_am_long_packet_t) 
					+ (numargs * sizeof(uint32_t)), 8);
	  im_data = (void *) (((uintptr_t) recv_header) + head_length);
	  memcpy(packet.galp.data, im_data, packet.galp.data_length);
	}
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_handle_am_long_packet(1, pe, (gc_am_long_packet_t *) &packet);
	gc_send_am_nop(pe);
	break;
      }
      case GC_CMD_AM_SHORT_REPLY: {
	memcpy(&packet, recv_header, sizeof(gc_am_short_packet_t) +
	       numargs * sizeof(uint32_t));
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_handle_am_short_packet(0, pe, (gc_am_short_packet_t *) &packet);
	break;
      }
      case GC_CMD_AM_MEDIUM_REPLY: {
	memcpy(&packet, recv_header, sizeof(gc_am_medium_packet_t) +
	       numargs * sizeof(uint32_t));

	if (packet.gamp.data_length > gasnet_AMMaxMedium()) {
	  GNIT_Abort("medium data_length %ld, max is %ld\n", 
		     packet.gamp.data_length, gasnet_AMMaxMedium());
	}
	head_length = GASNETI_ALIGNUP(sizeof(gc_am_medium_packet_t) +
				      (numargs * sizeof(uint32_t)), 8);
	im_data = (void *) ((uintptr_t) recv_header + head_length);
	memcpy(buffer, im_data, packet.gamp.data_length);
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_handle_am_medium_packet(0, pe, (gc_am_medium_packet_t *) &packet, buffer);
	break;
      }
      case GC_CMD_AM_LONG_REPLY: {
	memcpy(&packet, recv_header, sizeof(gc_am_long_packet_t) +
	       numargs * sizeof(uint32_t));
	if (packet.galp.data_length <= gasnet_AMMaxMedium()) {
	  head_length = GASNETI_ALIGNUP(sizeof(gc_am_long_packet_t) 
					+ (numargs * sizeof(uint32_t)), 8);
	  im_data = (void *) (((uintptr_t) recv_header) + head_length);
	  /* The data is in the packet, so copy it cout */
	  memcpy(packet.galp.data, im_data, packet.galp.data_length);
	}
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_handle_am_long_packet(0, pe, (gc_am_long_packet_t *) &packet);
	break;
      }
      case GC_CMD_SYS_SHUTDOWN_REQUEST: {
	memcpy(&packet, recv_header, sizeof(gc_sys_shutdown_packet_t));
	GASNETC_SMSGRELEASEUNLOCK(status, bound_ep_handles[pe]);
	gc_handle_sys_shutdown_packet(pe, (gc_sys_shutdown_packet_t *)& packet);
	break;
      }
      default: {
	GNIT_Abort("unknown packet type");
      }
      }
      /* now check the SmsgRelease status */      
      if (status == GNI_RC_SUCCESS) {
	/* LCS nothing to do */
      } else {
	/* LCS SmsgRelease Failed */
	/* GNI_RC_INVALID_PARAM here means bad endpoint */
	/* GNI_RC_NOT_DONE here means there was no smsg */
	GNIT_Log("SmsgRelease from pe %d fail with %s\n",
		   pe, gni_return_string(status));
      }
    } else if (status == GNI_RC_NOT_DONE) {
      GASNETC_UNLOCK_GNI();
      break;  /* GNI_RC_NOT_DONE here means there was no smsg */
    } else {
      GNIT_Abort("SmsgGetNext from pe %d fail with %s", 
		 pe, gni_return_string(status));
    }
    gc_poll_smsg_completion_queue();
  }
}

#endif


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

void gc_poll_smsg_completion_queue()
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
      gasneti_assert(source < gc_num_ranks);
      /* atomically enqueue the peer on the smsg queue if it isn't
	 already there.  */
      if (peer_data[source].qi.queue == NULL)
	gc_queue_enqueue_no_lock(&smsg_work_queue, 
				 (gc_queue_item_t *) &peer_data[source]);
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
      GNIT_Abort("CqGetEvent(smsg_cq) drain returns error %s", gni_return_string(status));
    }
    GASNETC_UNLOCK_GNI();
    /* and enqueue everyone on the work queue, who isn't already */
    GASNETC_LOCK_QUEUE(&smsg_work_queue);
    for (source = 0; source < gc_num_ranks; source += 1) {
      if (peer_data[source].qi.queue == NULL)
	gc_queue_enqueue_no_lock(&smsg_work_queue, 
				 (gc_queue_item_t *) &peer_data[source]);
    }
    GASNETC_UNLOCK_QUEUE(&smsg_work_queue);
    GNIT_Log("smsg queue overflow");
  } else {
    /* anything else is a fatal error */
    GNIT_Abort("CqGetEvent(smsg_cq) returns error %s", gni_return_string(status));
  }
}

void gc_poll_smsg_queue()
{
  int i;
  gasnet_node_t source;
  gc_poll_smsg_completion_queue();
  /* Now see about processing some peers off the smsg_work_queue */
  for (i = 0; i < SMSG_PEER_BURST; i += 1) {
    peer_struct_t *peer = (peer_struct_t *) gc_queue_pop(&smsg_work_queue);
    if (peer == NULL) break;
    source = peer->rank;
    gc_process_smsg_q(source);
  }
}


void gc_poll_local_queue()
{
  gni_return_t status;
  gni_cq_entry_t event_data;
  int i;
  gni_post_descriptor_t *pd;
  gc_post_descriptor_t *gpd;

  GASNETC_LOCK_GNI();
  for (i = 0; i < gc_poll_burst; i += 1) {
    /* Poll the bound_ep completion queue */
    status = GNI_CqGetEvent(bound_cq_handle,&event_data);
    if (status == GNI_RC_SUCCESS) {
      status = GNI_GetCompleted(bound_cq_handle, event_data, &pd);
      if (status != GNI_RC_SUCCESS)
	GNIT_Abort("GetCompleted(%p) failed %s\n",
		   (void *) event_data, gni_return_string(status));
      gpd = container_of(pd, gc_post_descriptor_t, pd);
      

      /* handle remaining work */
      if (gpd->flags & GC_POST_COPY) {
	memcpy(gpd->get_target, gpd->bounce_buffer, gpd->get_nbytes);
      }
      if (gpd->flags & GC_POST_UNREGISTER) {
	status = GNI_MemDeregister(nic_handle, &gpd->mem_handle);
	gasneti_assert_always (status == GNI_RC_SUCCESS);
      }
      if (gpd->flags & GC_POST_UNBOUNCE) {
	gc_free_bounce_buffer(gpd->bounce_buffer);
      }
      if (gpd->flags & GC_POST_SEND) {
	status = GNI_SmsgSend(bound_ep_handles[gpd->dest], &gpd->galp, 
			      sizeof(gc_am_long_packet_t) + 
			      (gpd->galp.header.numargs * sizeof(uint32_t)),
			      NULL, 0, 0);
	gasneti_assert_always (status == GNI_RC_SUCCESS);
      }
      /* atomic int of the completion pointer suffices for explicit
	 and implicit nb operations in GASNet */

      if (gpd->flags & GC_POST_COMPLETION_FLAG) {
	gasneti_atomic_set((gasneti_atomic_t *) gpd->completion, 1, 0);
      }
      if (gpd->flags & GC_POST_COMPLETION_EOP) {
	gasnete_op_markdone(gpd->completion, (gpd->flags & GC_POST_GET) != 0);
      }
      gc_free_post_descriptor(gpd);
    } else if (status == GNI_RC_NOT_DONE) {
      break;
    } else {
      GNIT_Log("bound CqGetEvent %s\n", gni_return_string(status));
    }
  }
  GASNETC_UNLOCK_GNI();
}
  
void gc_poll()
{
  gc_poll_smsg_queue();
  gc_poll_local_queue();
}

void gc_send_am_nop(uint32_t pe)
{
  gc_am_nop_packet_t m;
  m.header.command = GC_CMD_AM_NOP_REPLY;
  m.header.numargs = 0;
  gc_send(pe, &m, sizeof(gc_am_nop_packet_t), NULL, 0);
}


int gc_send(gasnet_node_t dest, 
	    void *header, int header_length, 
	    void *data, int data_length)
{
  gni_return_t status;
  GASNETI_TRACE_PRINTF(A, ("smsg s from %d to %d type %s\n", gc_rank, dest, gc_type_string(((GC_Header_t *) header)->command)));
  for (;;) {
    GASNETC_LOCK_GNI();

    status = GNI_SmsgSend(bound_ep_handles[dest], header, 
			  header_length, data, data_length, 0);
    GASNETC_UNLOCK_GNI();
    if (status == GNI_RC_SUCCESS) break;
    /*if (status == GNI_RC_NOT_DONE) continue; */
    GNIT_Abort("GNI_SmsgSend returned error %s\n", gni_return_string(status));
  }
  return(GASNET_OK);
}


void print_post_desc(char *title, gni_post_descriptor_t *cmd)
{
  printf("r %d %s, desc addr %p\n", gc_rank, title, cmd);
  printf("r %d status: %ld\n", gc_rank, cmd->status);
  printf("r %d cq_mode_complete: 0x%x\n", gc_rank, cmd->cq_mode_complete);
  printf("r %d cq_mode_type: %d\n", gc_rank, cmd->type);
  printf("r %d cq_mode: 0x%x\n", gc_rank, cmd->cq_mode);
  printf("r %d dlvr_mode: 0x%x\n", gc_rank, cmd->dlvr_mode);
  printf("r %d local_address: %p(0x%lx, 0x%lx)\n", gc_rank, (void *) cmd->local_addr, 
	 cmd->local_mem_hndl.qword1, cmd->local_mem_hndl.qword2);
  printf("r %d remote_address: %p(0x%lx, 0x%lx)\n", gc_rank, (void *) cmd->remote_addr, 
	 cmd->remote_mem_hndl.qword1, cmd->remote_mem_hndl.qword2);
  printf("r %d length: 0x%lx\n", gc_rank, cmd->length);
  printf("r %d rdma_mode: 0x%x\n", gc_rank, cmd->rdma_mode);
  printf("r %d src_cq_hndl: %p\n", gc_rank, cmd->src_cq_hndl);
  printf("r %d sync: (0x%lx,0x%lx)\n", gc_rank, cmd->sync_flag_value, cmd->sync_flag_addr);
  printf("r %d amo_cmd: %d\n", gc_rank, cmd->amo_cmd);
  printf("r %d amo: 0x%lx, 0x%lx\n", gc_rank, cmd->first_operand, cmd->second_operand);
  printf("r %d cqwrite_value: 0x%lx\n", gc_rank, cmd->cqwrite_value);
}

/* These are here due to transient resource problems in PostRdma
 *  They are not expected to happen (often)
 */

gni_return_t myPostRdma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  int i;
  i = 0;
  for (;;) {
      status = GNI_PostRdma(ep, pd);
      if (status == GNI_RC_SUCCESS) break;
      if (status != GNI_RC_ERROR_RESOURCE) break;
      if (i >= 1000) {
	fprintf(stderr, "postrdma retry failed\n");
	break;
      }
      GASNETC_UNLOCK_GNI();
      gc_poll_local_queue();
      GASNETC_LOCK_GNI();
  }
  return (status);
}

gni_return_t myPostFma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  int i;
  i = 0;
  for (;;) {
      status = GNI_PostFma(ep, pd);
      if (status == GNI_RC_SUCCESS) break;
      if (status != GNI_RC_ERROR_RESOURCE) break;
      if (i >= 1000) {
	fprintf(stderr, "postrdma retry failed\n");
	break;
      }
      GASNETC_UNLOCK_GNI();
      gc_poll_local_queue();
      GASNETC_LOCK_GNI();
  }
  return (status);
}

void gc_rdma_put(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gc_post_descriptor_t *gpd)
{
  gni_post_descriptor_t *pd;
  gni_return_t status;
  //  gni_mem_handle_t mem_handle;

  /*   if (nbytes == 0) return;  shouldn't happen */
  pd = &gpd->pd;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, dest_addr, nbytes);
  if (!gasneti_in_segment(gc_rank, source_addr, nbytes)) {
    /* source not (entirely) in segment */
    /* if (nbytes < gc_bounce_register_cutover)  then use bounce buffer
     * else mem-register
     */
    pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
    pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
    pd->remote_addr = (uint64_t) dest_addr;
    pd->remote_mem_hndl = peer_segment_data[dest].segment_mem_handle;
    pd->length = nbytes;
    /* first deal with the memory copy and bounce buffer assignment */
    if (nbytes <= GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
      gpd->bounce_buffer = gpd->immediate;
      memcpy(gpd->bounce_buffer, source_addr, nbytes);
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = mypeersegmentdata.segment_mem_handle;
    } else if (nbytes <  gasnetc_bounce_register_cutover) {
      gpd->flags |= GC_POST_UNBOUNCE;
      gpd->bounce_buffer = gc_alloc_bounce_buffer();
      memcpy(gpd->bounce_buffer, source_addr, nbytes);
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = mypeersegmentdata.segment_mem_handle;
    } else {
      gpd->flags |= GC_POST_UNREGISTER;
      pd->local_addr = (uint64_t) source_addr;
      GASNETC_LOCK_GNI();
      {
	int count = 0;
	for (;;) {
	  status = GNI_MemRegister(nic_handle, (uint64_t) source_addr, 
				   (uint64_t) nbytes, NULL,
				   GNI_MEM_STRICT_PI_ORDERING | GNI_MEM_PI_FLUSH 
				   |GNI_MEM_READWRITE, -1, &gpd->mem_handle);
	  if (status == GNI_RC_SUCCESS) break;
	  if (status == GNI_RC_ERROR_RESOURCE) {
	    fprintf(stderr, "MemRegister fault %d at  %p %lx, code %s\n", count, source_addr, nbytes,
		    gni_return_string(status));
	    count += 1;
	    if (count >= 10) break;
	  } else {
	    break;
	  }
	}
      }
      GASNETC_UNLOCK_GNI();
      gasneti_assert_always (status == GNI_RC_SUCCESS);
      pd->local_mem_hndl = gpd->mem_handle;
    }
    /* now initiate the transfer according to fma/rdma cutover */
    if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_PUT;
      GASNETC_LOCK_GNI();
      status = myPostFma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "non-segment-postfma", pd);
	GNIT_Abort("PostFMA failed with %s\n", gni_return_string(status));
      }
    } else {
      pd->type = GNI_POST_RDMA_PUT;
      GASNETC_LOCK_GNI();
      status = myPostRdma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "non-segment-postrdma", pd);
	GNIT_Abort("PostRdma failed with %s\n", gni_return_string(status));
      }
    }


  } else {
    if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_PUT;
      pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
      pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
      pd->local_addr = (uint64_t) source_addr;
      pd->local_mem_hndl = mypeersegmentdata.segment_mem_handle;
      pd->remote_addr = (uint64_t) dest_addr;
      pd->remote_mem_hndl = peer_segment_data[dest].segment_mem_handle;
      pd->length = nbytes;
      GASNETC_LOCK_GNI();
      status = myPostFma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "in-segment-postfma", pd);
	GNIT_Abort("Postfma failed with %s\n", gni_return_string(status));
      }
    } else {
      pd->type = GNI_POST_RDMA_PUT;
      pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
      pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
      pd->local_addr = (uint64_t) source_addr;
      pd->local_mem_hndl = mypeersegmentdata.segment_mem_handle;
      pd->remote_addr = (uint64_t) dest_addr;
      pd->remote_mem_hndl = peer_segment_data[dest].segment_mem_handle;
      pd->length = nbytes;
      GASNETC_LOCK_GNI();
      status =myPostRdma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "in-segment-postrdma", pd);
	GNIT_Abort("PostRdma failed with %s\n", gni_return_string(status));
      }
    }
  }
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
void gc_rdma_get(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gc_post_descriptor_t *gpd)
{
  gni_post_descriptor_t *pd;
  gni_return_t status;
  gni_mem_handle_t mem_handle;

  //  if (nbytes == 0) return;
  gasneti_assert(gpd);
  pd = &gpd->pd;
  gpd->flags |= GC_POST_GET;
  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, source_addr, nbytes);
  /* check where the local addr is */
  if (!gasneti_in_segment(gc_rank, dest_addr, nbytes)) {
    /* dest not (entirely) in segment */
    /* if (nbytes < gc_bounce_register_cutover)  then use bounce buffer
     * else mem-register
     */
    pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
    pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
    pd->remote_addr = (uint64_t) source_addr;
    pd->remote_mem_hndl = peer_segment_data[dest].segment_mem_handle;

    pd->length = nbytes;
    if (nbytes < GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
      gpd->flags |= GC_POST_COPY;
      gpd->bounce_buffer = gpd->immediate;
      gpd->get_target = dest_addr;
      gpd->get_nbytes = nbytes;
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = mypeersegmentdata.segment_mem_handle;
    } else if (nbytes < gasnetc_bounce_register_cutover) {
      gpd->flags |= GC_POST_UNBOUNCE | GC_POST_COPY;
      gpd->bounce_buffer = gc_alloc_bounce_buffer();
      gpd->get_target = dest_addr;
      gpd->get_nbytes = nbytes;
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
      pd->local_mem_hndl = mypeersegmentdata.segment_mem_handle;
    } else {
      gpd->flags |= GC_POST_UNREGISTER;
      pd->local_addr = (uint64_t) dest_addr;
      GASNETC_LOCK_GNI();
      {
	int count = 0;
	for (;;) {
	  status = GNI_MemRegister(nic_handle, (uint64_t) dest_addr, 
				   (uint64_t) nbytes, NULL,
				   GNI_MEM_STRICT_PI_ORDERING | GNI_MEM_PI_FLUSH 
				   |GNI_MEM_READWRITE, -1, &gpd->mem_handle);
	  if (status == GNI_RC_SUCCESS) break;
	  if (status == GNI_RC_ERROR_RESOURCE) {
	    fprintf(stderr, "MemRegister fault %d at  %p %lx, code %s\n",
		    count, dest_addr, nbytes,
		    gni_return_string(status));
	    count += 1;
	    if (count >= 10) break;
	  } else {
	    break;
	  }
	}
      }
      GASNETC_UNLOCK_GNI();
      gasneti_assert_always (status == GNI_RC_SUCCESS);
      pd->local_mem_hndl = gpd->mem_handle;
    }
    if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_GET;
      GASNETC_LOCK_GNI();
      status = myPostFma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "non-segment-postfma", pd);
	GNIT_Abort("PostFMA failed with %s\n", gni_return_string(status));
      }
    } else {
      pd->type = GNI_POST_RDMA_GET;
      GASNETC_LOCK_GNI();
      status = myPostRdma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "non-segment-postrdma", pd);
	GNIT_Abort("PostRdma failed with %s\n", gni_return_string(status));
      }
    }
  } else {
    pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
    pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
    pd->local_addr = (uint64_t) dest_addr;
    pd->local_mem_hndl = mypeersegmentdata.segment_mem_handle;
    pd->remote_addr = (uint64_t) source_addr;
    pd->remote_mem_hndl = peer_segment_data[dest].segment_mem_handle;
    pd->length = nbytes;
    if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_GET;
      GASNETC_LOCK_GNI();
      status = myPostFma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "in-segment-postfma", pd);
	GNIT_Abort("PostFMA failed with %s\n", gni_return_string(status));
      }
    } else {
      pd->type = GNI_POST_RDMA_GET;
      GASNETC_LOCK_GNI();
      status = myPostRdma(bound_ep_handles[dest], pd);
      GASNETC_UNLOCK_GNI();
      if (status != GNI_RC_SUCCESS) {
	print_post_desc((char *) "in-segment-postrdma", pd);
	GNIT_Abort("PostRdma failed with %s\n", gni_return_string(status));
      }
    }
  }
}


/* returns value - 1 even if it didn't change.  copied from kernel atomic.h */
static inline int32_t gc_atomic_dec_if_positive(gasneti_atomic_t *p)
{
  int32_t old, dec;
  for (;;) {
    old = gasneti_atomic_signed(gasneti_atomic_read(p, GASNETI_ATOMIC_NONE));
    dec = old - 1;
    if_pf(dec < 0) break;
    if (gasneti_atomic_compare_and_swap((p), old, dec, GASNETI_ATOMIC_NONE))
      break;
  }
  return (dec);
}
void gc_get_am_credit(uint32_t pe)
{
  gasneti_atomic_t *p = (gasneti_atomic_t *) &peer_data[pe].am_credit;
#if GASNETC_DEBUG
  fprintf(stderr, "r %d get am credit for %d, before is %d\n",
	 gc_rank, pe, peer_data[pe].am_credit);
#endif
  /* poll at least once, to assure forward progress */
  do {
    gc_poll();
  }  while (gc_atomic_dec_if_positive(p) < 0);
}

void gc_return_am_credit(uint32_t pe)
{
  gasneti_atomic_increment(&peer_data[pe].am_credit, GASNETI_ATOMIC_NONE);
#if GASNETC_DEBUG
  fprintf(stderr, "r %d return am credit for %d, after is %d\n",
	 gc_rank, pe, peer_data[pe].am_credit);
#endif
}

void gc_get_fma_credit(uint32_t pe)
{
  gasneti_atomic_t *p = (gasneti_atomic_t *) &peer_data[pe].fma_credit;
  while (gc_atomic_dec_if_positive(p) < 0) gc_poll_local_queue();
}

void gc_return_fma_credit(uint32_t pe)
{
  gasneti_atomic_increment(&peer_data[pe].fma_credit, GASNETI_ATOMIC_NONE);
}


void gc_queue_init(gc_queue_t *q) 
{
  q->head = NULL;
  q->tail = NULL;
  GASNETC_INITLOCK_QUEUE(q);
}

void gc_queue_item_init(gc_queue_item_t *qi) 
{
  qi->next = NULL;
  qi->queue = NULL;
}

#if 0
gc_queue_item_t *gc_dequeue(gc_queue_t *q, gc_queue_item_t *qi)
{
  gasneti_assert(q != NULL);
  GASNETC_LOCK_QUEUE(q);
  if (qi != NULL) {
    if (qi->prev == NULL) q->head = qi->next;
    else qi->prev->next = qi->next;
    if (qi->next == NULL) q->tail = qi->prev;
    else qi->next->prev = qi->prev;
    GC_DEBUG(qi->next = NULL);
    GC_DEBUG(qi->prev = NULL);
  }
  GASNETC_UNLOCK_QUEUE(q);
  return(qi);
}
#endif 

gc_queue_item_t *gc_queue_pop(gc_queue_t *q)
{
  gc_queue_item_t *qi;
  gasneti_assert(q != NULL);
  GASNETC_LOCK_QUEUE(q);
  qi = q->head;
  if (qi != NULL) {
    gasneti_assert(qi->queue == q);
    q->head = qi->next;
    if (q->head == NULL) q->tail = NULL;
    qi->queue = NULL;
    GC_DEBUG(qi->next = NULL);
  }
  GASNETC_UNLOCK_QUEUE(q);
  return(qi);
}

void gc_queue_push(gc_queue_t *q, gc_queue_item_t *qi)
{
  gasneti_assert(q != NULL);
  gasneti_assert(qi != NULL);
  gasneti_assert(qi->queue == NULL);
  GASNETC_LOCK_QUEUE(q);
  qi->next = q->head;
  q->head = qi;
  if (q->tail == NULL) q->tail = qi;
  qi->queue = q;
  GASNETC_UNLOCK_QUEUE(q);
}

void gc_queue_enqueue(gc_queue_t *q, gc_queue_item_t *qi)
{
  gasneti_assert(q != NULL);
  gasneti_assert(qi != NULL);
  gasneti_assert(qi->queue == NULL);
  GASNETC_LOCK_QUEUE(q);
  if (q->head == NULL) {
    q->head = qi;
  } else {
    gasneti_assert(q->tail != NULL);
    q->tail->next = qi;
  }
  q->tail = qi;
  qi->next = NULL;
  qi->queue = q;
  GASNETC_UNLOCK_QUEUE(q);
}

/* This version is used from smsg_poll inside a loop that holds the lock
 * to amortize locking costs
 */

void gc_queue_enqueue_no_lock(gc_queue_t *q, gc_queue_item_t *qi)
{
  gasneti_assert(q != NULL);
  gasneti_assert(qi != NULL);
  gasneti_assert(qi->queue == NULL);
  //GASNETC_LOCK_QUEUE(q);
  if (q->head == NULL) {
    q->head = qi;
  } else {
    gasneti_assert(q->tail != NULL);
    q->tail->next = qi;
  }
  q->tail = qi;
  qi->next = NULL;
  qi->queue = q;
  // GASNETC_UNLOCK_QUEUE(q);
}

int gc_queue_on_queue(gc_queue_item_t *qi)
{
  gasneti_assert(qi != NULL);
  return (qi->queue != NULL);
}

gasneti_lifo_head_t post_descriptor_pool = GASNETI_LIFO_INITIALIZER;

/* Needs no lock because it is called only from the init code */
void gc_init_post_descriptor_pool(void)
{
  int i;
  gc_post_descriptor_t *data = gc_pd_buffers.addr;
  gasneti_assert_always(data);
  for (i = 0; i < (gc_pd_buffers.size / sizeof(gc_post_descriptor_t)); i += 1) {
    gasneti_lifo_push(&post_descriptor_pool, &data[i]);
  }
}

/* This needs no lock because there is an internal lock in the queue */
gc_post_descriptor_t *gc_alloc_post_descriptor()
{
  gc_post_descriptor_t *gpd;
  while ((gpd = (gc_post_descriptor_t *) 
	  gasneti_lifo_pop(&post_descriptor_pool)) == NULL)
    gc_poll_local_queue();
  gpd->completion = NULL;
  return(gpd);
}


/* This needs no lock because there is an internal lock in the queue */
/* LCS inline this */
void gc_free_post_descriptor(gc_post_descriptor_t *gpd)
{
  gasneti_lifo_push(&post_descriptor_pool, gpd);
}

/* exit related */
volatile int gasnetc_shutdownInProgress = 0;
double gasnetc_shutdown_seconds = 0.0;
double shutdown_max = 10.;  /*  .05 seconds * ln(1000000) */
uint32_t sys_exit_rcvd = 0;


extern void gasnetc_sys_SendShutdownMsg(gasnet_node_t node, uint32_t distance, int exitcode)
{
  gc_sys_shutdown_packet_t shutdown;
  GASNETI_TRACE_PRINTF(C,("Send SHUTDOWN Request to node %d w/ distance %d, exitcode %d",node, distance, exitcode));
  shutdown.header.command = GC_CMD_SYS_SHUTDOWN_REQUEST;
  shutdown.header.numargs = 0;
  shutdown.distance = distance;
  shutdown.exitcode = exitcode;
  gc_send(node, &shutdown, sizeof(gc_sys_shutdown_packet_t), NULL, 0);
}


/* this is called from poll when a shutdown packet arrives */
void gc_handle_sys_shutdown_packet(uint32_t source, gc_sys_shutdown_packet_t *sys)
{
  uint32_t distance = sys->distance;
  int32_t exitcode = sys->exitcode;
  int oldcode;
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
  gasneti_tick_t timeout_us = 1e6 * gasnetc_shutdown_seconds;
  gasneti_tick_t starttime = gasneti_ticks_now();

  GASNETI_TRACE_PRINTF(C,("Entering SYS EXIT"));

  /*  gasneti_assert(portals_sysqueue_initialized); */

  for (distance = 1; distance < gasneti_nodes; distance *= 2) {
    gasnet_node_t peer;

    if (distance >= gasneti_nodes - gasneti_mynode) {
      peer = gasneti_mynode - (gasneti_nodes - distance);
    } else {
      peer = gasneti_mynode + distance;
    }

    oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
    exitcode = MAX(exitcode, oldcode);

    gasnetc_sys_SendShutdownMsg(peer,distance,exitcode);

    /* wait for completion of the proper receive, which might arrive out of order */
    goal |= distance;
    while ((sys_exit_rcvd & goal) != goal) {
      gc_poll();
      if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
        result = 1; /* failure */
        goto out;
      }
    }
  }

out:
  oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
  *exitcode_p = MAX(exitcode, oldcode);

  return result;
}






/* AuxSeg setup for registered bounce buffer space*/
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
    gc_bounce_buffers = auxseg_info[gc_rank];
#if GASNET_DEBUG_VERBOSE
    fprintf(stderr, "got auxseg %p size %ld\n", gc_bounce_buffers.addr,
	    gc_bounce_buffers.size);
#endif
  }

  return retval;
}

/* AuxSeg setup for registered post descriptors*/
gasneti_auxseg_request_t gasnetc_pd_auxseg_alloc(gasnet_seginfo_t *auxseg_info) {
  gasneti_auxseg_request_t retval;
  
  retval.minsz = gasneti_getenv_int_withdefault("GASNETC_GNI_MIN_NUM_PD",
                                                GASNETC_GNI_MIN_NUM_PD_DEFAULT,1)
    * sizeof(gc_post_descriptor_t);
  retval.optimalsz = gasneti_getenv_int_withdefault("GASNETC_GNI_NUM_PD",

                                                    GASNETC_GNI_NUM_PD_DEFAULT,1) 
    * sizeof(gc_post_descriptor_t);
  if (retval.optimalsz < retval.minsz) retval.optimalsz = retval.minsz;
#if GASNET_DEBUG_VERBOSE
  fprintf(stderr, "auxseg post descriptor asking for min  %ld opt %ld\n", retval.minsz, retval.optimalsz);
#endif
  if (auxseg_info == NULL){
    return retval; /* initial query */
  }	
  else { /* auxseg granted */
    /* The only one we care about is our own node */
    gc_pd_buffers = auxseg_info[gc_rank];
#if GASNET_DEBUG_VERBOSE
    fprintf(stderr, "got pd auxseg %p size %ld\n", gc_pd_buffers.addr,
	    gc_pd_buffers.size);
#endif
  }

  return retval;
}

gasneti_lifo_head_t gc_bounce_buffer_pool = GASNETI_LIFO_INITIALIZER;

void gc_init_bounce_buffer_pool(void)
{
  int i;
  int num_bounce;
  gasneti_assert_always(gc_bounce_buffers.addr != NULL);
  gasneti_assert_always(gc_bounce_buffers.size >= GASNETC_GNI_MIN_BOUNCE_SIZE_DEFAULT);
  gasnetc_bounce_register_cutover = 
    gasneti_getenv_int_withdefault("GASNETC_GNI_BOUNCE_REGISTER_CUTOVER",
				   GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_DEFAULT,1);
  if (gasnetc_bounce_register_cutover > GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_MAX)
    gasnetc_bounce_register_cutover = GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_MAX;
  if (gasnetc_bounce_register_cutover > gc_bounce_buffers.size)
    gasnetc_bounce_register_cutover = gc_bounce_buffers.size;
  num_bounce = gc_bounce_buffers.size / gasnetc_bounce_register_cutover;
  for(i = 0; i < num_bounce; i += 1) {
    gasneti_lifo_push(&gc_bounce_buffer_pool, (char *) gc_bounce_buffers.addr + 
		      (gasnetc_bounce_register_cutover * i));
  }
}

void *gc_alloc_bounce_buffer()
{
  void *buf;
  while ((buf = gasneti_lifo_pop(&gc_bounce_buffer_pool)) == NULL) 
	 gc_poll_local_queue();
  return(buf);
}

void gc_free_bounce_buffer(void *gcb)
{
  gasneti_lifo_push(&gc_bounce_buffer_pool, gcb);
}
