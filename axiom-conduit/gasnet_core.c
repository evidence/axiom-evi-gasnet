/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core.c $
 * Description: GASNet axiom conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include "axiom_nic_api_user.h"
#include "axiom_nic_packets.h"
#include "axiom_nic_init.h"
#include "axiom_nic_types.h"
#include "axiom_run_api.h"

#define _BLOCKING_MODE 1

// enable/disable internal conduit logging
#if 0
#include <sys/time.h>
#include <stdio.h>
#include <stdarg.h>
typedef enum { LOG_NOLOG = -1, LOG_FATAL = 0, LOG_ERROR = 1, LOG_WARN = 2, LOG_INFO = 3,  LOG_DEBUG = 4, LOG_TRACE = 5  } logmsg_level_t;
static const char *logmsg_name[] = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
static const logmsg_level_t logmsg_level=LOG_TRACE;
#define logmsg_is_enabled(lvl) ((lvl)<=logmsg_level)
#define logmsg(lvl, msg, ...) {\
  if (logmsg_is_enabled(lvl)) {\
    struct timespec _t0;\
    clock_gettime(CLOCK_REALTIME_COARSE,&_t0);\
    _logmsg("[%5d.%06d] %5s{%d}: " msg "\n", (int)(_t0.tv_sec % 10000), (int)_t0.tv_nsec/1000, logmsg_name[lvl], (int)getpid(), ##__VA_ARGS__);\
  }\
}
static void _logmsg(const char *msg, ...) __attribute__((format(printf, 1, 2)));
static inline void _logmsg(const char *msg, ...) {
    va_list list;
    va_start(list, msg);
    vfprintf(stderr, msg, list);
    va_end(list);
}
#else
#define logmsg_is_enabled(lvl) 0
#define logmsg(lvl,msg,...) 
#endif

#if GASNET_PSHM
// paranoia
#error AXIOM conduit does not support PSHM
#endif
#ifdef GASNET_SEGMENT_EVERYTHING
//paranoia
#error AXIOM conduit does not support SEGMENT_EVERYTHING
#endif

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_Name, "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable(void);
#if HAVE_ON_EXIT
static void gasnetc_on_exit(int, void*);
#else
static void gasnetc_atexit(void);
#endif

gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table (recommended impl) */

// commands (for axiom raw messages)
#define GASNETC_AM_REQ_MESSAGE 128
#define GASNETC_AM_REPLY_MESSAGE 129
#define GASNETC_RDMA_MESSAGE 130

typedef struct gasnetc_axiom_am_header {
    uint8_t command;
    uint8_t category;
    uint8_t handler_id;
    uint8_t numargs;
    uint32_t offset;
    uint32_t size;
    uint8_t data[GASNETC_ALIGN_SIZE]; // need if not using rdma_message!!!!
} __attribute__((__packed__)) gasnetc_axiom_am_header_t;

#ifndef GASNET_AXIOM_AM_MSG_HEADER_SIZE
#define GASNET_AXIOM_AM_MSG_HEADER_SIZE sizeof(gasnetc_axiom_am_header_t)
#endif
#if GASNET_AXIOM_AM_MSG_HEADER_SIZE!=20
#error GASNET_AXIOM_AM_MSG_HEADER_SIZE defined into gasnet_core.c must be equal to gasnet_core.h
#endif

typedef struct gasnetc_axiom_am_msg {
    gasnetc_axiom_am_header_t head;
    gasnet_handlerarg_t args[GASNET_AXIOM_AM_MAX_NUM_ARGS];
} __attribute__((__packed__)) gasnetc_axiom_am_msg_t;

        
#define compute_payload_size(numargs) (GASNET_AXIOM_AM_MSG_HEADER_SIZE+sizeof(gasnet_handlerarg_t)*(numargs))
//#define compute_aligned_payload_size(numargs) (compute_payload_size(numargs)+((numargs&0x1)?0:4))    
static inline size_t compute_aligned_payload_size(int numargs) {
    register size_t sz=compute_payload_size(numargs);
    return ((sz&(GASNETI_MEDBUF_ALIGNMENT-1))==0)?sz:((sz&~(GASNETI_MEDBUF_ALIGNMENT-1))+GASNETI_MEDBUF_ALIGNMENT);
}

typedef struct gasnetc_axiom_generic_msg {
    uint8_t command;
} __attribute__((__packed__)) gasnetc_axiom_generic_msg_t;

typedef struct gasnetc_axiom_msg {
    union {
        gasnetc_axiom_generic_msg_t gen;
        gasnetc_axiom_am_msg_t am;
        uint8_t buffer[AXIOM_LONG_PAYLOAD_MAX_SIZE];
    } __attribute__((__packed__));
} __attribute__((__packed__)) gasnetc_axiom_msg_t;

typedef struct gasnetc_axiom_am_info {
    gasnet_node_t node;
    axiom_port_t port;
    int isReq;
} gasnetc_axiom_am_info_t;

#define gasnetc_get_handler(_h) (gasnetc_handler[(_h)])

//#if GASNETC_HSL_SPINLOCK
#if 0
#define LOCK(x) \
    do { \
        if_pf(gasneti_mutex_trylock(&((x).lock)) == EBUSY) {\
            if (gasneti_wait_mode == GASNET_WAIT_SPIN) {\
                while (gasneti_mutex_trylock(&((x).lock)) == EBUSY) {\
                    gasneti_compiler_fence();\
                    gasneti_spinloop_hint();\
                }\
            } else {\
                gasneti_mutex_lock(&((x).lock));\
            }\
        }\
    } while (0)
#else
#define LOCK(x) \
    gasneti_mutex_lock(&((x).lock))
#endif
#define UNLOCK(x) \
    gasneti_mutex_unlock(&((x).lock))
#define INIT_MUTEX(x) \
    gasneti_mutex_init(&((x).lock))
#define MUTEX_t gasnet_hsl_t

#define INIT_COND(x) \
    gasneti_cond_init(&(x))
#define SIGNAL_COND(x) \
    gasneti_cond_signal(&(x))
#define WAIT_COND(x,m) \
    gasneti_cond_wait(&(x),&(m.lock))
#define COND_t gasneti_cond_t

/*
  Initialization
  ==============
 */

#define AXIOM_BIND_PORT 7

static axiom_dev_t *axiom_dev;

#define INVALID_PHYSICAL_NODE 255
#define INVALID_LOGICAL_NODE -1

static axiom_node_id_t *gasnetc_nodes_log2phy = NULL;
static gasnet_node_t *gasnetc_nodes_phy2log = NULL;
static int num_job_nodes;
static int num_phy_nodes;

static inline axiom_node_id_t node_log2phy(gasnet_node_t node) {
    gasneti_assert(node >= 0 && node < num_job_nodes);
    return gasnetc_nodes_log2phy[node];
}

static inline gasnet_node_t node_phy2log(axiom_node_id_t node) {
    gasneti_assert(node >= 0 && node < num_phy_nodes);
    gasneti_assert(gasnetc_nodes_phy2log[node] != INVALID_LOGICAL_NODE);
    return gasnetc_nodes_phy2log[node];
}

#ifndef _BLOCKING_MODE

#error not tested

//
#define SEND_RETRY 42
// usec
#define SEND_DELAY 5000

static inline axiom_msg_id_t _send_raw(axiom_dev_t *dev, axiom_node_id_t dst_id, axiom_port_t port, axiom_type_t type, axiom_raw_payload_size_t payload_size, void *payload) {
    axiom_err_t ret;
    int counter = 0;
    while (counter < SEND_RETRY) {
        ret = axiom_send_raw(axiom_dev, dst_id, port, type, payload_size, payload);
        if (ret != AXIOM_RET_NOTAVAIL) break;
        usleep(SEND_DELAY);
        counter++;
    }
    return ret;
}

