#ifndef GC_H
#define GC_H

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <pmi_cray.h>
#include <assert.h>
#include <gni_pub.h>
#include <mpi.h>
#include "gasnet_internal.h"
#include "gasnet_core_internal.h"
#include <gasnet_extended_internal.h>

#define GASNETC_DEBUG 0

#ifdef GASNETC_DEBUG
#define GC_DEBUG(x) x
#define STATS(x) x
#endif
extern int gc_debug;

/* debug support */
#define GNIT_Abort(msg, args...) do {			  \
    fprintf(stderr, "node %d error %s: " msg "\n", gc_rank,	  \
	    gasnett_current_loc, ##args);		  \
    gasnett_print_backtrace(2);				  \
    gasnett_fatalerror("fatalerror");			  \
  } while(0)

#define GNIT_Log(msg, args...) do {			  \
    fprintf(stderr, "node %d log %s: " msg "\n", gc_rank,	  \
	    gasnett_current_loc, ##args);		  \
  } while(0)



/* Set to 1 if want to use gasneti_spinlock_t rather than gasneti_mutex_t
 * in XXX
 * By default, we will use mutex_t.
 * copied from portals conduit
 */
#ifndef GASNETC_USE_SPINLOCK
#define GASNETC_USE_SPINLOCK 0
#endif

#if GASNETC_USE_SPINLOCK
gasneti_atomic_t gni_lock;
#define GASNETC_INITLOCK_GNI() gasneti_spinlock_init(&gni_lock)
#define GASNETC_LOCK_GNI() gasneti_spinlock_lock(&gni_lock)
#define GASNETC_UNLOCK_GNI() gasneti_spinlock_unlock(&gni_lock)



typedef gasneti_atomic_t gasnetc_queuelock_t;
#define GASNETC_INITLOCK_QUEUE(ptr) gasneti_spinlock_init(&((ptr)->lock))
#define GASNETC_TRYLOCK_QUEUE(ptr) gasneti_spinlock_trylock(&((ptr)->lock))
#define GASNETC_LOCK_QUEUE(ptr) gasneti_spinlock_lock(&((ptr)->lock))
#define GASNETC_UNLOCK_QUEUE(ptr) gasneti_spinlock_unlock(&((ptr)->lock))
#define GASNETC_INITLOCK_NODE(srcnode)			\
  gasneti_spinlock_init(&gasnetc_conn_queue[srcnode].lock)
#define GASNETC_TRYLOCK_NODE(srcnode)				\
  gasneti_spinlock_trylock(&gasnetc_conn_queue[srcnode].lock)
#define GASNETC_LOCK_NODE(srcnode)				\
  gasneti_spinlock_lock(&gasnetc_conn_queue[srcnode].lock)
#define GASNETC_UNLOCK_NODE(srcnode)			\
  gasneti_spinlock_unlock(&gasnetc_conn_queue[srcnode].lock)
#else
gasneti_mutex_t gni_lock;
#define GASNETC_INITLOCK_GNI() gasneti_mutex_init(&gni_lock)
#define GASNETC_LOCK_GNI() gasneti_mutex_lock(&gni_lock)
#define GASNETC_UNLOCK_GNI() gasneti_mutex_unlock(&gni_lock)

typedef gasneti_mutex_t gasnetc_queuelock_t;
#define GASNETC_INITLOCK_QUEUE(ptr) gasneti_mutex_init(&((ptr)->lock))
#define GASNETC_TRYLOCK_QUEUE(ptr) gasneti_mutex_trylock(&((ptr)->lock))
#define GASNETC_LOCK_QUEUE(ptr) gasneti_mutex_lock(&((ptr)->lock))
#define GASNETC_UNLOCK_QUEUE(ptr) gasneti_mutex_unlock(&((ptr)->lock))
#define GASNETC_INITLOCK_NODE(srcnode)			\
  gasneti_mutex_init(&gasnetc_conn_queue[srcnode].lock)
#define GASNETC_TRYLOCK_NODE(srcnode)				\
  gasneti_mutex_trylock(&gasnetc_conn_queue[srcnode].lock)
#define GASNETC_LOCK_NODE(srcnode)				\
  gasneti_mutex_lock(&gasnetc_conn_queue[srcnode].lock)
#define GASNETC_UNLOCK_NODE(srcnode)			\
  gasneti_mutex_unlock(&gasnetc_conn_queue[srcnode].lock)
#endif

#define GASNETC_SMSGRELEASE(status, eph)   status = GNI_SmsgRelease(eph); \
  GASNETC_UNLOCK_GNI()