//
#define RECV_RETRY 42
// usec
#define RECV_DELAY 25000

static inline axiom_msg_id_t axiom_recv_raw(axiom_dev_t *dev, axiom_node_id_t *src_id, axiom_port_t *port, axiom_type_t *type, axiom_raw_payload_size_t *payload_size, void *payload) {
    axiom_err_t ret;
    int counter = 0;
    while (counter < SEND_RETRY) {
        ret = axiom_recv_raw(axiom_dev, src_id, port, type, payload_size, payload);
        if (ret != AXIOM_RET_NOTAVAIL) break;
        usleep(SEND_DELAY);
        counter++;
    }
    return ret;
}

#else

static inline int _recv_avail(axiom_dev_t *dev) {
    int res;
    res = axiom_recv_raw_avail(axiom_dev);
    if (!res) {
        res = axiom_recv_long_avail(axiom_dev);
    }
    return res;
}

static inline axiom_msg_id_t _send_raw(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, axiom_raw_payload_size_t payload_size, void *payload) {
    return axiom_send_raw(axiom_dev, node_log2phy(node_id), port, AXIOM_TYPE_RAW_DATA, payload_size, payload);
}

static inline axiom_msg_id_t _send_long(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, axiom_long_payload_size_t payload_size, void *payload)
{
 /*
    while (!axiom_send_long_avail(axiom_dev)) {
        gasneti_AMPoll();
    }
*/
    return axiom_send_long(axiom_dev, node_log2phy(node_id), port, payload_size, payload);
}

static inline axiom_msg_id_t _send_long_iov(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, struct iovec *iov, int iovcnt)
{
    axiom_long_payload_size_t payload_size=0;
    register int i;
    for (i=0;i<iovcnt;i++)
        payload_size+=iov[i].iov_len;
/*
    while (!axiom_send_long_avail(axiom_dev)) {
        gasneti_AMPoll();
    }    
*/
    return axiom_send_iov_long(axiom_dev, node_log2phy(node_id), port, payload_size, iov,iovcnt);
}

static inline axiom_msg_id_t _recv_raw(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, axiom_raw_payload_size_t *payload_size, void *payload) {
    axiom_type_t type = AXIOM_TYPE_RAW_DATA;
    axiom_node_id_t src_id;
    axiom_msg_id_t id = axiom_recv_raw(axiom_dev, &src_id, port, &type, payload_size, payload);
    *node_id = node_phy2log(src_id);
    return id;
}

static inline axiom_msg_id_t _recv_long(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, axiom_long_payload_size_t *payload_size, void *payload)
{
    axiom_node_id_t src_id;
    axiom_msg_id_t id = axiom_recv_long(axiom_dev, &src_id, port, payload_size, payload);
    *node_id = node_phy2log(src_id);
    return id;
}

static inline axiom_msg_id_t _recv(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, size_t *payload_size, void *payload)
{
    axiom_type_t type;
    axiom_node_id_t src_id;
    axiom_msg_id_t id = axiom_recv(axiom_dev, &src_id, port, &type, payload_size, payload);
    *node_id = node_phy2log(src_id);
    return id;
}