/* create pshm compatible active message token from from address */
#define GC_CREATE_TOKEN(x) ((void *)( ((uint64_t) (x) + 1) << 1))
#define GC_DECODE_TOKEN(x) ((gasnet_node_t)( ((uint64_t) (x) >> 1) - 1))

/* convert from a pointer to a field of a struct to the struct */
#define gasnetc_get_struct_addr_from_field_addr(structname, fieldname, fieldaddr) \
        ((structname*)(((uintptr_t)fieldaddr) - offsetof(structname,fieldname)))


enum GC_CMD {
    GC_CMD_NULL = 12,
    GC_CMD_AM_SHORT,
    GC_CMD_AM_LONG,
    GC_CMD_AM_MEDIUM,
    GC_CMD_AM_MEDIUM_LAST,
    GC_CMD_AM_SHORT_REPLY,
    GC_CMD_AM_LONG_REPLY,
    GC_CMD_AM_MEDIUM_REPLY,
    GC_CMD_AM_NOP_REPLY,
    GC_CMD_SYS_SHUTDOWN_REQUEST   /* from portals-conduit */
};


typedef struct GC_Header {
  uint8_t to;	   	/* debug check  */
  uint8_t command;	  	/* GC_CMD */
  uint8_t numargs;	       /* number of GASNet arguments */
  uint8_t handler;	        /* index of GASNet handler */
  uint32_t from;		/* rank of packet origin */
} GC_Header_t;


/* This type is used to return credits */
typedef struct gc_am_nop_packet {
  GC_Header_t header;
  int sequence;
} gc_am_nop_packet_t;

/* This type is used by an AMShort message or reply */
typedef struct gc_am_short_packet {
  GC_Header_t header;
  int sequence;
  uint32_t args[];
} gc_am_short_packet_t;

typedef struct gc_am_short_packet_max {
  GC_Header_t header;
  int sequence;
  uint32_t args[gasnet_AMMaxArgs()];
} gc_am_short_packet_max_t;


/* The last AMMedium of a sequence has valid handler, and numargs */
#define GC_AM_LAST 1

/* This type is used by an AMMedium message or reply */
typedef struct gc_am_medium_packet {
  GC_Header_t header;
  int sequence;
  size_t data_length;
  uint32_t args[];
} gc_am_medium_packet_t;


typedef struct gc_am_medium_packet_max {
  GC_Header_t header;
  int sequence;
  size_t data_length;
  uint32_t args[gasnet_AMMaxArgs()];
} gc_am_medium_packet_max_t;

/* This type is used by an AMLong message or reply */
typedef struct gc_am_long_packet {
  GC_Header_t header;
  int sequence;
  void *data;
  size_t data_length;
  uint32_t args[];
} gc_am_long_packet_t;

typedef struct gc_am_long_packet_max {
  GC_Header_t header;
  int sequence;
  void *data;
  size_t data_length;
  uint32_t args[gasnet_AMMaxArgs()];
} gc_am_long_packet_max_t;


typedef struct gc_sys_shutdown_packet {
  GC_Header_t header;
  int sequence;
  uint32_t distance;
  uint32_t exitcode;
} gc_sys_shutdown_packet_t;

/* The various ways to interpret an arriving message
 * You can tell what it is by looking at the command field
 * in the GC_Header_t
 */
typedef union gc_eq_packet {
  gc_am_nop_packet_t ganp;
  gc_am_short_packet_max_t gasp;
  gc_am_medium_packet_max_t gamp;
  gc_am_long_packet_max_t galp;
  gc_sys_shutdown_packet_t gssp;
} gc_packet_t;
  



/* Routines in gc_utils.c */

extern uint32_t gc_num_ranks;
extern uint32_t gc_rank;


uint32_t *MPID_UGNI_AllAddr;

void *gather_nic_addresses(void);


int gc_init(gasnet_node_t *sizep, gasnet_node_t *rankp, char **errorstringp);


void GNIT_Job_size(int *nranks);

void GNIT_Rank(int *inst_id);

char GNIT_Ptag(void);

int GNIT_Cookie(void);

int GNIT_Device_Id(void);

void GNIT_Allgather(void *local, long length, void *global);


void GNIT_Finalize();


void GNIT_Barrier();


void gc_get_am_credit(uint32_t pe);
void gc_return_am_credit(uint32_t pe);
void gc_get_fma_credit(uint32_t pe);
void gc_return_fma_credit(uint32_t pe);

void gc_send_am_nop(uint32_t pe);