static inline axiom_err_t _rdma_write(axiom_dev_t *dev, gasnet_node_t node_id, axiom_rdma_payload_size_t size, void *source_addr, void *dest_addr) {
    gasneti_assert((uintptr_t) source_addr >= (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma);
    gasneti_assert((uintptr_t) dest_addr >= (uintptr_t) gasneti_seginfo[node_id].rdma);
    return axiom_rdma_write(axiom_dev, node_log2phy(node_id), 0, size, (uintptr_t) source_addr - (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma, (uintptr_t) dest_addr - (uintptr_t) gasneti_seginfo[node_id].rdma);
}

#endif

static void my_signal_handler(int sig);

static void init_signal_manager() {
    struct sigaction sa;
    memset(&sa, 0, sizeof (sa));
    sa.sa_handler = my_signal_handler;
    if_pf(sigaction(SIGQUIT, &sa, NULL) != 0) gasneti_fatalerror("Installing %s signal handler!", "SIGSEGV");
}

/* ------------------------------------------------------------------------------------ */

#define BARRIER_ID 7

static void gasnetc_bootstrapBarrier(void) {
    int res;
    res = axrun_sync(BARRIER_ID, 1);
    if_pf(res != 0) {
        gasneti_fatalerror("failure in gasnetc_bootstrapBarrier() using axrun_sync()");
    }
}

/* called at startup to check configuration sanity */
static void gasnetc_check_config(void) {
    gasneti_check_config_preinit();

    /* (###) add code to do some sanity checks on the number of nodes, handlers
     * and/or segment sizes */
}

#if GASNET_PSHM /* Used only in call to gasneti_pshm_init() */
/* (###) add code here to perform a supernode scoped broadcast.
   This call performs an independent broadcast within each supernode.
   The implementation can identify the supernode using the variables
   initialized by gasneti_nodemapInit(), or by the identical values
   passed for "rootnode".
   The implementation cannot use pshmnet.
   This is called collectively (currently exactly once in gasneti_pshm_init()).
 */

/* Naive (poorly scaling) "reference" implementation via gasnetc_bootstrapExchange() */
static void gasnetc_bootstrapSNodeBroadcast(void *src, size_t len, void *dest, int rootnode) {
    void *tmp = gasneti_malloc(len * gasneti_nodes);
    gasneti_assert(NULL != src);
    gasnetc_bootstrapExchange(src, len, tmp);
    memcpy(dest, (void*) ((uintptr_t) tmp + (len * rootnode)), len);
    gasneti_free(tmp);
}
#endif

extern char **environ;

static int send_to_all(void *packet, axiom_port_t port, axiom_raw_payload_size_t packet_size, gasnet_node_t mynode, gasnet_node_t num_nodes) {
    gasnet_node_t node;
    axiom_err_t ret;
    for (node = 0; node < num_nodes; node++) {
        if (node == mynode) continue;
        ret = _send_raw(axiom_dev, node, port, packet_size, packet);
        if (ret != AXIOM_RET_OK) break;
    }
    return (ret == AXIOM_RET_OK) ? GASNET_OK : -1;
}

static inline char *_strncpy(char *dest, const char *src, size_t n) {
    char *res = strncpy(dest, src, n);
    dest[n - 1] = '\0';
    return res;
}

extern size_t strlcpy(char *dst, const char *src, size_t size);

static int gasneti_bootstrapInit(int *argc_p, char ***argv_p) {
    char *s;
    uint64_t job_nodes;
    int nl, nf;
    int i;

    s = getenv("AXIOM_RUN");
    if (s == NULL) {
        gasneti_fatalerror("a gasnet axiom conduit application must be run throught axiom-run");
    }
    num_job_nodes = atoi(s);
    s = getenv("AXIOM_NODES");
    if (s == NULL) {
        gasneti_fatalerror("no AXIOM_NODES environment variable found");
    }
    job_nodes = strtol(s, NULL, 0);
    gasnetc_nodes_log2phy = (axiom_node_id_t*) gasneti_malloc(sizeof (axiom_node_id_t) * num_job_nodes);
    gasneti_assert(gasnetc_nodes_log2phy != NULL);
    num_phy_nodes = axiom_get_num_nodes(axiom_dev);
    gasnetc_nodes_phy2log = (gasnet_node_t*) gasneti_malloc(sizeof (gasnet_node_t) * num_phy_nodes);
    gasneti_assert(gasnetc_nodes_log2phy != NULL);
    for (i = 0; i < num_phy_nodes; i++) gasnetc_nodes_phy2log[i] = INVALID_LOGICAL_NODE;
    nf = nl = 0;
    while (job_nodes != 0) {
        if (job_nodes & 0x1) {
            gasneti_assert(nl < num_job_nodes);
            gasnetc_nodes_log2phy[nl] = nf;
            gasneti_assert(nf < num_phy_nodes);
            gasnetc_nodes_phy2log[nf] = nl;
            nl++;
        }
        job_nodes >>= 1;
        nf++;
    }
    if (nl != num_job_nodes) {
        gasneti_fatalerror("AXIOM_RUN and AXIOM_NODES environment variables mismatch");
    }
    gasneti_nodes = num_job_nodes;
    gasneti_mynode = gasnetc_nodes_phy2log[axiom_get_node_id(axiom_dev)];
    gasneti_assert(gasneti_mynode != INVALID_LOGICAL_NODE);

    return GASNET_OK;
}

static MUTEX_t poll_mutex;

static int gasnetc_mmap_done=0;

static int gasnetc_init(int *argc, char ***argv) {

    axiom_err_t ret;
    int res;

    /*  check system sanity */
    gasnetc_check_config();

    if (gasneti_init_done)
        GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

    gasneti_freezeForDebugger();

#if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr, "gasnetc_init(): about to spawn...\n");
    fflush(stderr);
#endif

    /* (###) add code here to bootstrap the nodes for your conduit */
    //gasneti_mutex_init(&(poll_mutex.lock));
    INIT_MUTEX(poll_mutex);
    init_signal_manager();
    {
#ifdef _BLOCKING_MODE
        axiom_dev = axiom_open(NULL);
#else
        struct axiom_args openargs;
        openargs.flags = AXIOM_FLAG_NOBLOCK;
        axiom_dev = axiom_open(&openargs);
#endif
    }

    if_pf(axiom_dev == NULL) {
        char s[255];
        snprintf(s, sizeof (s), "Can NOT open axiom device.");
        GASNETI_RETURN_ERRR(RESOURCE, s);
    }

    ret = axiom_bind(axiom_dev, AXIOM_BIND_PORT);

    if_pf(!AXIOM_RET_IS_OK(ret)) {
        char s[255];
        snprintf(s, sizeof (s), "Can NOT bind axiom device to port %d.", AXIOM_BIND_PORT);
        GASNETI_RETURN_ERRR(RESOURCE, s);
    }

    // flush message queue
    //
    ret = axiom_flush_raw(axiom_dev);
    if_pf(ret == AXIOM_RET_ERROR) {
        char s[255];
        snprintf(s, sizeof (s), "Error flushing axiom 'raw' input queue (ret=%d)", ret);
        GASNETI_RETURN_ERRR(RESOURCE, s);
    }
    ret = axiom_flush_long(axiom_dev);
    if_pf(ret == AXIOM_RET_ERROR) {
        char s[255];
        snprintf(s, sizeof (s), "Error flushing axiom 'long' input queue (ret=%d)", ret);
        GASNETI_RETURN_ERRR(RESOURCE, s);
    }

    res = gasneti_bootstrapInit(argc, argv);
    if (res != GASNET_OK) {
        return res;
    }

#if GASNET_DEBUG_VERBOSE
    fprintf(stderr, "gasnetc_init(): spawn successful - node %i/%i starting...\n",
            gasneti_mynode, gasneti_nodes);
    fflush(stderr);
#endif

    /* (###) Add code here to determine which GASNet nodes may share memory.
       The collection of nodes sharing memory are known as a "supernode".
       The (first) data structure to describe this is gasneti_nodemap[]:
          For all i: gasneti_nodemap[i] is the lowest node number collocated w/ node i
       where nodes are considered collocated if they have the same node "ID".
       Or in English:
         "gasneti_nodemap[] maps from node to first node on the same supernode."

       If the conduit has already communicated endpoint address information or
       a similar identifier that is unique per shared-memory compute node, then
       that info can be passed via arguments 2 through 4.
       Otherwise the conduit should pass a non-null gasnetc_bootstrapExchange
       as argument 1 to use platform-specific IDs, such as gethostid().
       See gasneti_nodemapInit() in gasnet_internal.c for more usage documentation.
       See below for info on gasnetc_bootstrapExchange()

       If the conduit can build gasneti_nodemap[] w/o assistance, it should
       call gasneti_nodemapParse() after constructing it (instead of nodemapInit()).
     */
    gasneti_nodemapInit(NULL, NULL, 0, 0); // trivial map i.e. (node->supernode) 0->0 1->1 ecc...

#if GASNET_PSHM
    /* (###) If your conduit will support PSHM, you should initialize it here.
     * The 1st argument is normally "&gasnetc_bootstrapSNodeBroadcast" or equivalent
     * The 2nd argument is the amount of shared memory space needed for any
     * conduit-specific uses.  The return value is a pointer to the space
     * requested by the 2nd argument.
     */
### = gasneti_pshm_init(###, ###);
#endif

#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    {
        /* (###) Add code here to determine optimistic maximum segment size */
        gasneti_MaxLocalSegmentSize = AXIOM_MAX_SEGMENT_SIZE;

        /* (###) Add code here to find the MIN(MaxLocalSegmentSize) over all nodes */
        gasneti_MaxGlobalSegmentSize = AXIOM_MAX_SEGMENT_SIZE;

        /* it may be appropriate to use gasneti_segmentInit() here to set 
           gasneti_MaxLocalSegmentSize and gasneti_MaxGlobalSegmentSize,
           if your conduit can use memory anywhere in the address space
           (you may want to tune GASNETI_MMAP_MAX_SIZE to limit the max size)

           it may also be appropriate to first call gasneti_mmapLimit() to
           get a good value for the first argument to gasneti_segmentInit(), to
           account for limitations imposed by having multiple GASNet nodes
           per shared-memory compute node (this is recommended for all
           systems with virtual memory unless there can be only one
           process per compute node).
         */
        //gasneti_segmentInit(AXIOM_MAX_SEGMENT_SIZE,); da usare solo se non si usano le definizioni precedenti!!!!!
    }
#elif GASNET_SEGMENT_EVERYTHING
    /* segment is everything - nothing to do */
#else
    //#error Bad segment config
#endif

#if 0
    /* Enable this if you wish to use the default GASNet services for broadcasting 
        the environment from one compute node to all the others (for use in gasnet_getenv(),
        which needs to return environment variable values from the "spawning console").
        You need to provide two functions (gasnetc_bootstrapExchange and gasnetc_bootstrapBroadcast)
        which the system can safely and immediately use to broadcast and exchange information 
        between nodes (gasnetc_bootstrapBroadcast is optional but highly recommended).
        See gasnet/other/mpi-spawner/gasnet_bootstrap_mpi.c for definitions of these two
        functions in terms of MPI collective operations.
       This system assumes that at least one of the compute nodes has a copy of the 
        full environment from the "spawning console" (if this is not true, you'll need to
        implement something yourself to get the values from the spawning console)
       If your job system already always propagates environment variables to all the compute
        nodes, then you probably don't need this.
     */
    gasneti_setupGlobalEnvironment(gasneti_nodes, gasneti_mynode,
            gasnetc_bootstrapExchange, gasnetc_bootstrapBroadcast);
#endif

    gasneti_init_done = 1;

    gasneti_auxseg_init(); /* adjust max seg values based on auxseg */

    return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
    int retval = gasnetc_init(argc, argv);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasneti_trace_init(argc, argv);

    return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
static char checkuniqhandler[256] = {0};

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
                snprintf(s, sizeof (s), "Too many handlers. (limit=%i)", highlimit - lowlimit + 1);
                GASNETI_RETURN_ERRR(BAD_ARG, s);
            }
        }

        /*  ensure handlers fall into the proper range of pre-assigned values */
        if (newindex < lowlimit || newindex > highlimit) {
            char s[255];
            snprintf(s, sizeof (s), "handler index (%i) out of range [%i..%i]", newindex, lowlimit, highlimit);
            GASNETI_RETURN_ERRR(BAD_ARG, s);
        }

        /* discover duplicates */
        if (checkuniqhandler[newindex] != 0)
            GASNETI_RETURN_ERRR(BAD_ARG, "handler index not unique");
        checkuniqhandler[newindex] = 1;

        /* register the handler */
        gasnetc_handler[(gasnet_handler_t) newindex] = (gasneti_handler_fn_t) table[i].fnptr;

        /* The check below for !table[i].index is redundant and present
         * only to defeat the over-aggressive optimizer in pathcc 2.1
         */
        if (dontcare && !table[i].index) table[i].index = newindex;

        (*numregistered)++;
    }
    return GASNET_OK;
}

#if GASNETC_NUM_BUFFERS<=32

static volatile uint32_t rdma_buf_state = 0xffffffff;

#define ONE ((uint32_t)1)

/**
 * Find the number of zero bits on the left of the parameter.
 * @param n the parameter
 * @return the number of ZERO bits on the left
 */
static inline int __clz(register uint32_t n) {
    if (n == 0) return 32;
    return __builtin_clz(n);
}

/**
 * Find the number of zero bits on the right of the parameter.
 * @param n the parameter
 * @return the number of ZERO bits on the left
 */
static inline int __ctz(register uint32_t n) {
    if (n == 0) return 32;
    return __builtin_ctz(n);
}

#elif GASNETC_NUM_BUFFERS<=64

static volatile uint64_t rdma_buf_state = 0xffffffffffffffff;

#define ONE ((uint64_t)1)

/**
 * Find the number of zero bits on the left of the parameter.
 * @param n the parameter
 * @return the number of ZERO bits on the left
 */
static inline int __clz(register uint64_t n) {
    // warning: GCC __builtin_clz() does not work with uint64_t
    register int c = 0;
    if (n == 0) return 64;
    while (n != 0) {
        if (n & 0x8000000000000000) break;
        n <<= 1;
        c++;
    }
    return c;
}

/**
 * Find the number of zero bits on the right of the parameter.
 * @param n the parameter
 * @return the number of ZERO bits on the left
 */
static inline int __ctz(register uint64_t n) {
    // warning: GCC __builtin_ctz() does not work with uint64_t
    register int c = 0;
    if (n == 0) return 64;
    while (n != 0) {
        if (n & 0x1) break;
        n >>= 1;
        c++;
    }
    return c;
}

#else
#error GASNETC_NUM_BUFFERS must be <=64
#endif

static MUTEX_t rdma_buf_mutex;
static COND_t rdma_buf_cond;

static void init_rdma_buf() {
    INIT_MUTEX(rdma_buf_mutex);
    INIT_COND(rdma_buf_cond);
}

static void *alloca_rdma_buf() {
    int idx;
    LOCK(rdma_buf_mutex);
    idx = __ctz(rdma_buf_state);
    while (idx >= GASNETC_NUM_BUFFERS) {
        WAIT_COND(rdma_buf_cond, rdma_buf_mutex);
        idx = __ctz(rdma_buf_state);
    }
    gasneti_assert(idx < GASNETC_NUM_BUFFERS);
    rdma_buf_state &= ~(ONE << idx);
    UNLOCK(rdma_buf_mutex);
    return (uint8_t*) gasneti_seginfo[gasneti_mynode].base + idx*GASNETC_BUFFER_SIZE;
}

static void free_rdma_buf(void *buf) {
    int idx = ((uint8_t*) buf - (uint8_t*) gasneti_seginfo[gasneti_mynode].base) / GASNETC_BUFFER_SIZE;
    LOCK(rdma_buf_mutex);
    rdma_buf_state |= (ONE << idx);
    SIGNAL_COND(rdma_buf_cond);
    UNLOCK(rdma_buf_mutex);
}

/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
        uintptr_t segsize, uintptr_t minheapoffset) {
    void *segbase = NULL;

    GASNETI_TRACE_PRINTF(C, ("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
            numentries, (unsigned long) segsize, (unsigned long) minheapoffset));

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
#else
    segsize = 0;
    minheapoffset = 0;
#endif

    segsize = gasneti_auxseg_preattach(segsize); /* adjust segsize for auxseg reqts */

    /* ------------------------------------------------------------------------------------ */
    /*  register handlers */
    {
        int i;
        for (i = 0; i < GASNETC_MAX_NUMHANDLERS; i++)
            gasnetc_handler[i] = (gasneti_handler_fn_t) & gasneti_defaultAMHandler;
    }
    { /*  core API handlers */
        gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *) gasnetc_get_handlertable();
        int len = 0;
        int numreg = 0;
        gasneti_assert(ctable);
        while (ctable[len].fnptr) len++; /* calc len */
        if (gasnetc_reghandlers(ctable, len, GASNETC_HANDLER_BASE, GASNETE_HANDLER_BASE - 1, 0, &numreg) != GASNET_OK)
            GASNETI_RETURN_ERRR(RESOURCE, "Error registering core API handlers");
        gasneti_assert(numreg == len);
    }

    { /*  extended API handlers */
        gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *) gasnete_get_handlertable();
        int len = 0;
        int numreg = 0;
        gasneti_assert(etable);
        while (etable[len].fnptr) len++; /* calc len */
        if (gasnetc_reghandlers(etable, len, GASNETE_HANDLER_BASE, GASNETU_HANDLER_BASE - 1, 0, &numreg) != GASNET_OK)
            GASNETI_RETURN_ERRR(RESOURCE, "Error registering extended API handlers");
        gasneti_assert(numreg == len);
    }

    if (table) { /*  client handlers */
        int numreg1 = 0;
        int numreg2 = 0;

        /*  first pass - assign all fixed-index handlers */
        if (gasnetc_reghandlers(table, numentries, GASNETU_HANDLER_BASE, GASNETC_MAX_NUMHANDLERS - 1, 0, &numreg1) != GASNET_OK)
            GASNETI_RETURN_ERRR(RESOURCE, "Error registering fixed-index client handlers");

        /*  second pass - fill in dontcare-index handlers */
        if (gasnetc_reghandlers(table, numentries, GASNETU_HANDLER_BASE, GASNETC_MAX_NUMHANDLERS - 1, 1, &numreg2) != GASNET_OK)
            GASNETI_RETURN_ERRR(RESOURCE, "Error registering fixed-index client handlers");

        gasneti_assert(numreg1 + numreg2 == numentries);
    }

    /* ------------------------------------------------------------------------------------ */
    /*  register fatal signal handlers */

    /* catch fatal signals and convert to SIGQUIT */
    gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

    /*  (###) register any custom signal handlers required by your conduit 
     *        (e.g. to support interrupt-based messaging)
     */

#if HAVE_ON_EXIT
    on_exit(gasnetc_on_exit, NULL);
#else
    atexit(gasnetc_atexit);
#endif

    /* ------------------------------------------------------------------------------------ */
    /*  register segment  */

    gasneti_seginfo = (gasnet_seginfo_t *) gasneti_malloc(gasneti_nodes * sizeof (gasnet_seginfo_t));
    gasneti_leak(gasneti_seginfo);