typedef struct gc_queue_item {
  struct gc_queue_item *next;    // pointer to next item on q, or NULL if last
  //  struct gc_queue_item *prev;
  struct gc_queue *queue;        // pointer to queue we are on, or NULL if not
} gc_queue_item_t;


typedef struct gc_queue {
  gc_queue_item_t *head;
  gc_queue_item_t *tail;
  gasnetc_queuelock_t lock;
} gc_queue_t;

void gc_queue_init(gc_queue_t *q);
void gc_queue_item_init(gc_queue_item_t *qi);

gc_queue_item_t *gc_queue_pop(gc_queue_t *q);
void gc_queue_push(gc_queue_t *q, gc_queue_item_t *qi);
void gc_queue_enqueue(gc_queue_t *q, gc_queue_item_t *qi);
void gc_queue_enqueue_no_lock(gc_queue_t *q, gc_queue_item_t *qi);

void gc_init_post_descriptor_pool(void);

/* use the auxseg mechanism to allocate registered memory for bounce buffers */
/* we want at least this many post descriptors */
#define GASNETC_GNI_MIN_NUM_PD_DEFAULT 4096
/* and preferably this much space */
#define GASNETC_GNI_NUM_PD_DEFAULT (4096 * 4)
/* we want at least this much space for bounce buffers */
#define GASNETC_GNI_MIN_BOUNCE_SIZE_DEFAULT 65536
/* and preferably this much space */
#define GASNETC_GNI_BOUNCE_SIZE_DEFAULT (65536 * 4)
/* a particular message up to this size goes via bounce */
#define GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_DEFAULT 8192
#define GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_MAX 32768
/* a particular message up to this size goes via fma */
#define GASNETC_GNI_FMA_RDMA_CUTOVER_DEFAULT 128
#define GASNETC_GNI_FMA_RDMA_CUTOVER_MAX 128 /* bloats descriptor storage */

gasnet_seginfo_t gc_bounce_buffers;   /* fields addr and size */
gasnet_seginfo_t gc_pd_buffers;   /* fields addr and size */

void  *gc_alloc_bounce_buffer();
void gc_free_bounce_buffer(void *buf);


void gc_init_bounce_buffer_pool(void);
uint32_t gasnetc_bounce_register_cutover;
uint32_t gasnetc_fma_rdma_cutover;

#define GC_POST_SEND 1
#define GC_POST_UNBOUNCE 2
#define GC_POST_UNREGISTER 4
#define GC_POST_COPY 8
#define GC_POST_INC 16
#define GC_POST_GET 32
#define GC_POST_COMPLETION_EOP 64
#define GC_POST_COMPLETION_FLAG 128

typedef struct gc_post_descriptor {
  gc_queue_item_t qi;  /* not needed XXX */
  void *bounce_buffer;
  void *get_target;
  uint32_t get_nbytes;
  uint32_t flags;
  gasnet_node_t dest;
  gni_mem_handle_t mem_handle;
  gasnete_op_t *completion;
  gni_post_descriptor_t pd;
  union {
    gc_am_long_packet_max_t galp;
    char immediate[GASNETC_GNI_FMA_RDMA_CUTOVER_MAX];
  } u;
} gc_post_descriptor_t;

gc_post_descriptor_t *gc_alloc_post_descriptor();
void gc_free_post_descriptor(gc_post_descriptor_t *pd);

/* default fraction of phys mem to assume is pinnable under CNL */
#ifndef GASNETC_DEFAULT_PHYSMEM_PINNABLE_RATIO
#define GASNETC_DEFAULT_PHYSMEM_PINNABLE_RATIO 0.75
#endif

/* exit related */
int gasnetc_exitcode;
volatile int gasnetc_shutdownInProgress;
double gasnetc_shutdown_seconds; /* number of seconds to poll before forceful shutdown */
double shutdown_max; 
int gasnetc_sys_exit(int *exitcode);
uint32_t sys_exit_rcvd;  /* bitmap of messages received during dissemination */



int gc_init(gasnet_node_t *sizep, gasnet_node_t *rankp, char **errstringp);
void gc_init_segment(void *segment_start, size_t segment_size);
void gc_init_messaging(void);
void gc_shutdown(void); /* clean up all gni state */


void gc_poll_local_queue(void);
void gc_poll(void);

int gc_send(gasnet_node_t dest, 
	     void *header, int header_length, 
	    void *data, int data_length);

void gc_rdma_put(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gc_post_descriptor_t *gpd);

void gc_rdma_get(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gc_post_descriptor_t *gpd);

#endif