#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    if (segsize == 0) segbase = NULL; /* no segment */
    else {
        /* (###) add code here to choose and register a segment 
           (ensuring alignment across all nodes if this conduit sets GASNET_ALIGNED_SEGMENTS==1) 
           you can use gasneti_segmentAttach() here if you used gasneti_segmentInit() above
         */
        uint64_t mysize;
        int delta;
        gasnet_node_t i;
        void *rdmabase;

        // MG
        gasneti_assert(GASNET_PAGESIZE % GASNETC_ALIGN_SIZE == 0);
        rdmabase = segbase = axiom_rdma_mmap(axiom_dev, &mysize);
        if (segbase == NULL)
            gasneti_fatalerror("axiom_rdma_mmap() return NULL");
        gasnetc_mmap_done=1;
        delta = 0;
        if (((unsigned long) segbase) % GASNET_PAGESIZE != 0) {
            uint8_t *newbase = (uint8_t*) ((((unsigned long) segbase) | (GASNET_PAGESIZE - 1)) + 1);
            delta = newbase - (uint8_t*) segbase;
            segbase = newbase;
        }
        segsize = (mysize - delta)&(~(uint64_t) (GASNET_PAGESIZE - 1));

        gasneti_assert(segsize > GASNETC_RESERVED_SPACE);
        for (i = 0; i < gasneti_nodes; i++) {
            gasneti_seginfo[i].rdma = (void *) rdmabase;
            gasneti_seginfo[i].rdmasize = mysize;
            gasneti_seginfo[i].base = (void *) segbase;
            //
            gasneti_seginfo[i].addr = (void *) ((uint8_t*) segbase + GASNETC_RESERVED_SPACE);
            gasneti_seginfo[i].size = (uintptr_t) segsize - GASNETC_RESERVED_SPACE;
        }
        init_rdma_buf();
        
        gasneti_assert(((uintptr_t) segbase) % GASNET_PAGESIZE == 0);
        gasneti_assert(segsize % GASNET_PAGESIZE == 0);
    }
#else
    { /* GASNET_SEGMENT_EVERYTHING */
        gasnet_node_t i;
        for (i = 0; i < gasneti_nodes; i++) {
            gasneti_seginfo[i].addr = (void *) 0;
            gasneti_seginfo[i].size = (uintptr_t) - 1;
        }
        segbase = (void *) 0;
        segsize = (uintptr_t) - 1;
        /* (###) add any code here needed to setup GASNET_SEGMENT_EVERYTHING support */
    }
#endif

    /* After local segment is attached, call optional client-provided hook
       (###) should call BEFORE any conduit-specific pinning/registration of the segment
     */
    if (gasnet_client_attach_hook) {
        gasnet_client_attach_hook(segbase, segsize);
    }

    /* ------------------------------------------------------------------------------------ */
    /*  gather segment information */

    /* (###) add code here to gather the segment assignment info into 
             gasneti_seginfo on each node (may be possible to use AMShortRequest here)
     */

    // TODO MG: per ora non server... poiche'  gasneti_seginfo[] e' gia' stata riempita]

    /* ------------------------------------------------------------------------------------ */
    /*  primary attach complete */
    gasneti_attach_done = 1;
    gasnetc_bootstrapBarrier();

    GASNETI_TRACE_PRINTF(C, ("gasnetc_attach(): primary attach complete"));

    gasneti_assert((uint8_t*) gasneti_seginfo[gasneti_mynode].addr == (uint8_t*) segbase + GASNETC_RESERVED_SPACE &&
            gasneti_seginfo[gasneti_mynode].size == segsize - GASNETC_RESERVED_SPACE);

    gasneti_auxseg_attach(); /* provide auxseg */

    gasnete_init(); /* init the extended API */

    gasneti_nodemapFini();

    /* ensure extended API is initialized across nodes */
    gasnetc_bootstrapBarrier();

    return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */

#if HAVE_ON_EXIT

static void gasnetc_on_exit(int exitcode, void *arg) {
    gasnetc_exit(exitcode);
}
#else

static void gasnetc_atexit(void) {

    gasnetc_exit(0);
}
#endif

//static int received_exit_message = 0;

extern void gasnetc_exit(int exitcode) {
    /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
    gasneti_reghandler(SIGQUIT, SIG_IGN);

    { /* ensure only one thread ever continues past this point */

        static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
        gasneti_mutex_lock(&exit_lock);
    }

    GASNETI_TRACE_PRINTF(C, ("gasnet_exit(%i)\n", exitcode));

    gasneti_flush_streams();
    gasneti_trace_finish();
    gasneti_sched_yield();

    /* (###) add code here to terminate the job across _all_ nodes 
             with gasneti_killmyprocess(exitcode) (not regular exit()), preferably
             after raising a SIGQUIT to inform the client of the exit
     */
    // MG TODO:
    if (gasnetc_mmap_done)
        axiom_rdma_munmap(axiom_dev);
    /*
        if (!received_exit_message) {
            gasnetc_axiom_msg_t msg;
            received_exit_message = 1;
            msg.command = GASNETC_EXIT_MESSAGE;
            msg.dummy2 = exitcode;
            send_to_all(&msg, AXIOM_BIND_PORT, 8, get_node_id(), get_nodes());
        }
     */
    gasneti_killmyprocess(exitcode);
    gasneti_fatalerror("gasnetc_exit failed! killmyprocess() return!");
}

static void my_signal_handler(int sig) {
    if (sig == SIGQUIT) {
        gasnet_exit(1);
    } else {
        struct sigaction sa;
        memset(&sa, 0, sizeof (sa));
        sa.sa_handler = SIG_DFL;
        sigaction(sig, &sa, NULL);
    }
    raise(sig);
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
 */
#if GASNET_PSHM
/* (###) GASNETC_GET_HANDLER
 *   If your conduit will support PSHM, then there needs to be a way
 *   for PSHM to see your handler table.  If you use the recommended
 *   implementation (gasnetc_handler[]) then you don't need to do
 *   anything special.  Othwerwise, #define GASNETC_GET_HANDLER in
 *   gasnet_core_fwd.h and implement gasnetc_get_handler() here, or
 *   as a macro or inline in gasnet_core_internal.h
 *
 * (###) GASNETC_TOKEN_CREATE
 *   If your conduit will support PSHM, then there needs to be a way
 *   for the conduit-specific and PSHM token spaces to co-exist.
 *   The default PSHM implementation produces tokens with the least-
 *   significant bit set and assumes the conduit never will.  If that
 *   is true, you don't need to do anything special here.
 *   If your conduit cannot use the default PSHM token code, then
 *   #define GASNETC_TOKEN_CREATE in gasnet_core_fwd.h and implement
 *   the associated routines described in gasnet_pshm.h.  That code
 *   could be functions located here, or could be macros or inlines
 *   in gasnet_core_internal.h.
 */
#endif

extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
    gasnet_node_t sourceid;
    GASNETI_CHECKATTACH();
    GASNETI_CHECK_ERRR((!token), BAD_ARG, "bad token");
    GASNETI_CHECK_ERRR((!srcindex), BAD_ARG, "bad src ptr");

#if GASNET_PSHM
    /* (###) If your conduit will support PSHM, let the PSHM code
     * have a chance to recognize the token first, as shown here. */
    if (gasneti_AMPSHMGetMsgSource(token, &sourceid) != GASNET_OK)
#endif
    {
        gasnetc_axiom_am_info_t *info = (gasnetc_axiom_am_info_t*) token;
        sourceid = info->node;
    }

    gasneti_assert(sourceid < gasneti_nodes);
    *srcindex = sourceid;

    return GASNET_OK;
}

#ifndef GASNETC_ENTERING_HANDLER_HOOK
/* extern void enterHook(int cat, int isReq, int handlerId, gasnet_token_t *token,
 *                       void *buf, size_t nbytes, int numargs, gasnet_handlerarg_t *args);
 */
#define GASNETC_ENTERING_HANDLER_HOOK(cat,isReq,handlerId,token,buf,nbytes,numargs,args) ((void)0)
#endif
#ifndef GASNETC_LEAVING_HANDLER_HOOK
/* extern void leaveHook(int cat, int isReq);
 */
#define GASNETC_LEAVING_HANDLER_HOOK(cat,isReq) ((void)0)
#endif

// do not use ONE !!!!!!!!!!! errors on "testgasnet" (bulk monothread send))
#define MAX_MSG_PER_POLL 3

//int _counter=0;

extern int gasnetc_AMPoll(void) {
    uint8_t buffer[sizeof(gasnetc_axiom_msg_t)+GASNETI_MEDBUF_ALIGNMENT];
    gasnetc_axiom_msg_t* payload;
    gasnetc_axiom_am_info_t info;
    size_t size;
    //axiom_raw_payload_size_t size;
    //axiom_long_payload_size_t sizel;
    axiom_err_t ret;
    int maxcounter;
    int res;
    int retval;
    gasnet_token_t token;
    int category;
    gasnet_handler_t handler_id;
    gasneti_handler_fn_t handler_fn;
    int numargs, isReq;
    gasnet_handlerarg_t *args;
    void *data;
    int nbytes;
    // so payload is always GASNETI_MEDBUF_ALIGNMENT aligned
    payload=(gasnetc_axiom_msg_t*)((((uintptr_t)buffer)&~(GASNETI_MEDBUF_ALIGNMENT-1))+GASNETI_MEDBUF_ALIGNMENT);
        
    GASNETI_CHECKATTACH();
#if GASNET_PSHM
    gasneti_AMPSHMPoll(0);
#endif

    for (maxcounter = MAX_MSG_PER_POLL; maxcounter > 0; maxcounter--) {

#ifdef _BLOCKING_MODE
        /*
        #if GASNETC_HSL_SPINLOCK

                if_pf(gasneti_mutex_trylock(&(poll_mutex.lock)) == EBUSY) {
                    if (gasneti_wait_mode == GASNET_WAIT_SPIN) {
                        while (gasneti_mutex_trylock(&(poll_mutex.lock)) == EBUSY) {
                            gasneti_compiler_fence();
                            gasneti_spinloop_hint();
                        }
                    } else {
                        gasneti_mutex_lock(&(poll_mutex.lock));
                    }
                }
        #else
                gasneti_mutex_lock(&(poll_mutex.lock));
        #endif
         */
        LOCK(poll_mutex);
        res = _recv_avail(axiom_dev);
        if (!res) {
            //gasneti_mutex_unlock(&(poll_mutex.lock));
            UNLOCK(poll_mutex);
            break;
        }        
#endif        
        
        info.node = INVALID_PHYSICAL_NODE;
        info.port = AXIOM_BIND_PORT;
        size=AXIOM_LONG_PAYLOAD_MAX_SIZE;
        size = sizeof (*payload);
        ret = _recv(axiom_dev, &info.node, &info.port, &size, payload);

#ifdef _BLOCKING_MODE
        //gasneti_mutex_unlock(&(poll_mutex.lock));
        UNLOCK(poll_mutex);
#endif

        if_pt(AXIOM_RET_IS_OK(ret)) {

            info.isReq = isReq = (payload->gen.command == GASNETC_AM_REQ_MESSAGE);
            
            switch (payload->gen.command) {

                case GASNETC_RDMA_MESSAGE:
                    gasneti_fatalerror("GASNETC_RDMA_MESSAGE not used anymore!");
                    break;

                case GASNETC_AM_REQ_MESSAGE:
                case GASNETC_AM_REPLY_MESSAGE:
                    token=&info;
                    category = payload->am.head.category;
                    handler_id = payload->am.head.handler_id;
                    handler_fn = gasnetc_get_handler(handler_id);
                    numargs = payload->am.head.numargs;
                    args = payload->am.args;
                    data = NULL;
                    nbytes = 0;
                    
                    logmsg(LOG_DEBUG,"received %s category=%s handler=%d",
                            payload->gen.command==GASNETC_AM_REQ_MESSAGE?"AM_REQ_MESSAGE":"AM_REPLY_MESSAGE",
                            category==gasnetc_Short?"Short":(category==gasnetc_Medium?"Medium":"Long"),
                            handler_id
                            );
                    
                    if (logmsg_is_enabled(LOG_TRACE)) {
                        char mybuf[5*128+1];
                        char num[6];
                        int i;
                        mybuf[0]='\0';
                        for (i=0;i<size&&i<128;i++) {
                            sprintf(num,"0x%02x ",*(((uint8_t*)payload)+i));
                            strcat(mybuf,num);
                        }
                        logmsg(LOG_TRACE,"packet dump: %s",mybuf);
                    }
                    
                    gasneti_assert((category == gasnetc_Short) || (category == gasnetc_Medium) || (category == gasnetc_Long));

                    switch (category) {
                        case gasnetc_Short:
                        {
                            GASNETC_ENTERING_HANDLER_HOOK(category, isReq, handler_id, token, data, nbytes, numargs, args);
                            GASNETI_RUN_HANDLER_SHORT(isReq, handler_id, handler_fn, token, args, numargs);
                        }
                            break;
                        case gasnetc_Medium:
                        {
                            void * data = payload->buffer+compute_aligned_payload_size(payload->am.head.numargs);
                            size_t nbytes = payload->am.head.size;
                            GASNETC_ENTERING_HANDLER_HOOK(category, isReq, handler_id, token, data, nbytes, numargs, args);
                            GASNETI_RUN_HANDLER_MEDIUM(isReq, handler_id, handler_fn, token, args, numargs, data, nbytes);
                        }
                            break;
                        case gasnetc_Long:
                        {
                            data = (uint8_t*) gasneti_seginfo[gasneti_mynode].rdma + payload->am.head.offset;
                            nbytes = payload->am.head.size;
                            if (nbytes & (GASNETC_ALIGN_SIZE - 1)) {
                                int rest = nbytes & (GASNETC_ALIGN_SIZE - 1);
                                void *ptr = (uint8_t*) data + (nbytes & (~(GASNETC_ALIGN_SIZE - 1)));
                                memcpy(ptr, payload->am.head.data, rest);
                            }
                            GASNETC_ENTERING_HANDLER_HOOK(category, isReq, handler_id, token, data, nbytes, numargs, args);
                            GASNETI_RUN_HANDLER_LONG(isReq, handler_id, handler_fn, token, args, numargs, data, nbytes);
                        }
                            break;
                    }
                    GASNETC_LEAVING_HANDLER_HOOK(category, isReq);
                    break;

                default:
                    gasneti_fatalerror("Unknown axiom packed received (command=%d)!", payload->gen.command);
                    break;

            }

        } else {
            gasneti_fatalerror("AXIOM read message error (err=%d)",ret);            
        }
        
    }

    return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */

/*
  Active Message Request Functions
  ================================
 */

extern int gasnetc_AMRequestShortM(gasnet_node_t dest, gasnet_handler_t handler, int numargs, ...) {
    gasnetc_axiom_am_msg_t payload;
    axiom_err_t ret;
    va_list argptr;
    int retval;
    int i;

    logmsg(LOG_INFO,"AMRequestShort dest=%d handler=%d",dest,handler);
    GASNETI_COMMON_AMREQUESTSHORT(dest, handler, numargs);
    gasneti_AMPoll();
    va_start(argptr, numargs);

#if GASNET_PSHM

    if_pt(gasneti_pshm_in_supernode(dest)) {
        retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Short, dest, handler,
                0, 0, 0,
                numargs, argptr);
    } else
#endif
    {
        payload.head.command = GASNETC_AM_REQ_MESSAGE;
        payload.head.category = gasnetc_Short;
        payload.head.handler_id = handler;
        payload.head.numargs = numargs;
        for (i = 0; i < numargs; i++) {
            payload.args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        ret = _send_raw(axiom_dev, dest, AXIOM_BIND_PORT, compute_payload_size(numargs), &payload);
        retval = AXIOM_RET_IS_OK(ret) ? GASNET_OK : -1;
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestMediumM(
        gasnet_node_t dest, /* destination node */
        gasnet_handler_t handler, /* index into destination endpoint's handler table */
        void *source_addr, size_t nbytes, /* data payload */
        int numargs, ...)
{
    gasnetc_axiom_am_msg_t payload;
    axiom_err_t ret;
    va_list argptr;
    int retval;
    int i;

    logmsg(LOG_INFO,"AMRequestMedium dest=%d handler=%d buf[0]=0x%02x",dest,handler,nbytes>0?*(uint8_t*)source_addr:255);
    GASNETI_COMMON_AMREQUESTMEDIUM(dest, handler, source_addr, nbytes, numargs);
    gasneti_AMPoll(); /* (###) poll at least once, to assure forward progress */
    va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM

    /* (###) If your conduit will support PSHM, let it check the dest first. */
    if_pt(gasneti_pshm_in_supernode(dest)) {
        retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Medium, dest, handler,
                source_addr, nbytes, 0,
                numargs, argptr);
    } else
#endif
    {              
        struct iovec v[2];
        payload.head.command = GASNETC_AM_REQ_MESSAGE;
        payload.head.category = gasnetc_Medium;
        payload.head.handler_id = handler;
        payload.head.numargs = numargs;
        for (i = 0; i < numargs; i++) {
            payload.args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        payload.head.size = nbytes;
        v[0].iov_base=&payload;
        v[0].iov_len=compute_aligned_payload_size(numargs);
        v[1].iov_base=source_addr;
        v[1].iov_len=nbytes;
        //if (nbytes > 0) memcpy(payload.buffer, source_addr, nbytes);
        ret = _send_long_iov(axiom_dev, dest, AXIOM_BIND_PORT, v, 2);
        //fprintf(stderr, "SENT ID (req) %d\n", ret);
        retval = AXIOM_RET_IS_OK(ret) ? GASNET_OK : -1;
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

static int _requestReplyLong(gasnet_node_t dest, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, va_list argptr, int flagtodel) {
    int ret = AXIOM_RET_OK;
    int retval = GASNET_ERR_RDMA;
    gasnetc_axiom_am_msg_t payload;
    int out;

    if (source_addr < gasneti_seginfo[gasneti_mynode].base) {
        if ((uint8_t*) source_addr + nbytes >= (uint8_t*) gasneti_seginfo[gasneti_mynode].base)
            gasneti_fatalerror("source payload cross starting rdma mapped memory");
        out = 1;
    } else if ((uint8_t*) source_addr > (uint8_t*) gasneti_seginfo[gasneti_mynode].addr + gasneti_seginfo[gasneti_mynode].size) {
        out = 1;
    } else {
        if (source_addr < gasneti_seginfo[gasneti_mynode].addr || (uint8_t*) source_addr + nbytes > (uint8_t*) gasneti_seginfo[gasneti_mynode].addr + gasneti_seginfo[gasneti_mynode].size)
            gasneti_fatalerror("source payload cross ending rdma mapped memory");
        out = 0;
    }

    if (nbytes & (GASNETC_ALIGN_SIZE - 1)) {
        //fprintf(stderr, "GASNET WARNING: requestLong() end address NOT properly aligned (nbytes=%d)\n", (int) nbytes);
        //gasnetc_axiom_rdma_msg_t payload;
        //int i;
        //payload.command = GASNETC_RDMA_MESSAGE;
        //payload.rest = nbytes & (GASNETC_ALIGN_SIZE - 1);
        //payload.offset = ((uint8_t*) dest_addr + (nbytes&~(GASNETC_ALIGN_SIZE - 1))) - (uint8_t*) gasneti_seginfo[dest].rdma;
        //memcpy(payload.data, (uint8_t*) source_addr + (nbytes&~(GASNETC_ALIGN_SIZE - 1)), payload.rest);
        //ret = _send_raw(axiom_dev, dest, AXIOM_BIND_PORT, sizeof (gasnetc_axiom_rdma_msg_t), &payload);
        ////to check return value
        int rest = nbytes & (GASNETC_ALIGN_SIZE - 1);
        void *ptr = (uint8_t*) source_addr + (nbytes & (~(GASNETC_ALIGN_SIZE - 1)));
        memcpy(payload.head.data, ptr, rest);
    }

    if (nbytes / GASNETC_ALIGN_SIZE > 0) {
        if (out) {
            void *buf = alloca_rdma_buf();
            uint8_t *srcp = source_addr, *dstp = dest_addr;
            int totras = (nbytes & (~(GASNETC_ALIGN_SIZE - 1)));
            int sz;
            for (;;) {
                sz = totras > GASNETC_BUFFER_SIZE ? GASNETC_BUFFER_SIZE : totras;
                memcpy(buf, srcp, sz);
                ret = _rdma_write(axiom_dev, dest, sz / GASNETC_ALIGN_SIZE, buf, dstp);
                if (!AXIOM_RET_IS_OK(ret)) break;
                totras -= sz;
                if (totras == 0) break;
                dstp += sz;
                srcp += sz;
            }
            free_rdma_buf(buf);
        } else {
            ret = _rdma_write(axiom_dev, dest, nbytes / GASNETC_ALIGN_SIZE, source_addr, dest_addr);
        }
    }

    if (AXIOM_RET_IS_OK(ret)) {

        axiom_err_t ret;
        int i;

        payload.head.command = GASNETC_AM_REQ_MESSAGE;
        payload.head.category = gasnetc_Long;
        payload.head.handler_id = handler;
        payload.head.numargs = numargs;
        payload.head.offset = (uintptr_t) dest_addr - (uintptr_t) gasneti_seginfo[dest].rdma;
        payload.head.size = nbytes;
        for (i = 0; i < numargs; i++) {
            payload.args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        ret = _send_raw(axiom_dev, dest, AXIOM_BIND_PORT, compute_payload_size(numargs), &payload);
        retval = AXIOM_RET_IS_OK(ret) ? GASNET_OK : GASNET_ERR_RAW_MSG;
    }

    return retval;
}

extern int gasnetc_AMRequestLongM(gasnet_node_t dest, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, ...) {
    axiom_err_t ret;
    va_list argptr;
    int retval;

    logmsg(LOG_INFO,"AMRequestLong dest=%d handler=%d",dest,handler);
    GASNETI_COMMON_AMREQUESTLONG(dest, handler, source_addr, nbytes, dest_addr, numargs);
    gasneti_AMPoll();
    va_start(argptr, numargs);

#if GASNET_PSHM

    if_pt(gasneti_pshm_in_supernode(dest)) {
        retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Long, dest, handler,
                source_addr, nbytes, dest_addr,
                numargs, argptr);
    } else
#endif
    {
        retval = _requestReplyLong(dest, handler, source_addr, nbytes, dest_addr, numargs, argptr, 1);
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongAsyncM(gasnet_node_t dest, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, ...) {
    int retval;
    va_list argptr;

    logmsg(LOG_INFO,"AMRequestLongAsync dest=%d handler=%d",dest,handler);
    GASNETI_COMMON_AMREQUESTLONGASYNC(dest, handler, source_addr, nbytes, dest_addr, numargs);
    gasneti_AMPoll(); /* (###) poll at least once, to assure forward progress */
    va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM

    if_pt(gasneti_pshm_in_supernode(dest)) {
        retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Long, dest, handler,
                source_addr, nbytes, dest_addr,
                numargs, argptr);
    } else
#endif
    {
        retval = _requestReplyLong(dest, handler, source_addr, nbytes, dest_addr, numargs, argptr, 1);
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM(gasnet_token_t token, gasnet_handler_t handler, int numargs, ...) {
    gasnetc_axiom_am_info_t *info = (gasnetc_axiom_am_info_t*) token;
    gasnetc_axiom_am_msg_t payload;
    axiom_err_t ret;
    va_list argptr;
    int retval;
    int i;

    logmsg(LOG_INFO,"AMReplyShort token=%p dest_node=%d handler=%d",(void*)token,info->node,handler);
    GASNETI_COMMON_AMREPLYSHORT(token, handler, numargs);
    gasneti_assert_always(info->isReq);
    //GASNETI_CHECK_ERRR((info == NULL), BAD_ARG, "AMReplyXXX() called from a reply handler!");
    va_start(argptr, numargs);

#if GASNET_PSHM

    if_pt(gasnetc_token_is_pshm(token)) {
        retval = gasneti_AMPSHM_ReplyGeneric(gasnetc_Short, token, handler,
                0, 0, 0,
                numargs, argptr);
    } else
#endif
    {
        payload.head.command = GASNETC_AM_REPLY_MESSAGE;
        payload.head.category = gasnetc_Short;
        payload.head.handler_id = handler;
        payload.head.numargs = numargs;
        for (i = 0; i < numargs; i++) {
            payload.args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        ret = _send_raw(axiom_dev, info->node, info->port, compute_payload_size(numargs), &payload);
        retval = AXIOM_RET_IS_OK(ret) ? GASNET_OK : GASNET_ERR_RAW_MSG;
    }
    va_end(argptr);

    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyMediumM(
        gasnet_token_t token, /* token provided on handler entry */
        gasnet_handler_t handler, /* index into destination endpoint's handler table */
        void *source_addr, size_t nbytes, /* data payload */
        int numargs, ...)
{
    gasnetc_axiom_am_info_t *info = (gasnetc_axiom_am_info_t*) token;
    gasnetc_axiom_am_msg_t payload;
    axiom_err_t ret;
    va_list argptr;
    int retval;
    int i;
    
    logmsg(LOG_INFO,"AMReplyMedium token=%p dest_node=%d handler=%d buf[0]=0x%02x",(void*)token,info->node,handler,nbytes>0?*(uint8_t*)source_addr:255);
    GASNETI_COMMON_AMREPLYMEDIUM(token, handler, source_addr, nbytes, numargs);
    gasneti_assert_always(info->isReq);
    va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
    /* (###) If your conduit will support PSHM, let it check the token first. */
    if_pt(gasnetc_token_is_pshm(token)) {
        retval = gasneti_AMPSHM_ReplyGeneric(gasnetc_Medium, token, handler,
                source_addr, nbytes, 0,
                numargs, argptr);
    } else
#endif
    {
        struct iovec v[2];        
        payload.head.command = GASNETC_AM_REPLY_MESSAGE;
        payload.head.category = gasnetc_Medium;
        payload.head.handler_id = handler;
        payload.head.numargs = numargs;
        for (i = 0; i < numargs; i++) {
            payload.args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        payload.head.size = nbytes;
        v[0].iov_base=&payload;
        v[0].iov_len=compute_aligned_payload_size(numargs);
        v[1].iov_base=source_addr;
        v[1].iov_len=nbytes;
        //if (nbytes > 0) memcpy(payload.buffer, source_addr, nbytes);
        ret = _send_long_iov(axiom_dev, info->node, info->port, v, 2);
        //fprintf(stderr, "SENT ID (rep) %d\n", ret);
        retval = AXIOM_RET_IS_OK(ret) ? GASNET_OK : GASNET_ERR_RAW_MSG;
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyLongM(gasnet_token_t token, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, ...) {
    gasnetc_axiom_am_info_t *info = (gasnetc_axiom_am_info_t*) token;
    int retval;
    va_list argptr;

    logmsg(LOG_INFO,"AMReplyLong token=%p dest_node=%d handler=%d",(void*)token,info->node,handler);
    GASNETI_COMMON_AMREPLYLONG(token, handler, source_addr, nbytes, dest_addr, numargs);
    //GASNETI_CHECK_ERRR((info == NULL), BAD_ARG, "AMReplyXXX() called from a reply handler!");
    gasneti_assert_always(info->isReq);
    va_start(argptr, numargs);

#if GASNET_PSHM
    if_pt(gasnetc_token_is_pshm(token)) {
        retval = gasneti_AMPSHM_ReplyGeneric(gasnetc_Long, token, handler,
                source_addr, nbytes, dest_addr,
                numargs, argptr);
    } else
#endif
    {
        retval = _requestReplyLong(info->node, handler, source_addr, nbytes, dest_addr, numargs, argptr, 0);
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
  This section is only required for conduits that may use interrupt-based handler dispatch
  See the GASNet spec and http://gasnet.lbl.gov/dist/docs/gasnet.html for
    philosophy and hints on efficiently implementing no-interrupt sections
  Note: the extended-ref implementation provides a thread-specific void* within the 
    gasnete_threaddata_t data structure which is reserved for use by the core 
    (and this is one place you'll probably want to use it)
 */
#if GASNETC_USE_INTERRUPTS
#error interrupts not implemented

extern void gasnetc_hold_interrupts(void) {

    GASNETI_CHECKATTACH();
    /* add code here to disable handler interrupts for _this_ thread */
}

extern void gasnetc_resume_interrupts(void) {

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

extern void gasnetc_hsl_init(gasnet_hsl_t *hsl) {

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

extern void gasnetc_hsl_lock(gasnet_hsl_t *hsl) {
    GASNETI_CHECKATTACH();

    {
#if GASNETI_STATS_OR_TRACE

        gasneti_tick_t startlock = GASNETI_TICKS_NOW_IFENABLED(L);
#endif
#if GASNETC_HSL_SPINLOCK

        if_pf(gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) {
            if (gasneti_wait_mode == GASNET_WAIT_SPIN) {
                while (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) {
                    gasneti_compiler_fence();
                    gasneti_spinloop_hint();
                }
            } else {
                gasneti_mutex_lock(&(hsl->lock));
            }
        }
#else
        gasneti_mutex_lock(&(hsl->lock));
#endif
#if GASNETI_STATS_OR_TRACE
        hsl->acquiretime = GASNETI_TICKS_NOW_IFENABLED(L);
        GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime - startlock);
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

extern void gasnetc_hsl_unlock(gasnet_hsl_t *hsl) {

    GASNETI_CHECKATTACH();

#if GASNETC_USE_INTERRUPTS
    /* conduits with interrupt-based handler dispatch need to add code here to 
       re-enable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock release and we're not inside an enclosing no-interrupt section)
     */
#error interrupts not implemented
#endif

    GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_TICKS_NOW_IFENABLED(L) - hsl->acquiretime);

    gasneti_mutex_unlock(&(hsl->lock));
}

extern int gasnetc_hsl_trylock(gasnet_hsl_t *hsl) {
    GASNETI_CHECKATTACH();

    {
        int locked = (gasneti_mutex_trylock(&(hsl->lock)) == 0);

        GASNETI_TRACE_EVENT_VAL(L, HSL_TRYLOCK, locked);
        if (locked) {
#if GASNETI_STATS_OR_TRACE

            hsl->acquiretime = GASNETI_TICKS_NOW_IFENABLED(L);
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

    /* ptr-width dependent handlers */ {
        0, NULL
    }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable(void) {
    return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */
