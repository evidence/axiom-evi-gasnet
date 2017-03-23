/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core.c $
 * Description: GASNet AXIOM conduit Implementation
 *
 * Copyright (C) 2016, Evidence Srl.
 * Terms of use are as specified in COPYING
 *
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include "axiom_nic_api_user.h"
#include "axiom_nic_packets.h"
#include "axiom_nic_init.h"
#include "axiom_nic_types.h"
#include "axiom_run_api.h"
#include "axiom_allocator.h"

/*
 *
 * checks DEFINEs
 *
 */

/* NMB: gasnet_core_fwd.h defines some compilation "#define" modes */

#if defined(_BLOCKING_MODE)
#if defined(_NOT_BLOCKING_MODE)
#error Defined both _BLOCKING_MODE and _NOT_BLOCKING_MODE
#endif
#else
#if !defined(_NOT_BLOCKING_MODE)
#warning No _BLOCKING_MODE or _NOT_BLOCKING_MODE defined! _BLOCKING_MODE will be used!
#define _BLOCKING_MODE
#endif
#endif

#if defined(_ASYNC_RDMA_MODE)
#if defined(_NOT_ASYNC_RDMA_MODE)
#error Defined both _ASYNC_RDMA_MODE and _NOT_ASYNC_RDMA_MODE
#endif
#else
#if !defined(_NOT_ASYNC_RDMA_MODE)
#warning No _ASYNC_RDMA_MODE or _NOT_ASYNC_RDMA_MODE defined! _NOT_ASYNC_RDMA_MODE will be used!
#define _NOT_ASYNC_RDMA_MODE
#endif
#endif

#if defined(_NOT_BLOCK_ON_LOOP)
#if defined(_BLOCK_ON_LOOP_CONDWAIT)||defined(_BLOCK_ON_LOOP_EPOLL)
#error Defined too much _NOT_BLOCK_ON_LOOP _BLOCK_ON_LOOP_CONDWAIT _BLOCK_ON_LOOP_EPOLL
#endif
#elif defined(_BLOCK_ON_LOOP_CONDWAIT)
#if defined(_NOT_BLOCK_ON_LOOP)||defined(_BLOCK_ON_LOOP_EPOLL)
#error Defined too much _NOT_BLOCK_ON_LOOP _BLOCK_ON_LOOP_CONDWAIT _BLOCK_ON_LOOP_EPOLL
#endif
#elif defined(_BLOCK_ON_LOOP_EPOLL)
#if defined(_NOT_BLOCK_ON_LOOP)||defined(_BLOCK_ON_LOOP_CONDWAIT)
#error Defined too much _NOT_BLOCK_ON_LOOP _BLOCK_ON_LOOP_CONDWAIT _BLOCK_ON_LOOP_EPOLL
#endif
#else
#warning No _NOT_BLOCK_ON_LOOP or _BLOCK_ON_LOOP_CONDWAIT or _BLOCK_ON_LOOP_EPOLL defined! _NOT_BLOCK_ON_LOOP will be used!
#define _NOT_BLOCK_ON_LOOP
#endif

/*
 *
 *
 * enable/disable internal conduit logging
 *
 *
 *
 */

/* PS: if enabled the overhead is only an integer comparison for every message in the code (plus, eventually, the time spent to display the message).  */

#ifdef GASNET_DEBUG

// if defined does not output system time
#define GASNET_DEBUG_SIMPLE_OUTPUT

// if defined emit a thread id on evry log message
#define GASNET_DEBUG_EMIT_THREAD

#include <sys/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <axiom/axiom_nic_types.h>
#include <axiom/axiom_nic_api_user.h>
#include <sys/eventfd.h>

/**
 * Logs levels.
 * Lower level include upper level.
 */
typedef enum {
    /** No logging. */
    LOG_NOLOG = -1,
    /** Fatal error logging. */
    LOG_FATAL = 0,
    /** Error logging. */
    LOG_ERROR = 1,
    /** Warning logging. */
    LOG_WARN = 2,
    /** Informational logging. */
    LOG_INFO = 3,
    /** Debug logging. */
    LOG_DEBUG = 4,
    /** Trace logging. */
    LOG_TRACE = 5
} logmsg_level_t;

/** Name to log level mapping. */
static const char *logmsg_name[] = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
/** Actual log level. Default to LOG_ERROR. */
static logmsg_level_t logmsg_level=LOG_ERROR;
/** Logging output FILE. */
static FILE *logmsg_fout=NULL;
/** Start logging time. Seconds. */
static time_t logmsg_sec;
/** Start logging time. Microsencods. */
static long logmsg_micros;

/** Test if a message of a specific log level is enabled. */
#define logmsg_is_enabled(lvl) ((lvl)<=logmsg_level)

/*
 * OLD, do not remove!
#define logmsgOLD(lvl, msg, ...) {\
  if (logmsg_is_enabled(lvl)) {\
    struct timespec _t1;\
    time_t _secs;\
    long _micros;\
    clock_gettime(CLOCK_REALTIME_COARSE,&_t1);\
    _micros=_t1.tv_nsec/1000-logmsg_micros;\
    _secs=_t1.tv_sec-logmsg_sec;\
    if (_micros<0) { _micros+=1000000;_secs--;}\
    _logmsg("[%5d.%06d] %5s{%d}: " msg "\n", (int)_secs, (int)_micros, logmsg_name[lvl], (int)getpid(), ##__VA_ARGS__);\
  }\
}
 */

#ifdef GASNET_DEBUG_EMIT_THREAD
/** A thread-local-storage key. Contain a "number" to identify a  thread. */
static pthread_key_t logmsg_threadkey;
/** A thread counter. */
static int logmsg_threadkeycounter;
/**
 * Build a "number" to identify the thread.
 * @return A pointer contains a " number" fo identiy a thread.
 */
static void *make_self_index() {
    int *ptr=(int*)gasneti_malloc(sizeof(int));
    pthread_setspecific(logmsg_threadkey,ptr);
    *ptr=__sync_fetch_and_add(&logmsg_threadkeycounter,1);
    return ptr;
}
/**
 * To identify a thread.
 * @return A number to identify the thread.
 */
static inline int gasnetc_self() {
    int *ptr=(int*)pthread_getspecific(logmsg_threadkey);
    if (ptr==NULL) ptr=make_self_index();
    return *ptr;
}
///**
// * To identify a thread.
// * @return A number to identify the thread.
// */
//static inline int gasnetc_self() {
//    return (int)(pthread_self()&0xfffff);
//}
#endif

#ifdef GASNET_DEBUG_SIMPLE_OUTPUT
/**
 * Log a message.
 * Without time.
 * @param lvl the log level.
 * @param msg the message.
 * @param ... the parameter of the message (printf stype).
 */
#ifdef GASNET_DEBUG_EMIT_THREAD
#define logmsg(lvl, msg, ...) {\
  if (logmsg_is_enabled(lvl)) {\
    _logmsg("%5s{%02d}: " msg "\n", logmsg_name[lvl], gasnetc_self(), ##__VA_ARGS__);\
  }\
}
#else
#define logmsg(lvl, msg, ...) {\
  if (logmsg_is_enabled(lvl)) {\
    _logmsg("%5s: " msg "\n", logmsg_name[lvl], ##__VA_ARGS__);\
  }\
}
#endif
#else
/**
 * Log a message.
 * With time.
 * @param lvl the log level.
 * @param msg the message.
 * @param ... the parameter of the message (printf stype).
 */
#ifdef GASNET_DEBUG_EMIT_THREAD
#define logmsg(lvl, msg, ...) {\
  if (logmsg_is_enabled(lvl)) {\
    struct timespec _t1;\
    time_t _secs,_min;\
    clock_gettime(CLOCK_REALTIME_COARSE,&_t1);\
    _secs=_t1.tv_sec%60;\
    _min=(_t1.tv_sec/60)%60;\
    _logmsg("[%02d:%02d.%06d] %5s{%02d}: " msg "\n", (int)_min, (int)_secs, (int)(_t1.tv_nsec/1000), logmsg_name[lvl], gasnetc_self(), ##__VA_ARGS__);\
  }\
}
#else
#define logmsg(lvl, msg, ...) {\
  if (logmsg_is_enabled(lvl)) {\
    struct timespec _t1;\
    time_t _secs,_min;\
    clock_gettime(CLOCK_REALTIME_COARSE,&_t1);\
    _secs=_t1.tv_sec%60;\
    _min=(_t1.tv_sec/60)%60;\
    _logmsg("[%02d:%02d.%06d] %5s: " msg "\n", (int)_min, (int)_secs, (int)(_t1.tv_nsec/1000), logmsg_name[lvl], ##__VA_ARGS__);\
  }\
}
#endif
#endif

static void _logmsg(const char *msg, ...) __attribute__((format(printf, 1, 2)));

/**
 * Write a log message on output stream.
 * @param msg the message.
 * @param ... the messages's parameters (printf style).
 */
static inline void _logmsg(const char *msg, ...) {
    va_list list;
    va_start(list, msg);
    vfprintf(logmsg_fout, msg, list);
    va_end(list);
}

/**
 * Logging initialization.
 * Some enviroment variable are used to initialize the logging subsystem.
 * GASNET_AXIOM_LOG_LEVEL if present must contains the name of the logging level required (otherwise a default of LOG_ERROR will be used).
 * GASNET_AXIOM_LOG_FILE if present contains the name of the outputfile (otherwise stderr is used).
 */
static inline void logmsg_init() {
    struct timespec t0;
    char buf[MAXPATHLEN];
    char *value;
    //
    clock_gettime(CLOCK_REALTIME_COARSE,&t0);
    logmsg_micros=t0.tv_nsec/1000;
    logmsg_sec=t0.tv_sec;
    //
    value = getenv("GASNET_AXIOM_LOG_LEVEL");
    if (value != NULL) {
        int i;
        for (i = 0; i<sizeof (logmsg_name) / sizeof (char*); i++)
            if (strcasecmp(logmsg_name[i], value) == 0) {
                logmsg_level = i;
                break;
            }
    }
    //    
    value = getenv("GASNET_AXIOM_LOG_FILE");
    if (value!=NULL&&strstr(value,"%ld")!=NULL) {
        snprintf(buf,sizeof(buf),value,(long)getpid());
        value=buf;
    }
    if (value != NULL) {
        logmsg_fout=fopen(value,"w+");
        setvbuf(logmsg_fout, NULL, _IONBF, 0);
    } else {
        logmsg_fout = stderr;
    }
    //
#ifdef GASNET_DEBUG_EMIT_THREAD
    // data needed to identify a thread with a "small number"
    pthread_key_create(&logmsg_threadkey,NULL);
    logmsg_threadkeycounter=0;
#endif
}

#else

// if this is a 'performance' build remove all logging functions.

#define logmsg_init()
#define logmsg_is_enabled(lvl) 0
#define logmsg(lvl,msg,...) 

#endif

/*
 *
 *
 * gasnet conduit misc definitions
 *
 *
 *
 */

#if GASNET_PSHM
// paranoia
#error AXIOM conduit does not support PSHM
#endif
#ifdef GASNET_SEGMENT_EVERYTHING
//paranoia
#error AXIOM conduit does not support SEGMENT_EVERYTHING
#endif

/* Conduit identification. */
GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
#ifdef _BLOCKING_MODE
#ifdef _ASYNC_RDMA_MODE
GASNETI_IDENT(gasnetc_IdentString_Name, "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " (flags: BLOCK, ASYNC)$");
#else
GASNETI_IDENT(gasnetc_IdentString_Name, "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " (flags: BLOCK, NO ASYNC$");
#endif
#else
#ifdef _ASYNC_RDMA_MODE
GASNETI_IDENT(gasnetc_IdentString_Name, "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " (flags; NO BLOCK, ASYNC)$");
#else
GASNETI_IDENT(gasnetc_IdentString_Name, "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " (flags: NO BLOCK, NO ASYNC)$");
#endif
#endif

/* The message handler table */
gasnet_handlerentry_t const *gasnetc_get_handlertable(void);

/* Exit functions. */
#if HAVE_ON_EXIT
static void gasnetc_on_exit(int, void*);
#else
static void gasnetc_atexit(void);
#endif

/** Gasnet conduit handler table. */
gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table (recommended impl) */

/*
 *
 *
 * Internal axiom messages
 *
 *
 *
 */

/** Default axiom port to use. */
#define AXIOM_BIND_PORT 7

/** Axiom device. */
static axiom_dev_t *axiom_dev=NULL;

/*
#ifndef _BLOCKING_MODE
static axiom_dev_t *axiom_dev_blocking=NULL;
#endif
*/

// commands (for axiom raw messages)
/** This is a Active Message request. */
#define GASNETC_AM_REQ_MESSAGE 128
/** This is a Active message reply. */
#define GASNETC_AM_REPLY_MESSAGE 129
/** This is a RDMA message. */
#define GASNETC_RDMA_MESSAGE 130

/**
 * Axiom raw header message structure.
 */
typedef struct gasnetc_axiom_am_header {
    /** Command. A GASNET_XXXX_MESSAGE constant. */
    uint8_t command;
    /** Message catagory. On of the gasnetc_Short, gasnetc_Medium or gasnetc_Long constant. */
    uint8_t category;
    /** Remote handler identification. */
    uint8_t handler_id;
    /** Number of arguments. The arguments are located after this message header.*/
    uint8_t numargs;
    /** RDMA offset. Used for 'data' below.*/
    uint32_t offset;
    /** RDMA size. Used for 'data' below. */
    uint32_t size;
    /** RDMA residual data to transfert. AXIOM RDMA can transfert only a multiple of GASNETC_ALIGN_SIZE bytes so this array containt the residual data.*/
    uint8_t data[GASNETC_ALIGN_SIZE]; // need if not using rdma_message!!!!
} __attribute__((__packed__)) gasnetc_axiom_am_header_t;

// Size of the raw message header.
// This size implies the maximum number of Short message argument so it must known externally.
#ifndef GASNET_AXIOM_AM_MSG_HEADER_SIZE
#define GASNET_AXIOM_AM_MSG_HEADER_SIZE sizeof(gasnetc_axiom_am_header_t)
#endif
#if GASNET_AXIOM_AM_MSG_HEADER_SIZE!=20
#error GASNET_AXIOM_AM_MSG_HEADER_SIZE defined into gasnet_core.c must be equal to gasnet_core.h
#endif

/**
 * Axiom message structure.
 */
typedef struct gasnetc_axiom_am_msg {
    /** The header. */
    gasnetc_axiom_am_header_t head;
    /** The parameters of the active message. */
    gasnet_handlerarg_t args[GASNET_AXIOM_AM_MAX_NUM_ARGS];
} __attribute__((__packed__)) gasnetc_axiom_am_msg_t;


/** Compute the size of the payload. If using a specific number of argument. */
#define compute_payload_size(numargs) (GASNET_AXIOM_AM_MSG_HEADER_SIZE+sizeof(gasnet_handlerarg_t)*(numargs))

//#define compute_aligned_payload_size(numargs) (compute_payload_size(numargs)+((numargs&0x1)?0:4))

/**
 * Compute the size of the aligned payload.
 * A size needed if the 'real' payload size is not GASNETI_MEDBUF_ALIGNMENT bytes aligned.
 *
 * @param numargs Number of argument.
 * @return The size.
 */
static inline size_t compute_aligned_payload_size(int numargs) {
    register size_t sz=compute_payload_size(numargs);
    return ((sz&(GASNETI_MEDBUF_ALIGNMENT-1))==0)?sz:((sz&~(GASNETI_MEDBUF_ALIGNMENT-1))+GASNETI_MEDBUF_ALIGNMENT);
}

/**
 * Axiom generic message structure.
 * Only the command is needed.
 */
typedef struct gasnetc_axiom_generic_msg {
    /** Command. A GASNET_XXXX_MESSAGE constant. */
    uint8_t command;
} __attribute__((__packed__)) gasnetc_axiom_generic_msg_t;

/**
 * An axiom buffer for a received message.
 */
typedef struct gasnetc_axiom_msg {
    union {
        /** The generic message. */
        gasnetc_axiom_generic_msg_t gen;
        /** The active message. */
        gasnetc_axiom_am_msg_t am;
        /** The Gasnet Long message. */
        uint8_t buffer[AXIOM_LONG_PAYLOAD_MAX_SIZE];
    } __attribute__((__packed__));
} __attribute__((__packed__)) gasnetc_axiom_msg_t;

/**
 * Axiom internal information for build a gasnet replay.
 */
typedef struct gasnetc_axiom_am_info {
    /** Source axiom node. */
    gasnet_node_t node;
    /** Source axiom port */
    axiom_port_t port;
    /** If true this is a request (otherwise a reply). */
    int isReq;
} gasnetc_axiom_am_info_t;

/**
 * Get hadler.
 * Default implementation using an array.
 * 
 * @param h the handler id.
 * @return the handelr information.
 */
#define gasnetc_get_handler(_h) (gasnetc_handler[(_h)])

// maps gasnet HSL (High Speed Lock) to friendly name

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
/** Lock a mutex. */
#define LOCK(x) \
    gasneti_mutex_lock(&(x))
#endif
/** Unlock a mutes. */
#define UNLOCK(x) \
    gasneti_mutex_unlock(&(x))
/** Initialize a mutex. */
#define INIT_MUTEX(x) \
    gasneti_mutex_init(&(x))
/** Static mutex initializer. */
#define MUTEX_INITIALIZER GASNETI_MUTEX_INITIALIZER
/** Mutex definition. */
#define MUTEX_t gasneti_mutex_t

/** Init a condition variable. */
#define INIT_COND(x) \
    gasneti_cond_init(&(x))
/** Signal a condition variable. */
#define SIGNAL_COND(x) \
    gasneti_cond_signal(&(x))
/** Wait a condition variable. */
#define WAIT_COND(x,m) \
    gasneti_cond_wait(&(x),&(m))
/** Condition variable definition. */
#define COND_t gasneti_cond_t

/*
 *
 *
 * Node management
 *
 *
 * 
 */

// to map AXIOM nodes to GASNET nodes
// PHYSICAL means AXIOM
// LOGICAL means GASNET

/** Invalid axiom node number. */
#define INVALID_PHYSICAL_NODE 255
/** Invalid gasnet node number. */
#define INVALID_LOGICAL_NODE -1

/** Table for gasnet to axiom node mapping. */
static axiom_node_id_t *gasnetc_nodes_log2phy = NULL;
/** Table for gasnet to axiom node mapping. */
static gasnet_node_t *gasnetc_nodes_phy2log = NULL;
/** Number of gasnet nodes. Note that num_jobs_nodes must be less or equal to num_phy_nodes */
static int num_job_nodes;
/** Number of axiom nodes. */
static int num_phy_nodes;

/**
 * Map a gasnet node to a axiom node.
 * @param node The gasnet node.
 * @return The axiom node.
 */
static inline axiom_node_id_t node_log2phy(gasnet_node_t node) {
    gasneti_assert(node >= 0 && node < num_job_nodes);
    return gasnetc_nodes_log2phy[node];
}

/**
 * Map a axiom node to a gasnet node.
 * @param node The axiom node.
 * @return The gasnet node.
 */
static inline gasnet_node_t node_phy2log(axiom_node_id_t node) {
    gasneti_assert(node > 0 && node <= num_phy_nodes);
    gasneti_assert(gasnetc_nodes_phy2log[node] != INVALID_LOGICAL_NODE);
    return gasnetc_nodes_phy2log[node];
}

/*
 *
 *
 * Low levels axiom API wrappers.
 *
 *
 *
 */

#ifdef _NOT_BLOCKING_MODE

/*
 * OLD implementation: do not remove.
 *
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
*/

/** SPINLOOP_FOR() iterations before slowing spin. */
#define WRN_SPINLOOP_FOR 65536
/** SPINLOOP_FOR() max interactions before return an error. */
#define MAX_SPINLOOP_FOR (WRN_SPINLOOP_FOR+1024)
/** SPINLOOP_FOR() comulative max delay after WRN_SPINLOOP iterations. */
#define MAX_SPINLOOP_FOR_MSEC 5000

/** Sleeping time after WRN_SPINLOOP_FOR iterations (usec for every iteration). */
#define SPINLOOP_FOR_STEP ((MAX_SPINLOOP_FOR_MSEC)*1000/(MAX_SPINLOOP_FOR-WRN_SPINLOOP_FOR))

// safety...
#if SPINLOOP_FOR_STEP<100
#error usleep() step can not be < 100usec
#endif

#if 0
/** Spinloop in case of polling. */
#define SPINLOOP_FOR(res) {\
  if (res!=AXIOM_RET_NOTAVAIL) break;\
  gasneti_sched_yield();\
  gasneti_compiler_fence();\
  gasneti_spinloop_hint();\
}

/** Spinloop in case of polling. */
#define SPINLOOP_FOR(res) {\
  if (++counter>MAX_SPINLOOP_FOR||res!=AXIOM_RET_NOTAVAIL) break;\
  gasneti_sched_yield();\
  gasneti_compiler_fence();\
  gasneti_spinloop_hint();\
}
#endif

/** Spinloop in case of polling. */
/*
 * this macro is used during the axiom_send_SOMETHING()
 * if there is no low level resource available retry for WRN_SPINLOOP_FOR (using a sched_yield between calls)
 * then retries for MAX_SPINLOOP_FOR more times (sleeping for SPINLOOP_FOR_STEP usec between calls)
 * then return an error
 */
#define SPINLOOP_FOR(res) {\
  if (res!=AXIOM_RET_NOTAVAIL) break;\
  if (++counter>=WRN_SPINLOOP_FOR) {\
     if (counter==WRN_SPINLOOP_FOR) logmsg(LOG_WARN,"SPINLOOP_FOR: > %d! switch to slow spinning for safety!",WRN_SPINLOOP_FOR);\
     if (counter>MAX_SPINLOOP_FOR) break;\
     usleep(SPINLOOP_FOR_STEP);\
  } else {\
     gasneti_sched_yield();\
  }\
  gasneti_compiler_fence();\
  gasneti_spinloop_hint();\
}

/**
 * Return if some message are available.
 * @param dev The device to check.
 * @return The number of messages into receiver queue.
 */
static inline int _recv_avail(axiom_dev_t *dev) {
    int res;
    logmsg(LOG_TRACE,"_recv_avail(): start");
    res = axiom_recv_raw_avail(axiom_dev)+axiom_recv_long_avail(axiom_dev);
    logmsg(LOG_TRACE,"_recv_avail(): end");
    return res;
}

/**
 * Send a AXIOM raw messages.
 * In case of resource not available resend the message until done (yielding the processor core).
 *
 * @param dev The AXIOM device.
 * @param node_id The node identification.
 * @param port The port number.
 * @param payload_size The size of the message.
 * @param payload The message.
 * @return The return status (see axiom_send_raw).
 */
static inline axiom_err_t _send_raw(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, axiom_raw_payload_size_t payload_size, void *payload) {
    register axiom_err_t res;
    logmsg(LOG_TRACE,"_send_raw(): start");
    res=axiom_send_raw(axiom_dev, node_log2phy(node_id), port, AXIOM_TYPE_RAW_DATA, payload_size, payload);
    if (res==AXIOM_RET_NOTAVAIL) {
        register int counter=0; // used by SPINLOOP_FOR()!
        for (;;) {
            res=axiom_send_raw(axiom_dev, node_log2phy(node_id), port, AXIOM_TYPE_RAW_DATA, payload_size, payload);
            SPINLOOP_FOR(res);
        }
    }
    logmsg(LOG_TRACE,"_send_raw(): end");
    return res;
}

/**
 * Send a AXIOM long messages.
 * In case of resource not available resend the message until done (yielding the processor core).
 * 
 * @param dev The AXIOM device.
 * @param node_id The node identification.
 * @param port The port number.
 * @param payload_size The size of the message.
 * @param payload The message.
 * @return The return status (see axiom_send_long).
 */
static inline axiom_err_t _send_long(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, axiom_long_payload_size_t payload_size, void *payload)
{
    register axiom_err_t res;
    logmsg(LOG_TRACE,"_send_long(): start");
    res=axiom_send_long(axiom_dev, node_log2phy(node_id), port, payload_size, payload);
    if (res==AXIOM_RET_NOTAVAIL) {
        register int counter=0; // used by SPINLOOP_FOR()!
        for (;;) {
            res=axiom_send_long(axiom_dev, node_log2phy(node_id), port, payload_size, payload);
            SPINLOOP_FOR(res);
        }
    }
    logmsg(LOG_TRACE,"_send_long(): end");
    return res;
}

/**
 * Send a AXIOM long messages using multiple buffer.
 * In case of resource not available resend the message until done (yielding the processor core).
 *
 * @param dev The AXIOM device.
 * @param node_id The node identification.
 * @param port The port number.
 * @param iov An array of buffers.
 * @param iovcnt Size of iov array.
 * @return The return status (see axiom_send_iov_long).
 */
static inline axiom_err_t _send_long_iov(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, struct iovec *iov, int iovcnt)
{
    axiom_long_payload_size_t payload_size=0;
    register axiom_err_t res;
    register int i;
    logmsg(LOG_TRACE,"_send_long_iov(): start");
    for (i=0;i<iovcnt;i++)
        payload_size+=iov[i].iov_len;
    res=axiom_send_iov_long(axiom_dev, node_log2phy(node_id), port, payload_size, iov,iovcnt);
    if (res==AXIOM_RET_NOTAVAIL) {
        register int counter=0; // used by SPINLOOP_FOR()!
        for (;;) {
            res=axiom_send_iov_long(axiom_dev, node_log2phy(node_id), port, payload_size, iov,iovcnt);
            SPINLOOP_FOR(res);
        }
    }
    logmsg(LOG_TRACE,"_send_long_iov(): end");
    return res;
}

/**
 * Read AXIOM raw message.
 *
 * @param dev The AXIOM device.
 * @param node_id The node that send the message (output parameter).
 * @param port The port queue that has the messahe (output parameter).
 * @param payload_size The size of the message (output parameter). Max size of buffer (input parameter).
 * @param payload The buffer receiving the message.
 * @return The exit status (sedd axiom_recv_raw).
 */
static inline axiom_err_t _recv_raw(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, axiom_raw_payload_size_t *payload_size, void *payload) {
    axiom_type_t type = AXIOM_TYPE_RAW_DATA;
    axiom_node_id_t src_id;
    axiom_err_t res;
    logmsg(LOG_TRACE,"_recv_raw(): start");
    res = axiom_recv_raw(axiom_dev, &src_id, port, &type, payload_size, payload);
    if (res==AXIOM_RET_NOTAVAIL) {
        logmsg(LOG_TRACE,"_recv_raw(): end");
        return res;
    }
    if_pt(AXIOM_RET_IS_OK(res)) *node_id = node_phy2log(src_id);
    logmsg(LOG_TRACE,"_recv_raw(): end");
    return res;
}

/**
 * Read AXIOM long message.
 *
 * @param dev The AXIOM device.
 * @param node_id The node that send the message (output parameter).
 * @param port The port queue that has the messahe (output parameter).
 * @param payload_size The size of the message (output parameter). Max size of buffer (input parameter).
 * @param payload The buffer receiving the message.
 * @return The exit status (sedd axiom_recv_raw).
 */
static inline axiom_err_t _recv_long(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, axiom_long_payload_size_t *payload_size, void *payload)
{
    axiom_node_id_t src_id;
    axiom_err_t res;
    logmsg(LOG_TRACE,"_recv_long(): start");
    res = axiom_recv_long(axiom_dev, &src_id, port, payload_size, payload);
    if (res==AXIOM_RET_NOTAVAIL) {
        logmsg(LOG_TRACE,"_recv_long(): end");
        return res;
    }
    if_pt(AXIOM_RET_IS_OK(res)) *node_id = node_phy2log(src_id);
    logmsg(LOG_TRACE,"_recv_long(): end");
    return res;
}

/**
 * Read AXIOM message (raw or long).
 *
 * @param dev The AXIOM device.
 * @param node_id The node that send the message (output parameter).
 * @param port The port queue that has the messahe (output parameter).
 * @param payload_size The size of the message (output parameter). Max size of buffer (input parameter).
 * @param payload The buffer receiving the message.
 * @return The exit status (sedd axiom_recv_raw).
 */
static inline axiom_err_t _recv(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, size_t *payload_size, void *payload)
{
    axiom_type_t type;
    axiom_node_id_t src_id;
    axiom_err_t res;
    logmsg(LOG_TRACE,"_recv(): start");
    res = axiom_recv(axiom_dev, &src_id, port, &type, payload_size, payload);
    if (res==AXIOM_RET_NOTAVAIL) {
    logmsg(LOG_TRACE,"_recv(): end");
        return res;
    }
    if (AXIOM_RET_IS_OK(res)) *node_id = node_phy2log(src_id);
    logmsg(LOG_TRACE,"_recv(): end");
    return res;
}

/**
 * Axiom RDMA request.
 * In case of resource not available resend the message until done (yielding the processor core).
 * Note that this is a ASYNC request: when the function return the DMA may not be completed yet.
 *
 * @param dev The AXIOM device.
 * @param node_id The destination node.
 * @param size The size of the request.
 * @param source_addr The soruce address (current node).
 * @param dest_addr The destination address (node_id node).
 * @param token token that can be used to check rdma progress.
 * @return The exit status (see axiom_rdma_write).
 */
static inline axiom_err_t _rdma_write(axiom_dev_t *dev, gasnet_node_t node_id, size_t size, void *source_addr, void *dest_addr, axiom_token_t *token) {
    register axiom_err_t res;
    gasneti_assert((uintptr_t) source_addr >= (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma);
    gasneti_assert((uintptr_t) dest_addr >= (uintptr_t) gasneti_seginfo[node_id].rdma);
    logmsg(LOG_TRACE,"_rdma_write(): from node %d(phy:%d) %p:%p to node %d(phy:%d) %p:%p for %lu (%lu MiB)",gasneti_mynode,node_log2phy(gasneti_mynode),source_addr,((uint8_t*)source_addr)+size,node_id,node_log2phy(node_id),dest_addr,((uint8_t*)dest_addr)+size-1,(unsigned long)size,(unsigned long)size/1024/1024);
    res=axiom_rdma_write(axiom_dev, node_log2phy(node_id), size, source_addr, dest_addr, token);
    if (res==AXIOM_RET_NOTAVAIL) {
        register int counter=0; // used into SPINLOOP_FOR()!
        for (;;) {
            //res=axiom_rdma_write(axiom_dev, node_log2phy(node_id), size, (uintptr_t) source_addr - (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma, (uintptr_t) dest_addr - (uintptr_t) gasneti_seginfo[node_id].rdma);
            res=axiom_rdma_write(axiom_dev, node_log2phy(node_id), size, source_addr, dest_addr, token);
            SPINLOOP_FOR(res);
        }
    }
    logmsg(LOG_TRACE,"_rdma_write(): end");
    return res;
}

/**
 * Axiom SYNC RDMA request.
 * In case of resource not available resend the message until done (yielding the processor core).
 * Note that this is a SYNC request: when the function return the DMA should be completed.
 *
 * @param dev The AXIOM device.
 * @param node_id The destination node.
 * @param size The size of the request.
 * @param source_addr The soruce address (current node).
 * @param dest_addr The destination address (node_id node).
 * @return The exit status (see axiom_rdma_write).
 */
static inline axiom_err_t _rdma_write_sync(axiom_dev_t *dev, gasnet_node_t node_id, size_t size, void *source_addr, void *dest_addr) {
    register axiom_err_t res;
    gasneti_assert((uintptr_t) source_addr >= (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma);
    gasneti_assert((uintptr_t) dest_addr >= (uintptr_t) gasneti_seginfo[node_id].rdma);
    logmsg(LOG_TRACE,"_rdma_write(): from node %d(phy:%d) %p:%p to node %d(phy:%d) %p:%p for %lu (%lu MiB)",gasneti_mynode,node_log2phy(gasneti_mynode),source_addr,((uint8_t*)source_addr)+size,node_id,node_log2phy(node_id),dest_addr,((uint8_t*)dest_addr)+size-1,(unsigned long)size,(unsigned long)size/1024/1024);
    res=axiom_rdma_write_sync(axiom_dev, node_log2phy(node_id), size, source_addr, dest_addr, NULL);
    if (res==AXIOM_RET_NOTAVAIL) {
        register int counter=0; // used into SPINLOOP_FOR()!
        for (;;) {
            //res=axiom_rdma_write(axiom_dev, node_log2phy(node_id), size, (uintptr_t) source_addr - (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma, (uintptr_t) dest_addr - (uintptr_t) gasneti_seginfo[node_id].rdma);
            res=axiom_rdma_write_sync(axiom_dev, node_log2phy(node_id), size, source_addr, dest_addr, NULL);
            SPINLOOP_FOR(res);
        }
    }
    logmsg(LOG_TRACE,"_rdma_write_sync(): end");
    return res;
}

#else

/**
 * Return if some message are available.
 * @param dev The device to check.
 * @return The number of messages into receiver queue.
 */
static inline int _recv_avail(axiom_dev_t *dev) {
    int res;
    res = axiom_recv_raw_avail(axiom_dev);
    if (!res) {
        res = axiom_recv_long_avail(axiom_dev);
    }
    return res;
}

/**
 * Send a AXIOM raw messages.
 * In case of resource not available resend the message until done (yielding the processor core).
 *
 * @param dev The AXIOM device.
 * @param node_id The node identification.
 * @param port The port number.
 * @param payload_size The size of the message.
 * @param payload The message.
 * @return The return status (see axiom_send_raw).
 */
static inline axiom_msg_id_t _send_raw(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, axiom_raw_payload_size_t payload_size, void *payload) {
    return axiom_send_raw(axiom_dev, node_log2phy(node_id), port, AXIOM_TYPE_RAW_DATA, payload_size, payload);
}

/**
 * Send a AXIOM long messages.
 * In case of resource not available resend the message until done (yielding the processor core).
 *
 * @param dev The AXIOM device.
 * @param node_id The node identification.
 * @param port The port number.
 * @param payload_size The size of the message.
 * @param payload The message.
 * @return The return status (see axiom_send_long).
 */
static inline axiom_msg_id_t _send_long(axiom_dev_t *dev, gasnet_node_t node_id, axiom_port_t port, axiom_long_payload_size_t payload_size, void *payload)
{
 /*
    while (!axiom_send_long_avail(axiom_dev)) {
        gasneti_AMPoll();
    }
*/
    return axiom_send_long(axiom_dev, node_log2phy(node_id), port, payload_size, payload);
}

/**
 * Send a AXIOM long messages using multiple buffer.
 * In case of resource not available resend the message until done (yielding the processor core).
 *
 * @param dev The AXIOM device.
 * @param node_id The node identification.
 * @param port The port number.
 * @param iov An array of buffers.
 * @param iovcnt Size of iov array.
 * @return The return status (see axiom_send_iov_long).
 */

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

/**
 * Read AXIOM raw message.
 *
 * @param dev The AXIOM device.
 * @param node_id The node that send the message (output parameter).
 * @param port The port queue that has the messahe (output parameter).
 * @param payload_size The size of the message (output parameter). Max size of buffer (input parameter).
 * @param payload The buffer receiving the message.
 * @return The exit status (sedd axiom_recv_raw).
 */
static inline axiom_msg_id_t _recv_raw(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, axiom_raw_payload_size_t *payload_size, void *payload) {
    axiom_type_t type = AXIOM_TYPE_RAW_DATA;
    axiom_node_id_t src_id;
    axiom_msg_id_t id = axiom_recv_raw(axiom_dev, &src_id, port, &type, payload_size, payload);
    *node_id = node_phy2log(src_id);
    return id;
}

/**
 * Read AXIOM long message.
 *
 * @param dev The AXIOM device.
 * @param node_id The node that send the message (output parameter).
 * @param port The port queue that has the messahe (output parameter).
 * @param payload_size The size of the message (output parameter). Max size of buffer (input parameter).
 * @param payload The buffer receiving the message.
 * @return The exit status (sedd axiom_recv_raw).
 */
static inline axiom_msg_id_t _recv_long(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, axiom_long_payload_size_t *payload_size, void *payload)
{
    axiom_node_id_t src_id;
    axiom_msg_id_t id = axiom_recv_long(axiom_dev, &src_id, port, payload_size, payload);
    *node_id = node_phy2log(src_id);
    return id;
}

/**
 * Read AXIOM message (raw or long).
 *
 * @param dev The AXIOM device.
 * @param node_id The node that send the message (output parameter).
 * @param port The port queue that has the messahe (output parameter).
 * @param payload_size The size of the message (output parameter). Max size of buffer (input parameter).
 * @param payload The buffer receiving the message.
 * @return The exit status (sedd axiom_recv_raw).
 */
static inline axiom_msg_id_t _recv(axiom_dev_t *dev, gasnet_node_t *node_id, axiom_port_t *port, size_t *payload_size, void *payload)
{
    axiom_type_t type;
    axiom_node_id_t src_id;
    axiom_msg_id_t id = axiom_recv(axiom_dev, &src_id, port, &type, payload_size, payload);
    *node_id = node_phy2log(src_id);
    return id;
}

/**
 * Axiom RDMA request.
 * Note that this is a ASYNC request: when the function return the DMA may not be completed yet.
 *
 * @param dev The AXIOM device.
 * @param node_id The destination node.
 * @param size The size of the request.
 * @param source_addr The soruce address (current node).
 * @param dest_addr The destination address (node_id node).
 * @param token token that can be used to check rdma progress.
 * @return The exit status (see axiom_rdma_write).
 */
static inline axiom_err_t _rdma_write(axiom_dev_t *dev, gasnet_node_t node_id, size_t size, void *source_addr, void *dest_addr, axiom_token_t *token) {
    gasneti_assert((uintptr_t) source_addr >= (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma);
    gasneti_assert((uintptr_t) dest_addr >= (uintptr_t) gasneti_seginfo[node_id].rdma);
    //return axiom_rdma_write(axiom_dev, node_log2phy(node_id), 0, size, (uintptr_t) source_addr - (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma, (uintptr_t) dest_addr - (uintptr_t) gasneti_seginfo[node_id].rdma);
    logmsg(LOG_TRACE,"_rdma_write(): from node %d(phy:%d) %p:%p to node %d(phy:%d) %p:%p for %lu (%lu MiB)",gasneti_mynode,node_log2phy(gasneti_mynode),source_addr,((uint8_t*)source_addr)+size,node_id,node_log2phy(node_id),dest_addr,((uint8_t*)dest_addr)+size-1,(unsigned long)size,(unsigned long)size/1024/1024);
    return axiom_rdma_write(axiom_dev, node_log2phy(node_id), size, source_addr, dest_addr, token);
}

/**
 * Axiom SYNC RDMA request.
 * Note that this is a SYNC request: when the function return the DMA should be completed.
 *
 * @param dev The AXIOM device.
 * @param node_id The destination node.
 * @param size The size of the request.
 * @param source_addr The soruce address (current node).
 * @param dest_addr The destination address (node_id node).
 * @return The exit status (see axiom_rdma_write).
 */
static inline axiom_err_t _rdma_write_sync(axiom_dev_t *dev, gasnet_node_t node_id, size_t size, void *source_addr, void *dest_addr) {
    gasneti_assert((uintptr_t) source_addr >= (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma);
    gasneti_assert((uintptr_t) dest_addr >= (uintptr_t) gasneti_seginfo[node_id].rdma);
    //return axiom_rdma_write(axiom_dev, node_log2phy(node_id), 0, size, (uintptr_t) source_addr - (uintptr_t) gasneti_seginfo[gasneti_mynode].rdma, (uintptr_t) dest_addr - (uintptr_t) gasneti_seginfo[node_id].rdma);
    logmsg(LOG_TRACE,"_rdma_write(): from node %d(phy:%d) %p:%p to node %d(phy:%d) %p:%p for %lu (%lu MiB)",gasneti_mynode,node_log2phy(gasneti_mynode),source_addr,((uint8_t*)source_addr)+size,node_id,node_log2phy(node_id),dest_addr,((uint8_t*)dest_addr)+size-1,(unsigned long)size,(unsigned long)size/1024/1024);
    return axiom_rdma_write_sync(axiom_dev, node_log2phy(node_id), size, source_addr, dest_addr, NULL);
}

#endif

#ifdef _BLOCK_ON_LOOP_EPOLL

// epoll event types
#define UNKNOWN_EVENT 0
#define RAW_EVENT   1
#define LONG_EVENT  2
#define ASYNC_RDMA_EVENT  3
#define CHECK_EVENT 4
#define MAX_EVENT CHECK_EVENT
// epoll event names
static const char *event2str[]={"UNKNOW","RAW","LONG","ASYNC_RDMA","CHECK"};

// queue management
/** A thread-local-storage key to associate a bitmp mask to every thread. */
pthread_key_t gasnetc_thread_key;
/** A thread conter. To compute the bitmap mask associated with every thread. */
int gasnetc_thread_idx;

#ifdef EVENTFD_PER_THREAD
/** An array of event-file-descriptor (a descripto for every thread). */
static int evfds[GASNETI_MAX_THREADS];
static uint64_t fastmask[]={
    0x0000000000000001,0x0000000000000002,0x0000000000000004,0x0000000000000008,
    0x0000000000000010,0x0000000000000020,0x0000000000000040,0x0000000000000080,
    0x0000000000000100,0x0000000000000200,0x0000000000000400,0x0000000000000800,
    0x0000000000001000,0x0000000000002000,0x0000000000004000,0x0000000000008000,
    0x0000000000010000,0x0000000000020000,0x0000000000040000,0x0000000000080000,
    0x0000000000100000,0x0000000000200000,0x0000000000400000,0x0000000000800000,
    0x0000000001000000,0x0000000002000000,0x0000000004000000,0x0000000008000000,
    0x0000000010000000,0x0000000020000000,0x0000000040000000,0x0000000080000000,
    0x0000000100000000,0x0000000200000000,0x0000000400000000,0x0000000800000000,
    0x0000001000000000,0x0000002000000000,0x0000004000000000,0x0000008000000000,
    0x0000010000000000,0x0000020000000000,0x0000040000000000,0x0000080000000000,
    0x0000100000000000,0x0000200000000000,0x0000400000000000,0x0000800000000000,
    0x0001000000000000,0x0002000000000000,0x0004000000000000,0x0008000000000000,
    0x0010000000000000,0x0020000000000000,0x0040000000000000,0x0080000000000000,
    0x0100000000000000,0x0200000000000000,0x0400000000000000,0x0800000000000000,
    0x1000000000000000,0x2000000000000000,0x4000000000000000,0x8000000000000000
    };
#else
/** EPoll file descriptor. Shared between all threads. */
static int epfd; // file descriptor for epoll
/** Event file descriptor. To signal the need of recheck conditions. */
static int evfd; // file descriptor for eventfd
#endif

/**
 * Used to free the buffer allocated and associated with gasnetc_thread_key.
 * @param ptr The buffer pointer.
 */
static void gasneti_freethread_keymask(void *ptr) {
    gasneti_free(ptr);
}

/**
 * Data initialization to use epoll/eventfd block.
 */
static void init_epoll_block() {
    int res;
#ifndef EVENTFD_PER_THREAD
    struct epoll_event epe;
    int h0,h1,h2;
    //
    // axiom file descriptors
    //
    axiom_err_t err=axiom_get_fds(axiom_dev,&h0,&h1,&h2);
    if (!AXIOM_RET_IS_OK(err)) {
        logmsg(LOG_WARN,"init_epoll_block(): axiom_get_fds return %d",err);
        gasneti_fatalerror("FATAL on axiom_get_fds()!");
    }
    epfd=epoll_create(4);
    if (epfd<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_create() %d",errno);
        gasneti_fatalerror("FATAL on epoll_create()!");
    }
    epe.events=EPOLLIN;
    epe.data.u32=RAW_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,h0,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctrl() h0 %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
    epe.data.u32=LONG_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,h1,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctl() h1 %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
    epe.data.u32=ASYNC_RDMA_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,h2,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctl() h2 %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
    //
    // eventfd file descriptor
    //
    evfd=eventfd(0,EFD_NONBLOCK);
    if (evfd<0) {
        logmsg(LOG_WARN,"init_epoll_block(): eventfd() %d",errno);
        gasneti_fatalerror("FATAL on eventfd()!");
    }
    epe.data.u32=CHECK_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,evfd,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctl() evfd %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
#endif
    //
    // threads queue management
    //
    res=pthread_key_create(&gasnetc_thread_key,gasneti_freethread_keymask);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): pthread_key_create() errno %d",errno);
        gasneti_fatalerror("FATAL on pthread_key_create()!");
    }
    gasnetc_thread_idx=0;
}

// This is a simple/fast implementation of queue management for using eventd as signal condition
// constrain: max 64 thread for process
// see: gasnet_core_fwd.h to

// Every thread has a bit in a mask that check the presence of the thread into the queue.
// A new bitmask is computed into gasneti_get_new_thread_keymask().
// gasneti_polluntil() use the macro gasneti_define_thread_keymask() to get the keymask and call gasneti_get_new_thread_keymask() if not found.
// see: gasnet_core_help.h

/** Active queue. Contains threads that have test condition and will block on the epoll. */
static uint64_t act_wait_bitmap=0;
/** Saved queue: Contains threads that are blocked into the epoll that must check conditions. */
static uint64_t svd_wait_bitmap=0;

#ifdef EVENTFD_PER_THREAD
/**
 * Compute a new bitmap mask value for a thread.
 * @return A pointer where the bitmap is stored.
 */
gasnetc_tls_t *gasneti_get_new_thread_keymask() {
    gasnetc_tls_t *ptr;
    int res,v;
    axiom_err_t err;
    struct epoll_event epe;
    int h0,h1,h2;
    int epfd,evfd;
    //
    ptr=(gasnetc_tls_t*)gasneti_malloc(sizeof(gasnetc_tls_t));
    gasneti_assert(ptr!=NULL);
    res=pthread_setspecific(gasnetc_thread_key,ptr);
    gasneti_assert(res==0);
#if defined(GASNET_DEBUG)&&defined(GASNET_DEBUG_EMIT_THREAD)
    v=gasnetc_self(); // so thread debug output index and key mask are in sync
#else
    v=__sync_fetch_and_add(&gasnetc_thread_idx,1);
#endif
    gasneti_assert(v<GASNETI_MAX_THREADS);
    ptr->idx=v;
    ptr->keymask=(((uint64_t)1)<<v);
    //
    // axiom file descriptors
    //
    err=axiom_get_fds(axiom_dev,&h0,&h1,&h2);
    if (!AXIOM_RET_IS_OK(err)) {
        logmsg(LOG_WARN,"init_epoll_block(): axiom_get_fds return %d",err);
        gasneti_fatalerror("FATAL on axiom_get_fds()!");
    }
    epfd=epoll_create(4);
    if (epfd<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_create() %d",errno);
        gasneti_fatalerror("FATAL on epoll_create()!");
    }
    epe.events=EPOLLIN;
    epe.data.u32=RAW_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,h0,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctrl() h0 %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
    epe.data.u32=LONG_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,h1,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctl() h1 %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
    epe.data.u32=ASYNC_RDMA_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,h2,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctl() h2 %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
    //
    // eventfd file descriptor
    //
    evfd=eventfd(0,EFD_NONBLOCK);
    if (evfd<0) {
        logmsg(LOG_WARN,"init_epoll_block(): eventfd() %d",errno);
        gasneti_fatalerror("FATAL on eventfd()!");
    }
    epe.data.u32=CHECK_EVENT;
    res=epoll_ctl(epfd,EPOLL_CTL_ADD,evfd,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): epoll_ctl() evfd %d",errno);
        gasneti_fatalerror("FATAL on epoll_ctl()!");
    }
    //
    ptr->epfd=epfd;
    ptr->evfd=evfd;
    evfds[ptr->idx]=evfd;
    //
    logmsg(LOG_INFO,"thread index %02d mask 0x%08lx",ptr->idx,ptr->keymask);
    return ptr;
}
#else
/**
 * Compute a new bitmap mask value for a thread.
 * @return A pointer where the bitmap is stored.
 */
uint64_t *gasneti_get_new_thread_keymask() {
    uint64_t *ptr;
    int res,v;
    ptr=(uint64_t*)gasneti_malloc(sizeof(uint64_t));
    gasneti_assert(ptr!=NULL);
    res=pthread_setspecific(gasnetc_thread_key,ptr);
    gasneti_assert(res==0);
#if defined(GASNET_DEBUG)&&defined(GASNET_DEBUG_EMIT_THREAD)
    v=gasnetc_self(); // so thread debug output index and key mask are in sync
#else
    v=__sync_fetch_and_add(&gasnetc_thread_idx,1);
#endif
    gasneti_assert(v<GASNETI_MAX_THREADS);
    *ptr=(((uint64_t)1)<<v);
    logmsg(LOG_INFO,"thread index mask 0x%08lx",*ptr);
    return ptr;
}
#endif

// experimental.... (and unsafe)
// try to postpone the LOCK() from the main mutex after the check for eventdfd
// so there is a chance for a thread not to wake-up if has already test condition
//#define THREADSAFE_QUEUE_OPS

/**
 * Add actual thread que into saved queue.
 */
static inline void save_act_into_svd() {
    svd_wait_bitmap|=act_wait_bitmap;
    //__sync_fetch_and_or(&svd_wait_bitmap,act_wait_bitmap);
}

/**
 * Insert a thread into the actual queue.
 * @param mask The thread bitmap mask.
 */
static inline void insert_into_act(uint64_t mask) {
    act_wait_bitmap|=mask;
    //__sync_fetch_and_or(&act_wait_bitmap,mask);
}

/**
 * Remove a thread from the actual queue.
 * @param mask The thread bitmap mask.
 * @return Return 0 if the queue is empty (after the removing) another number otherwise.
 */
static inline int remove_from_act(uint64_t mask) {
    return act_wait_bitmap&=(~mask);
    //return __sync_and_and_fetch(&act_wait_bitmap,~mask);
}


/**
 * Remove a thread from the saved queue.
 * @param mask The thread bitmap mask.
 * @return Return 0 if the queue is empty (after the removing) another number otherwise.
 */
static inline int remove_from_svd(uint64_t mask) {
    return svd_wait_bitmap&=(~mask);
    //return __sync_and_and_fetch(&svd_wait_bitmap,~mask);
}

/**
 * Test if a thread is into the saved queue.
 * @param mask The thread bitmap mask.
 * @return 0 if not presence another number otherwise
 */
static inline int am_i_into_svd(uint64_t mask) {
    return svd_wait_bitmap&mask;
}

/**
 * Test if the saved queue is empty.
 * @return 0 if is not empty another number otherwise.
 */
static inline int is_svd_empty() {
    return svd_wait_bitmap==0;
}

/** A mutex to access common resource (if using _BLOCK_ON_LOOP_CONDWAIT).*/
MUTEX_t gasnetc_mut = MUTEX_INITIALIZER;

/**
 * Raise a check signal on all blocked thread.
 */
static inline void raise_check_event() {
    LOCK(gasnetc_mut);
    if (logmsg_is_enabled(LOG_DEBUG)) {
        uint64_t bitmap=svd_wait_bitmap;
        save_act_into_svd();
        logmsg(LOG_DEBUG,"raise_check_event: bitmaps act=0x%08lx before svd=0x%08lx after svd=0x%08lx",act_wait_bitmap,bitmap,svd_wait_bitmap);
    } else {
        save_act_into_svd();
    }
    if (!is_svd_empty()) {
#ifdef EVENTFD_PER_THREAD
        register uint64_t bmp=svd_wait_bitmap;
        int idx;
        while (bmp!=0) {
            idx=__builtin_ctzl(bmp);
            logmsg(LOG_DEBUG,"raise_check_event: WRITE check event for %02d (bmp=0x%08lx)",idx,bmp);
            if (eventfd_write(evfds[idx],1)<0) {
                logmsg(LOG_ERROR,"raise_check_event: eventfd_write errno %d",errno);
                gasneti_fatalerror("eventfd_write() error!");
            }
            bmp&=~(((uint64_t)1)<<idx);
        }
#else
        logmsg(LOG_DEBUG,"raise_check_event: WRITE check event");
        if (eventfd_write(evfd,1)<0) {
            logmsg(LOG_ERROR,"raise_check_event: eventfd_write errno %d",errno);
            gasneti_fatalerror("eventfd_write() error!");
        }
#endif
    }
    UNLOCK(gasnetc_mut);
}

 /**
  * Block waiting condition.
  * Must be called after acquaring the gasnetc_mut mutex.
  * Should be an inline/macro function (but not for now because debugging).
  * Used from gasneti_polluntil().
  */
#ifdef EVENTFD_PER_THREAD
int gasnetc_block_on_condition(uint64_t keymask, int epfd, int evfd) {
    struct epoll_event evt;
    int res;

    insert_into_act(keymask);
    logmsg(LOG_DEBUG,"queue bitmaps block act=0x%08lx svd=0x%08lx",act_wait_bitmap,svd_wait_bitmap);
    UNLOCK(gasnetc_mut);

    logmsg(LOG_INFO,"Block until cond....");
    res=epoll_wait(epfd,&evt,1,-1);
    if (res<0) {
        logmsg(LOG_WARN,"block_on_condition: epoll_wait errno %d",errno);
        return -1;
    }
    logmsg(LOG_INFO,"UNBLOCKED for event '%s'!",evt.data.u32>MAX_EVENT?"out of range error":event2str[evt.data.u32]);

    gasneti_compiler_fence();
    gasneti_spinloop_hint();

    LOCK(gasnetc_mut);
    logmsg(LOG_DEBUG,"queue bitmaps unblk act=0x%08lx svd=0x%08lx",act_wait_bitmap,svd_wait_bitmap);
    remove_from_act(keymask);
    if (am_i_into_svd(keymask)) {
        eventfd_t value;
        remove_from_svd(keymask);
        logmsg(LOG_DEBUG,"READ check event");
        if (eventfd_read(evfd,&value)<0) {
            logmsg(LOG_ERROR,"gasnetc_block_on_condition: eventfd_read errno %d",errno);
            gasneti_fatalerror("eventfd_read() error!");
        }
        logmsg(LOG_DEBUG,"svd queue bitmap after removing myself svd=0x%08lx",svd_wait_bitmap);
    }
    return evt.data.u32;
}
#else
int gasnetc_block_on_condition(uint64_t keymask) {
    struct epoll_event evt;
    int res;

    insert_into_act(keymask);
    logmsg(LOG_DEBUG,"queue bitmaps block act=0x%08lx svd=0x%08lx",act_wait_bitmap,svd_wait_bitmap);
    UNLOCK(gasnetc_mut);

    gasneti_sched_yield(); // not needed but can reduce the wakeup of threads that have already check the condition

    logmsg(LOG_INFO,"Block until cond....");
    res=epoll_wait(epfd,&evt,1,-1);
    if (res<0) {
        logmsg(LOG_WARN,"block_on_condition: epoll_wait errno %d",errno);
        return -1;
    }
    logmsg(LOG_INFO,"UNBLOCKED for event '%s'!",evt.data.u32>MAX_EVENT?"out of range error":event2str[evt.data.u32]);

    gasneti_compiler_fence();
    gasneti_spinloop_hint();

    LOCK(gasnetc_mut);
    logmsg(LOG_DEBUG,"queue bitmaps unblk act=0x%08lx svd=0x%08lx",act_wait_bitmap,svd_wait_bitmap);
    remove_from_act(keymask);
    if (am_i_into_svd(keymask)) {
        if (remove_from_svd(keymask)==0) {
            eventfd_t value;
            logmsg(LOG_DEBUG,"READ check event");
            if (eventfd_read(evfd,&value)<0) {
                logmsg(LOG_ERROR,"gasnetc_block_on_condition: eventfd_read errno %d",errno);
                gasneti_fatalerror("eventfd_read() error!");
            }
        }
        logmsg(LOG_DEBUG,"svd queue bitmap after removing myself svd=0x%08lx",svd_wait_bitmap);
    }

    return evt.data.u32;
}
#endif

// __BLOCK_ON_LOOP_EPOLL
#endif

#ifdef _BLOCK_ON_LOOP_CONDWAIT

/** A mutex to access common resource (if using _BLOCK_ON_LOOP_CONDWAIT).*/
 gasneti_mutex_t gasnetc_mut = GASNETI_MUTEX_INITIALIZER;
 /** A condition variable to signal/wait threads. */
 gasneti_cond_t gasnetc_cond = GASNETI_COND_INITIALIZER;

 /**
  * Block waiting condition.
  * Must be called after acquaring the gasnetc_mut mutex.
  * Should be an inline/macro function (but not for now because debugging).
  */
void gasnetc_block_on_condition() {

     // must check if there are working to do before blocking!!!
     if (_recv_avail(axiom_dev)>0) return;

     logmsg(LOG_INFO,"Block until cond....");
     gasneti_cond_wait(&gasnetc_cond, &gasnetc_mut);
     logmsg(LOG_INFO,"UNBLOCKED!");
     gasneti_compiler_fence();
     gasneti_spinloop_hint();
}

/**
 * A high priority thread usde to control low level queues status.
 * It signals other blocked thread when there is work to do.
 * @param dummy Not used argument.
 * @return Nothing.
 */
static void *fast_epoller(void *dummy) {
    int h0,h1,h2;
    int ep;
    struct epoll_event epe;
    int res;
    axiom_err_t err=axiom_get_fds(axiom_dev,&h0,&h1,&h2);
    if (!AXIOM_RET_IS_OK(err)) {
        logmsg(LOG_WARN,"fast_epoller(): axiom_get_fds return %d",err);
        gasneti_fatalerror("FATAL!");
    }
    ep=epoll_create(3);
    if (ep<0) {
        logmsg(LOG_WARN,"fast_epoller(): epoll_create() %d",errno);
        gasneti_fatalerror("FATAL!");
    }
    epe.events=EPOLLIN|EPOLLET; // EdgeTrigger DANGER! (fd should be not blocking and there is the possibility of combined events report)
    epe.data.ptr=NULL;
    epe.data.fd=h0;
    epe.data.u32=0;
    epe.data.u64=0;
    res=epoll_ctl(ep,EPOLL_CTL_ADD,h0,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"fast_epoller(): epoll_ctrl() h0 %d",errno);
        gasneti_fatalerror("FATAL!");
    }
    epe.data.fd=h1;
    res=epoll_ctl(ep,EPOLL_CTL_ADD,h1,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"fast_epoller(): epoll_ctrl() h1 %d",errno);
        gasneti_fatalerror("FATAL!");
    }
    epe.data.fd=h2;
    res=epoll_ctl(ep,EPOLL_CTL_ADD,h2,&epe);
    if (res<0) {
        logmsg(LOG_WARN,"fast_epoller(): epoll_ctrl() h2 %d",errno);
        gasneti_fatalerror("FATAL!");
    }

    for (;;) {
        logmsg(LOG_DEBUG,"EPolling for events...");
        res=epoll_wait(ep,&epe,1,-1);
        logmsg(LOG_INFO,"EPolled event!");
        if (res<0) {
            logmsg(LOG_WARN,"fast_epoller(): epoll_wait %d",errno);
        } else {
            /*
             * Why a mutex lock/unlock?
             * To not miss some wake up of blocked thread made by this cond_broadcast()!
             * The epoll() is set on EDGE TRIGGER so it wake-up this thread when there is a change into the file descriptor read queue.
             * The tested conditions on the blocked threads are made by gasnet library users so are unrelated to this condition (data available into the axiom queue).
             * If the cond_broadcast() is sent when a thread is between the "user" tested condition and the cond_wait() the signal is lost for it.
             * The lock/unlock mutex ensure that this signal is never lost.
             * Note: it could be ok to miss some signals for some thread if the latency is not a problem but can dead-lock the application if "there is no other network activity" (i.e.
             * the application is exiting)
             */
            gasneti_mutex_lock(&gasnetc_mut);
            gasneti_cond_broadcast(&gasnetc_cond);
            gasneti_mutex_unlock(&gasnetc_mut);
        }
    }

    close(ep);
}

#endif

/*
 *
 *
 * signal handlers
 *
 *
 *
 */

/**
 * Termination signal handler.
 *
 * @param sig Signal number.
 */
static void my_signal_handler(int sig) {
    if (sig == SIGQUIT) {
        gasnet_exit(42);
    } else {
        struct sigaction sa;
        memset(&sa, 0, sizeof (sa));
        sa.sa_handler = SIG_DFL;
        sigaction(sig, &sa, NULL);
    }
    raise(sig);
}

/**
 * Initialize signal handlers.
 */
static void init_signal_manager() {
    struct sigaction sa;
    memset(&sa, 0, sizeof (sa));
    sa.sa_handler = my_signal_handler;
    //if_pf(sigaction(SIGQUIT, &sa, NULL) != 0) gasneti_fatalerror("Installing %s signal handler!", "SIGSEGV");
}

/*
 *
 *
 * gasnet initialization
 *
 *
 *
 */

/** Default axiom barrier identification. */
#define BARRIER_ID 7

/**
 * Internal gasnet initial barrier.
 * To synchornize jobs nodes.
 */
static void gasnetc_bootstrapBarrier(void) {
    int res;
    res = axrun_sync(BARRIER_ID, 1);
    if_pf(res != 0) {
        gasneti_fatalerror("failure in gasnetc_bootstrapBarrier() using axrun_sync()");
    }
}

/**
 * Called at startup to check configuration sanity.
 */
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

/**
 * Send a RAW message to all nodes (of the gasnet application).
 * The first error abort the transmission.
 *
 * @param packet The data to be sent.
 * @param port The receiver port.
 * @param packet_size The size of the packet.
 * @param mynode My node number.
 * @param num_nodes The number of node of the gasnet application.
 * @return  The exti status (see _send_raw).
 */
static int send_to_all(void *packet, axiom_port_t port, axiom_raw_payload_size_t packet_size, gasnet_node_t mynode, gasnet_node_t num_nodes) {
    gasnet_node_t node;
    axiom_err_t ret;
    for (node = 0; node < num_nodes; node++) {
        if (node == mynode) continue;
        ret = _send_raw(axiom_dev, node, port, packet_size, packet);
        if (!AXIOM_RET_IS_OK(ret)) break;
    }
    return AXIOM_RET_IS_OK(ret)? GASNET_OK : -1;
}

/**
 * strncpy() with \0 termination.
 * @param dest Destination string.
 * @param src Source string.
 * @param n Destination max size.
 * @return Destination string.
 */
static inline char *_strncpy(char *dest, const char *src, size_t n) {
    char *res = strncpy(dest, src, n);
    dest[n - 1] = '\0';
    return res;
}

/**
 * BSD strlcpy.
 * @param dst Destination string.
 * @param src Source string.
 * @param size Size of destination string.
 * @return Destination string.
 */
extern size_t strlcpy(char *dst, const char *src, size_t size);

/**
 * Gasnet internal initialization bootstrap.
 *
 * @param argc_p main() argc pointer.
 * @param argv_p main() *argv[] pointer.
 * @return GASNET_OK in case of success otherwise another values.
 */
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
    num_phy_nodes = axiom_get_num_nodes(axiom_dev)+1;
    gasnetc_nodes_phy2log = (gasnet_node_t*) gasneti_malloc(sizeof (gasnet_node_t) * num_phy_nodes);
    gasneti_assert(gasnetc_nodes_log2phy != NULL);
    for (i = 0; i < num_phy_nodes; i++) gasnetc_nodes_phy2log[i] = INVALID_LOGICAL_NODE;
    nf = nl = 0;
    while (job_nodes != 0) {
        if (job_nodes & 0x1) {
            gasneti_assert(nl < num_job_nodes);
            gasnetc_nodes_log2phy[nl] = nf;
            gasneti_assert(nf <= num_phy_nodes);
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

    if (logmsg_is_enabled(LOG_DEBUG)) {
        int n;
        logmsg(LOG_DEBUG,"phy2log table");
        for (n=1;n<num_phy_nodes;n++) {
            logmsg(LOG_DEBUG,"  phy: %2d -> log: %2d",n,node_phy2log(n));
        }
        logmsg(LOG_DEBUG,"log2phy table");
        for (n=0;n<num_job_nodes;n++) {
            logmsg(LOG_DEBUG,"  log: %2d -> phy: %2d",n,node_log2phy(n));
        }
    }
        
    return GASNET_OK;
}

#ifdef _BLOCKING_MODE
/** A mutex for synchronization for blocking mode during poll.*/
static MUTEX_t poll_mutex;
#endif

/** Indicate if the rdma allocation has been done. */
static int gasnetc_rdma_allocation_done=0;

/**
 * Initialization for async request.
 */
static void async_init();

/**
 * Axion conduit initialization.
 *
 * @param argc main() argc pointer.
 * @param argv main() *argv[] pointer.
 * 
 * @return GASNET_OK in case of success otherwise another value.
 */
static int gasnetc_init(int *argc, char ***argv) {

    axiom_err_t ret;
    int res;

    // debug logging message activation
    logmsg_init();
    logmsg(LOG_INFO,"gasnetc_init() start");

    /* A thread local storage key. */
    res=pthread_key_create(&gasnetc_thread_key,NULL);
    if (res<0) {
        logmsg(LOG_WARN,"init_epoll_block(): pthread_key_create() errno %d",errno);
        gasneti_fatalerror("FATAL on pthread_key_create()!");
    }

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
    init_signal_manager();
    {
#ifdef _BLOCKING_MODE
        INIT_MUTEX(poll_mutex);
        axiom_dev = axiom_open(NULL);
#ifdef _ASYNC_RDMA_MODE
        async_init();
#endif
#else
        struct axiom_args openargs;
        openargs.flags = AXIOM_FLAG_NOBLOCK;
        axiom_dev = axiom_open(&openargs);
#ifdef _ASYNC_RDMA_MODE
        async_init();
#endif
/*
        axiom_dev_blocking=axiom_open(NULL);
        if_pf(axiom_dev_blocking == NULL) {
            char s[255];
            snprintf(s, sizeof (s), "Can NOT open axiom device in blocking mode.");
            GASNETI_RETURN_ERRR(RESOURCE, s);
        }
*/
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

#ifdef _BLOCK_ON_LOOP_EPOLL
    init_epoll_block();
#endif

#ifdef _BLOCK_ON_LOOP_CONDWAIT
    {
        // start high priority thread to signal low level queues state...
        pthread_t pth;
        pthread_attr_t attr;
        struct sched_param params;
        pthread_attr_init(&attr);
        pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setscope(&attr,PTHREAD_SCOPE_PROCESS);
        pthread_attr_setschedpolicy(&attr,SCHED_FIFO);
        params.__sched_priority=sched_get_priority_max(SCHED_FIFO);
        pthread_attr_setschedparam(&attr,&params);
        int res=pthread_create(&pth,&attr,fast_epoller,NULL);
        if (res<0) {
            gasneti_fatalerror("pctread_create fast_epoller() thread!!!!");
        }
    }
#endif

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
        //gasneti_MaxLocalSegmentSize = AXIOM_MAX_SEGMENT_SIZE;
        gasneti_MaxLocalSegmentSize = (256L*1024*1024);

        /* (###) Add code here to find the MIN(MaxLocalSegmentSize) over all nodes */
        //gasneti_MaxGlobalSegmentSize = AXIOM_MAX_SEGMENT_SIZE;
        gasneti_MaxGlobalSegmentSize = (256L*1024*1024);

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

    //
    // wait mode hint
    //
    {
        char *value = getenv("GASNET_AXIOM_WAITMODE");
        if (value != NULL) {
            if (strncasecmp(value,"SPIN",5)==0) {
                gasneti_set_waitmode(GASNET_WAIT_SPIN);
                logmsg(LOG_INFO,"setting waitmode hint to SPIN");
            } else if (strncasecmp(value,"BLOCK",6)==0) {
                gasneti_set_waitmode(GASNET_WAIT_BLOCK);
                logmsg(LOG_INFO,"setting waitmode hint to BLOCK");
            } else if (strncasecmp(value,"SPINBLOCK",10)==0) {
                gasneti_set_waitmode(GASNET_WAIT_SPINBLOCK);
                logmsg(LOG_INFO,"setting waitmode hint to SPINBLOCK");
            } else {
                logmsg(LOG_WARN,"Unknown GASNET_AXIOM_WAITMODE value '%s' (legal values: SPIN, BLOCK, SPINBLOCK), setting SPINGBLOCK",value);
                gasneti_set_waitmode(GASNET_WAIT_SPINBLOCK);
            }
        } else {
            logmsg(LOG_INFO,"no GASNET_AXIOM_WAITMODE environment variable found, waitmode default to SPINBLOCK");
            gasneti_set_waitmode(GASNET_WAIT_SPINBLOCK);
        }
    }

    gasneti_auxseg_init(); /* adjust max seg values based on auxseg */

    logmsg(LOG_INFO,"gasnetc_init() end (without error)");

    return GASNET_OK;
}

/**
 * Gasnet initialization.
 *
 * @param argc main() argc pointer.
 * @param argv main() *argv[] pointer.
 *
 * @return GASNET_OK on success.
 */
extern int gasnet_init(int *argc, char ***argv) {
    int retval = gasnetc_init(argc, argv);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasneti_trace_init(argc, argv);

    return GASNET_OK;
}

/** 
 * Used to check for unique handler id.
 * If ZERO the id is not used. If ONE the handler id is already used.
 */
static char checkuniqhandler[256] = {0};

/**
 * Default handler registration.
 *
 * @param table Handler table.
 * @param numentries Num entries.
 * @param lowlimit Low handler id.
 * @param highlimit High handler id.
 * @param dontcare If ONE then if a handler is not specified into handler table then assign a internal generated handler id.
 * @param numregistered Num handler registered.
 * @return GASNET_OK is success.
 */
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

/*
 *
 *
 * DMA buffers
 *
 *
 *
 */

// used when a user request an Active Long message from an area outside RDMA mapped memory

// is using less than 32 buffers then a uint32_t is used
#if GASNETC_NUM_BUFFERS<=32

/**
 * Buffer state.
 * The n-th bit specifies state on n-th buffer.
 * ONE free or ZERO allocated.
 */
static volatile uint32_t rdma_buf_state = 0xffffffff;

/** One costant. */
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

// is using less than 64 buffers then a uint64_t is used
#elif GASNETC_NUM_BUFFERS<=64

/**
 * Buffer state.
 * The n-th bit specifies state on n-th buffer.
 * ONE free or ZERO allocated.
 */
static volatile uint64_t rdma_buf_state = 0xffffffffffffffff;

/** One costant. */
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
// is using MORE than 64 buffers no implementation
#error GASNETC_NUM_BUFFERS must be <=64
#endif

/** A mutex for buffer management. (need in PAR mode). */
static MUTEX_t rdma_buf_mutex;
/** A condition variable for buffer management. (need in PAR mode). */
static COND_t rdma_buf_cond;

/**
 * Initialize DMA buffer management.
 */
static void init_rdma_buf() {
    INIT_MUTEX(rdma_buf_mutex);
    INIT_COND(rdma_buf_cond);
}

/**
 * Allocate a DMA buffer.
 * Note tha this function can block (into a polling state) the caller.
 * @return The buffer allocated.
 */
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

/**
 * Free a DMA buffer.
 * @param buf The buffer already allocated.
 */
static void free_rdma_buf(void *buf) {
    int idx = ((uint8_t*) buf - (uint8_t*) gasneti_seginfo[gasneti_mynode].base) / GASNETC_BUFFER_SIZE;
    LOCK(rdma_buf_mutex);
    rdma_buf_state |= (ONE << idx);
    SIGNAL_COND(rdma_buf_cond);
    UNLOCK(rdma_buf_mutex);
}

/*
 *
 *
 * Conduit attach
 *
 *
 *
 */

/**
 * Axiom conduit attach.
 * Note tha some parameter are pointer because this function can increase the values.
 * 
 * @param table Handler table.
 * @param numentries Num entries into handler table.
 * @param segsize Requested memory segment size.
 * @param minheapoffset Requested minmum heap offest.
 * @return GASNET_OK in success.
 */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
        uintptr_t segsize, uintptr_t minheapoffset) {
    void *segbase = NULL;

    logmsg(LOG_DEBUG,"gasnetc_attach() start");

    GASNETI_TRACE_PRINTF(C, ("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
            numentries, (unsigned long) segsize, (unsigned long) minheapoffset));

    if (!gasneti_init_done)
        GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
    if (gasneti_attach_done)
        GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

    logmsg(LOG_INFO,"gasnet_attach(): requested by user %lu (%lu MiB)",segsize,segsize/1024/1024);

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
        size_t mysize,mysizereq,sharedsize;
        int delta, ret;
        gasnet_node_t i;
        void *rdmabase;

        // MG
        gasneti_assert(GASNET_PAGESIZE % GASNETC_ALIGN_SIZE == 0);
        mysizereq=mysize=segsize+GASNET_PAGESIZE+GASNETC_RESERVED_SPACE;
        sharedsize=0;
        ret = axiom_allocator_init(&mysize, &sharedsize, AXAL_SW);
        logmsg(LOG_INFO,"gasnet_attach(): request by conduit %lu (%lu MiB)",mysizereq,mysizereq/1024/1024);
        logmsg(LOG_INFO,"gasnet_attach(): initialized %lu (%lu MiB) requested %lu (%lu MiB)",mysize,mysize/1024/1024,mysizereq,mysizereq/1024/1024);
        mysize=mysize-GASNET_PAGESIZE;
        if (ret) 
            gasneti_fatalerror("axiom_allocator_init return %d",ret);
        rdmabase = segbase = axiom_private_malloc(mysize);
        if (!rdmabase) 
            gasneti_fatalerror("axiom_private_malloc error addr:%p size:%ld",rdmabase,mysize);
        logmsg(LOG_INFO,"gasnet_attach(): malloc %lu (%lu MiB) at %p",mysize,mysize/1024/1024,rdmabase);
        gasnetc_rdma_allocation_done=1;
        delta = 0;
        if (((unsigned long) segbase) % GASNET_PAGESIZE != 0) {
            uint8_t *newbase = (uint8_t*) ((((unsigned long) segbase) | (GASNET_PAGESIZE - 1)) + 1);
            delta = newbase - (uint8_t*) segbase;
            segbase = newbase;
        }
        segsize = (mysize - delta)&(~(uint64_t) (GASNET_PAGESIZE - 1));

        logmsg(LOG_INFO,"gasnet_attach(): aligned by %u now at %p for %lu (%lu MiB)",delta,segbase,segsize,segsize/1024/1024);
        logmsg(LOG_INFO,"gasnet_attach(): user space at %p:%p for %lu (%lu MiB)",
                ((uint8_t*) segbase + GASNETC_RESERVED_SPACE),
                ((uint8_t*) segbase + GASNETC_RESERVED_SPACE)+segsize - GASNETC_RESERVED_SPACE-1,
                segsize - GASNETC_RESERVED_SPACE,
                (segsize - GASNETC_RESERVED_SPACE)/1024/1024);

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

    logmsg(LOG_DEBUG,"gasnetc_attach() primary attach complete");
    
    GASNETI_TRACE_PRINTF(C, ("gasnetc_attach(): primary attach complete"));

    gasneti_assert((uint8_t*) gasneti_seginfo[gasneti_mynode].addr == (uint8_t*) segbase + GASNETC_RESERVED_SPACE &&
            gasneti_seginfo[gasneti_mynode].size == segsize - GASNETC_RESERVED_SPACE);

    gasneti_auxseg_attach(); /* provide auxseg */

    gasnete_init(); /* init the extended API */

    gasneti_nodemapFini();

    /* ensure extended API is initialized across nodes */
    gasnetc_bootstrapBarrier();

    logmsg(LOG_DEBUG,"gasnetc_attach() end (without errror)");

    return GASNET_OK;
}

/*
 *
 *
 * conduit exit
 *
 *
 *
 */

#if HAVE_ON_EXIT

/**
 * Conduit exit function called on_exit.
 *
 * @param exitcode exit code.
 * @param arg ???.
 */
static void gasnetc_on_exit(int exitcode, void *arg) {
    gasnetc_exit(exitcode);
}
#else

/**
 * Conduit exit function called on_exit.
 * @param exitcode Exit code.
 */
static void gasnetc_atexit(void) {

    gasnetc_exit(0);
}
#endif

//static int received_exit_message = 0;

/**
 * Conduit exit function.
 * This funtion never return.
 * @param exitcode The exit code
 */
extern void gasnetc_exit(int exitcode) {
    logmsg(LOG_INFO,"gasnetc_exit(): called");
    /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
    gasneti_reghandler(SIGQUIT, SIG_IGN);

    { /* ensure only one thread ever continues past this point */

        static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
        gasneti_mutex_lock(&exit_lock);
    }

    logmsg(LOG_INFO,"gasnetc_exit(): shutdown sequence in progress");
    GASNETI_TRACE_PRINTF(C, ("gasnet_exit(%i)\n", exitcode));

    gasneti_flush_streams();
    logmsg(LOG_DEBUG,"gasnetc_exit(): stream flushed");
    gasneti_trace_finish();
    logmsg(LOG_DEBUG,"gasnetc_exit(): trace finished");
    gasneti_sched_yield();
    logmsg(LOG_DEBUG,"gasnetc_exit(): sched yeld :-)");

    /* (###) add code here to terminate the job across _all_ nodes 
             with gasneti_killmyprocess(exitcode) (not regular exit()), preferably
             after raising a SIGQUIT to inform the client of the exit
     */
    // MG TODO:
    if (gasnetc_rdma_allocation_done)
        axiom_private_free(gasneti_seginfo[gasneti_mynode].rdma);
    logmsg(LOG_DEBUG,"gasnetc_exit(): free rdma memory done");
        //axiom_rdma_munmap(axiom_dev);
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

/*
 *
 *
 * Async request manager
 *
 *
 *
 */

#ifdef _ASYNC_RDMA_MODE

/** Max pending async reqeust. */
#define ASYNC_MAX_PENDING_REQ 64
/** Masq for pending request. (So MAX_PENDING must be a power of two). */
#define ASYNC_MASQ 0x3f

// rationale: why more arrays (and not an array of structur)? because the axiom api will change to check for tokens with one ioctl (passing an array of rdma tokens)

/** Buffer for storing pending raw message. */
static gasnetc_axiom_am_msg_t async_buf[ASYNC_MAX_PENDING_REQ];
/** Array to store axiom rmda tokes. */
static axiom_token_t async_tok[ASYNC_MAX_PENDING_REQ];

// if defined use a copy of async_tok to store outqued token
// (only for informational purpose)
#define USE_ASYNC_TOK2

#ifdef USE_ASYNC_TOK2
static axiom_token_t async_tok2[ASYNC_MAX_PENDING_REQ];
#endif

/** Array of information for a pending async request. */
static struct {
    /** FREE or USED or a number that indicate a group. */
    int state;
    /** Destination node. */
    gasnet_node_t dest;
    /** Payload size. */
    axiom_long_payload_size_t size;
} async_info[ASYNC_MAX_PENDING_REQ];

/** State FREE. */
#define ASYNC_FREE 0
/** State USED. */
#define ASYNC_USED -1
/** Check for free state. */
#define ASYNC_IS_FREE(idx) (async_info[idx].state==ASYNC_FREE)
/** Check for used state. */
#define ASYNC_IS_USED(idx)  (async_info[idx].state==ASYNC_USED)
/** Check for elaboration state. the buffer is own by a group. */
#define ASYNC_IS_ELAB(idx) (async_info[idx].state!=ASYNC_USED&&async_info[idx].state!=ASYNC_FREE)

/** Number of buffe used. */
static int async_used=0;
/** Next buffer to use. circular buffer. */
static int async_next=0;
/** Mutex for PAR mode. */
static MUTEX_t async_mutex;

/**
 * Initialize data for async requestes.
 */
static void async_init() {
    int idx;
    for (idx=0;idx<ASYNC_MAX_PENDING_REQ;idx++) {
        async_info[idx].state=ASYNC_FREE;
        //async_tok[idx].raw=INVALID_TOKEN;
        AXIOM_TOKEN_INVALIDATE(async_tok+idx);
    }
    INIT_MUTEX(async_mutex);
}

/**
 * Allocate an async buffer.
 * @return The index of the buffer is returned or -1 if not available.
 */
static int async_allocate_buffer() {
    int idx=-1;
    LOCK(async_mutex);
    if (async_used!=ASYNC_MAX_PENDING_REQ) {
        for (;;) {
            if (ASYNC_IS_FREE(async_next)) {
                idx=async_next;
                async_next=(async_next+1)&ASYNC_MASQ;
                //
                async_info[idx].state=ASYNC_USED;
                async_used++;
                break;
            }
            async_next=(async_next+1)&ASYNC_MASQ;
        }
    }
    UNLOCK(async_mutex);
    if (logmsg_is_enabled(LOG_DEBUG)) {
        if (idx==-1) {
            logmsg(LOG_DEBUG,"async buffers: full!");
        } else {
            logmsg(LOG_DEBUG,"async buffers: allocated idx=%d",idx);
        }
    }
    return idx;
}

/**
 * Free buffers.
 * TO BE USED after async_check_buffer() (so the buffer are already free but not usables).
 * @param num_free Number of buffer to free.
 */
static void async_free_buffer(int num_free) {
    LOCK(async_mutex);
    async_used-=num_free;
    UNLOCK(async_mutex);
}

/** Maxium number of pendig request to elaborate per gasnet poll request. */
/* to check: if ASYNC_MAX_NUM_PER_CHECK < ASYNC_MAX_PENDING_REQ starvation?*/
//#define ASYNC_MAX_NUM_PER_CHECK ASYNC_MAX_PENDING_REQ

/**
 * Check if async RDMA request are done.
 * See if async_info[].state==*id to find index of request terminated.
 * @param id An id to find which request are done.
 * @param num Number of request done.
 */
static void async_check_buffers(int *id, int *num) {
    static volatile int first_message=1;
    axiom_err_t ret;
    int idx;
    *num=0;
    *id=0;

    if (async_used!=0) {

        LOCK(async_mutex);
        if (async_used!=0) {
            if (logmsg_is_enabled(LOG_DEBUG)&&__sync_fetch_and_or(&first_message,1)==0) {
                logmsg(LOG_DEBUG,"async buffers: pending request into queue");
            }
            ret=axiom_rdma_check(axiom_dev,async_tok,ASYNC_MAX_PENDING_REQ);
            if (AXIOM_RET_IS_OK(ret)) {
                if (ret>0) {
                    *id=pthread_self();
                    gasneti_assert(*id!=0);
                    for (idx=0;idx<ASYNC_MAX_PENDING_REQ;idx++) {
                        if (AXIOM_TOKEN_IS_ACKED(async_tok+idx)) {
                            if (ASYNC_IS_USED(idx)) {
                                logmsg(LOG_DEBUG,"async buffers: freeing idx=%d",idx);
                                (*num)=(*num)+1;
                                async_info[idx].state=*id;
#ifdef USE_ASYNC_TOK2
                                async_tok2[idx].raw=async_tok[idx].raw;
#endif
                                AXIOM_TOKEN_INVALIDATE(async_tok+idx);
                            } else {
                                // when a token is acked we invalidate and set info to ELAB so it can be acked again and NOT used (it must be in elab)
                                // i.e. the ASYNC_IS_USED is a paranoia check!!!
                                gasneti_fatalerror("INTERNAL axiom conduit ERROR: rdma tok is ack but not used!!!");
                            }
                            if (--ret<=0) break;
                        }
                    }
                }
            } else {
                if (ret!=AXIOM_RET_NOTAVAIL)
                    logmsg(LOG_WARN,"async buffers: axiom_rdma_check() error (ret=%d)",ret);
            }
            /*
             *         for (idx=0;idx<ASYNC_MAX_PENDING_REQ;idx++) {
             *             if (ASYNC_IS_USED(idx)) {
             *                 ret=axiom_rdma_check(axiom_dev,async_tok+idx);
             *                 if (ret==AXIOM_RET_NOTAVAIL) continue;
             *                 if (!AXIOM_RET_IS_OK(ret)) {
             *                     continue;
             *                 }
             *                 if (*id==0) {
             *                     *id=pthread_self();
             *                     gasneti_assert(*id!=0);
             *                 }
             *                 logmsg(LOG_DEBUG,"async buffers: freeing idx=%d",idx);
             *                 (*num)=(*num)+1;
             *                 async_info[idx].state=*id;
             * #ifdef USE_ASYNC_TOK2
             *                 async_tok2[idx].raw=async_tok[idx].raw;
             * #endif
             *                 async_tok[idx].raw=INVALID_TOKEN;
             *                 if (*num>=ASYNC_MAX_NUM_PER_CHECK) break;
               *                                    }
               *              }
               *         */
        } else {
            if (logmsg_is_enabled(LOG_DEBUG)&&__sync_fetch_and_and(&first_message,0)==1) {
                logmsg(LOG_DEBUG,"async buffers: no request into queue");
            }
        }
        UNLOCK(async_mutex);

    }
}

#endif

/*
 *
 *
 * Active message functions
 *
 *
 *
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

/**
 * The source node of an active message.
 * @param token The token received.
 * @param srcindex The source node (output information).
 * @return GASNET_OK if success.
 */
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

/** Maximum remote messages elabotated for gasnet poll request. */
// do not use ONE !!!!!!!!!!! errors on "testgasnet" (bulk monothread send))
// with the new gasnet_pollwhile() you can set this to one if using a BLOCKUNTIL() thread
// (if this is too low and we does not have a polling thread this can cause an infinite loop on a sending primitive if the driver can't free hw buffers)
#ifdef _NOT_BLOCK_ON_LOOP
#define MAX_MSG_PER_POLL 7
#else
#define MAX_MSG_PER_POLL 5
#endif

//int _counter=0;

// for ASYNC_RDMA_REQUEST
#define MAX_MSG_RETRANSMIT 16

/**
 * Conduit internal poll request.
 * @return GASNET_OK if success.
 */
extern int gasnetc_internal_AMPoll(void) {
#ifdef _ASYNC_RDMA_MODE
    static volatile int first_message=1;
#endif
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
    int something_done=0;
    gasnet_handler_t handler_id;
    gasneti_handler_fn_t handler_fn;
    int numargs, isReq;
    gasnet_handlerarg_t *args;
    void *data;
    int nbytes;
    // so payload is always GASNETI_MEDBUF_ALIGNMENT aligned
    payload=(gasnetc_axiom_msg_t*)((((uintptr_t)buffer)&~(GASNETI_MEDBUF_ALIGNMENT-1))+GASNETI_MEDBUF_ALIGNMENT);

    logmsg(LOG_DEBUG,"gasnetc_AMPoll() enter");

    GASNETI_CHECKATTACH();
#if GASNET_PSHM
    gasneti_AMPSHMPoll(0);
#endif

#ifdef _ASYNC_RDMA_MODE
    if (async_used!=0) {
        int id,num;
        if (logmsg_is_enabled(LOG_DEBUG)) {
            if (__sync_fetch_and_or(&first_message,1)==0) {
                logmsg(LOG_DEBUG,"AMPoll: async RDMA request pending");
            }
        }
        logmsg(LOG_TRACE,"AMPoll: checking async RDMA request completition");
        async_check_buffers(&id,&num);
        if (num!=0) {
            something_done=1;
            int counter=num;
            int idx;
            logmsg(LOG_DEBUG,"AMPoll: %d async RDMA request completed",counter);
            for (idx=0;idx<ASYNC_MAX_PENDING_REQ;idx++) {
                if (async_info[idx].state==id) {
#ifdef USE_ASYNC_TOK2
                    logmsg(LOG_DEBUG,"AMPoll: outqueue RDMA token 0x%016lx",async_tok2[idx].raw);
#else
                    logmsg(LOG_DEBUG,"AMPoll: outqueue RDMA token");
#endif
                    ret= _send_raw(axiom_dev, async_info[idx].dest, AXIOM_BIND_PORT, async_info[idx].size, async_buf+idx);
                    if (!AXIOM_RET_IS_OK(ret)) {
                        // what to do now???
                        logmsg(LOG_WARN,"AMPoll: error sending raw message for pending RDMA request...");
                        if (ret==AXIOM_RET_NOTAVAIL) {
                            int loopc=MAX_MSG_RETRANSMIT;
                            while (loopc-->0) {
                                ret= _send_raw(axiom_dev, async_info[idx].dest, AXIOM_BIND_PORT, async_info[idx].size, async_buf+idx);
                                if (ret!=AXIOM_RET_NOTAVAIL) break;
                                gasneti_sched_yield();
                            }
                        }
                    }
                    if (!AXIOM_RET_IS_OK(ret))
                        gasneti_fatalerror("AMPoll: pending RDMA request sending to phy:%d error! (ret=%d)",node_log2phy(async_info[idx].dest),ret);
                    async_info[idx].state=ASYNC_FREE;
                    counter--;
                    if (counter==0) break;
                }
            }
            async_free_buffer(num);
        }
    } else {
        if (logmsg_is_enabled(LOG_DEBUG)) {
            if (__sync_fetch_and_and(&first_message,0)!=0) {
                logmsg(LOG_DEBUG,"AMPoll: NO async RDMA request in queue");
            }
        }
    }
#endif

    for (maxcounter = MAX_MSG_PER_POLL; maxcounter > 0; maxcounter--) {

#ifdef _BLOCKING_MODE
        LOCK(poll_mutex);
        res = _recv_avail(axiom_dev);
        if (!res) {
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
        UNLOCK(poll_mutex);
#else
        if (ret==AXIOM_RET_NOTAVAIL) break;
#endif

        if_pt(AXIOM_RET_IS_OK(ret)) {
            something_done=1;

            info.isReq = isReq = (payload->gen.command == GASNETC_AM_REQ_MESSAGE);

            switch (payload->gen.command) {

                case GASNETC_RDMA_MESSAGE:
                    gasneti_fatalerror("GASNETC_RDMA_MESSAGE not used anymore!");
                    break;

                case GASNETC_AM_REQ_MESSAGE:
                case GASNETC_AM_REPLY_MESSAGE:
/*
#ifndef _BLOCKING_MODE
                    if (payload->gen.command==GASNETC_AM_REQ_MESSAGE) {
                        info.rdma_token = payload->am.head.rdma_token;
                    } else {
                        // TODO
                        // wait for RDMA ack using rdma_token as key!
                    }
#endif
*/
                    token=&info;
                    category = payload->am.head.category;
                    handler_id = payload->am.head.handler_id;
                    handler_fn = gasnetc_get_handler(handler_id);
                    numargs = payload->am.head.numargs;
                    args = payload->am.args;
                    data = NULL;
                    nbytes = 0;

                    if (logmsg_is_enabled(LOG_TRACE)) {
                        char mybuf[5*128+1];
                        char num[6];
                        int i;
                        mybuf[0]='\0';
                        for (i=0;i<size&&i<128;i++) {
                            sprintf(num,"0x%02x ",(unsigned)*(((uint8_t*)payload)+i));
                            strcat(mybuf,num);
                        }
                        logmsg(LOG_TRACE,"packet dump: %s",mybuf);
                    }

                    gasneti_assert((category == gasnetc_Short) || (category == gasnetc_Medium) || (category == gasnetc_Long));

                    switch (category) {
                        case gasnetc_Short:
                        {
                            logmsg(LOG_INFO,"AMPoll %s category=Short handler=%d from %d(phy:%d) numargs=%d",
                                    payload->gen.command==GASNETC_AM_REQ_MESSAGE?"AM_REQ_MESSAGE":"AM_REPLY_MESSAGE",
                                    handler_id,
                                    info.node,
                                    node_log2phy(info.node),
                                    numargs
                                    );
                            GASNETC_ENTERING_HANDLER_HOOK(category, isReq, handler_id, token, data, nbytes, numargs, args);
                            GASNETI_RUN_HANDLER_SHORT(isReq, handler_id, handler_fn, token, args, numargs);
                        }
                            break;
                        case gasnetc_Medium:
                        {
                            void * data = payload->buffer+compute_aligned_payload_size(payload->am.head.numargs);
                            nbytes = payload->am.head.size;
                            logmsg(LOG_INFO,"AMPoll %s category=Medium handler=%d from %d(phy:%d) numargs=%d size=%u",
                                    payload->gen.command==GASNETC_AM_REQ_MESSAGE?"AM_REQ_MESSAGE":"AM_REPLY_MESSAGE",
                                    handler_id,
                                    info.node,
                                    node_log2phy(info.node),
                                    numargs,
                                    nbytes
                                    );
                            GASNETC_ENTERING_HANDLER_HOOK(category, isReq, handler_id, token, data, nbytes, numargs, args);
                            GASNETI_RUN_HANDLER_MEDIUM(isReq, handler_id, handler_fn, token, args, numargs, data, nbytes);
                        }
                            break;
                        case gasnetc_Long:
                        {
                            data = (uint8_t*) gasneti_seginfo[gasneti_mynode].rdma + payload->am.head.offset;
                            nbytes = payload->am.head.size;
                            logmsg(LOG_INFO,"AMPoll %s category=Long handler=%d from %d(phy:%d) numargs=%d size=%u to=%p",
                                    payload->gen.command==GASNETC_AM_REQ_MESSAGE?"AM_REQ_MESSAGE":"AM_REPLY_MESSAGE",
                                    handler_id,
                                    info.node,
                                    node_log2phy(info.node),
                                    numargs,
                                    nbytes,
                                    data
                                    );
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
                    gasneti_fatalerror("Unknown axiom packed received (command=%d ret=%d)!", payload->gen.command, ret);
                    break;

            }
#ifdef _BLOCK_ON_LOOP_CONDWAIT
            gasneti_mutex_lock(&gasnetc_mut);
            gasneti_cond_broadcast(&gasnetc_cond);
            gasneti_mutex_unlock(&gasnetc_mut);
#endif
#ifdef _BLOCK_ON_LOOP_EPOLL
            raise_check_event();
#endif
        } else {

            //
            // WARNING
            // (read error ignored!!!)
            //
            logmsg(LOG_WARN,"AXIOM read message error (err=%d)", ret);
            //gasneti_fatalerror("AXIOM read message error (err=%d)",ret);
        }
        
    }

    logmsg(LOG_DEBUG,"gasnetc_AMPoll() leave with return %s",something_done?"GASNET_OK":"GASNET_ERR_AGAIN");
    return something_done?GASNET_OK:GASNET_ERR_AGAIN;
}

/**
 * Short active message request.
 * @param dest Destination node.
 * @param handler Remote handler id.
 * @param numargs Number of arguments.
 * @param ... Arguments.
 * @return AXIOM_OK if success.
 */
extern int gasnetc_AMRequestShortM(gasnet_node_t dest, gasnet_handler_t handler, int numargs, ...) {
    gasnetc_axiom_am_msg_t payload;
    axiom_err_t ret;
    va_list argptr;
    int retval;
    int i;

    logmsg(LOG_INFO,"AMRequestShort dest=%d(phy:%d) handler=%d",dest,node_log2phy(dest),handler);
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
        //payload.head.rdma_token = 0;
        for (i = 0; i < numargs; i++) {
            payload.args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        ret = _send_raw(axiom_dev, dest, AXIOM_BIND_PORT, compute_payload_size(numargs), &payload);
        retval = AXIOM_RET_IS_OK(ret) ? GASNET_OK : -1;
        if (retval!=GASNET_OK) {
            logmsg(LOG_WARN,"AMRequestShort failed during axiom_send_raw() with ret=%d",ret);
        }
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

/**
 * Medium active message request.
 * @param dest Destination node.
 * @param handler Remote handler id.
 * @param Source address.
 * @param Size of source memory block.
 * @param numargs Number of arguments.
 * @param ... Arguments.
 * @return AXIOM_OK if success.
 */
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

    logmsg(LOG_INFO,"AMRequestMedium dest=%d(phy:%d) handler=%d src=%p sz=%lu buf[0]=0x%02x",dest,node_log2phy(dest),handler,source_addr,nbytes,nbytes>0?*(uint8_t*)source_addr:255);
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
        //payload.head.rdma_token = 0;
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
        if (retval!=GASNET_OK) {
            logmsg(LOG_WARN,"AMRequestMedium failed during axiom_send_long_iov() with ret=%d",ret);
        }
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

/** Long message type.*/
typedef enum {
  /** Active long message. */
  NORMAL_REQUEST=0, 
  /** Async active long message. */
  ASYNC_REQUEST=1,
  /** Long reply message.*/
  REPLAY=2
} type_enum;

/**
 * Called by AMRequest or AMReply long active messages.
 *
 * @param dest Destination node
 * @param handler Requested remote handler id
 * @param source_addr Source memory address
 * @param nbytes Source memory buffer size
 * @param dest_addr Destination memory address (on remote node)
 * @param numargs Number of arguments
 * @param argptr Variable list of uint32_t arguments
 * @param rdma_token rdma token received (or zero if no token present)
 * @param type zero=normal, one=async, two=reply
 * @return GASNET_OK if succesfull otherwise an error code
 */
//static int _requestOrReplyLong(gasnet_node_t dest, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, va_list argptr, uint32_t rdma_token, int type) {
static int _requestOrReplyLong(gasnet_node_t dest, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, va_list argptr, type_enum type) {
    int ret = AXIOM_RET_OK;
    int retval = GASNET_ERR_RDMA;
    gasnetc_axiom_am_msg_t payload_buffer;
    gasnetc_axiom_am_msg_t *payload=&payload_buffer;
    int out;

#ifdef _NOT_ASYNC_RDMA_MODE
    // no async request in blocking mode
    if (type==ASYNC_REQUEST) type=NORMAL_REQUEST;
#endif

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

    if (nbytes >= GASNETC_ALIGN_SIZE) {
        if (out) {
            void *buf = alloca_rdma_buf();
            uint8_t *srcp = source_addr, *dstp = dest_addr;
            int totras = (nbytes & (~(GASNETC_ALIGN_SIZE - 1)));
            int sz;
            axiom_token_t token;
            logmsg(LOG_DEBUG,"Sync/AsyncReq: out of RDMA space... switch so SYNC RDMA from internal buffers");
            if (type==ASYNC_REQUEST) type=NORMAL_REQUEST;
            for (;;) {
                sz = totras > GASNETC_BUFFER_SIZE ? GASNETC_BUFFER_SIZE : totras;
                memcpy(buf, srcp, sz);
//#ifdef _NOT_ASYNC_RDMA_MODE
//                ret = _rdma_write_sync(axiom_dev, dest, sz, buf, dstp);
//#else
                ret = _rdma_write_sync(axiom_dev, dest, sz, buf, dstp);
//                if (AXIOM_RET_IS_OK(ret))
//                    ret=axiom_rdma_wait(axiom_dev,&token);
//#endif
                if (!AXIOM_RET_IS_OK(ret)) {
                    logmsg(LOG_WARN,"Sync/AsyncReq: _rdma_write/wait error (ret=%d)",ret);
                    break;
                }
                totras -= sz;
                if (totras == 0) break;
                dstp += sz;
                srcp += sz;
            }
            free_rdma_buf(buf);
        } else {
#ifdef _NOT_ASYNC_RDMA_MODE
            ret = _rdma_write_sync(axiom_dev, dest, nbytes &(~(GASNETC_ALIGN_SIZE - 1)), source_addr, dest_addr);
#else
            axiom_token_t token;
            if (type!=ASYNC_REQUEST) {
                ret = _rdma_write_sync(axiom_dev, dest, nbytes &(~(GASNETC_ALIGN_SIZE - 1)), source_addr, dest_addr);
            } else {
                ret = _rdma_write(axiom_dev, dest, nbytes &(~(GASNETC_ALIGN_SIZE - 1)), source_addr, dest_addr, &token);
                if (AXIOM_RET_IS_OK(ret)) {
                    int idx=async_allocate_buffer(ret);
                    if (idx==-1) {
                        // no buffer available... switch to sync mode...
                        logmsg(LOG_DEBUG,"AsyncReq: switch to SYNC request (no buffer availables)... wait RDMA");
                        type=NORMAL_REQUEST;
                        ret=axiom_rdma_wait(axiom_dev,&token);
                    } else {
                        logmsg(LOG_DEBUG,"AsyncReq: enqueueing RDMA token 0x%016lx",token.raw);
                        async_tok[idx]=token;
                        async_info[idx].dest=dest;
                        async_info[idx].size=compute_payload_size(numargs);
                        payload=async_buf+idx;
                    }
                } else {
                    logmsg(LOG_WARN,"AsyncReq; _rdma_write() error (ret=%d)",ret);
                }
            }
#endif
        }
    }
    
    if (AXIOM_RET_IS_OK(ret)) {

        int i;
        
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
            memcpy(payload->head.data, ptr, rest);
        }

        payload->head.command = type==REPLAY?GASNETC_AM_REPLY_MESSAGE:GASNETC_AM_REQ_MESSAGE;
        payload->head.category = gasnetc_Long;
        payload->head.handler_id = handler;
        payload->head.numargs = numargs;
        payload->head.offset = (uintptr_t) dest_addr - (uintptr_t) gasneti_seginfo[dest].rdma;
        payload->head.size = nbytes;
/*
#ifdef _BLOCKING_MODE
        payload->head.rdma_token=0;
#else
        // if rdma_token is NOT zero
        //   we have been called by a AMReply that as received a rdma_token from remote
        //   so send back
        // else
        //   we have been called by a AMReply with no token or by a AMRequest
        //   so ret contain a new rdma token (if AMRequest Async are used) send the new token or zero (if we haven't one)
        payload->head.rdma_token=(rdma_token!=0?rdma_token:(ret==AXIOM_RET_OK?0:ret));
#endif
 */
        for (i = 0; i < numargs; i++) {
            payload->args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        if (type==ASYNC_REQUEST&&nbytes >= GASNETC_ALIGN_SIZE) {
            retval=GASNET_OK;
        } else {
            axiom_err_t ret2 = _send_raw(axiom_dev, dest, AXIOM_BIND_PORT, compute_payload_size(numargs), payload);
            if (logmsg_is_enabled(LOG_WARN)&&!AXIOM_RET_IS_OK(ret2)) {
                logmsg(LOG_WARN,"Error %d calling _send_raw()",ret);
            }
            retval = AXIOM_RET_IS_OK(ret2) ? GASNET_OK : GASNET_ERR_RAW_MSG;
        }
    } else {
        logmsg(LOG_WARN,"Error %d calling axiom_rdma_write()",ret);
    }

    return retval;
}

/**
 * Long active message request.
 *
 * @param dest Destination node
 * @param handler Requested remote handler id
 * @param source_addr Source memory address
 * @param nbytes Source memory buffer size
 * @param dest_addr Destination memory address (on remote node)
 * @param numargs Number of arguments
 * @param ... Arguments.
 * @return GASNET_OK if succesfull otherwise an error code
 */
extern int gasnetc_AMRequestLongM(gasnet_node_t dest, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, ...) {
    axiom_err_t ret;
    va_list argptr;
    int retval;

    logmsg(LOG_INFO,"AMRequestLong dest=%d(phy:%d) handler=%d src=%p dst=%p sz=%lu",dest,node_log2phy(dest),handler,source_addr,dest_addr,nbytes);
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
        retval = _requestOrReplyLong(dest, handler, source_addr, nbytes, dest_addr, numargs, argptr, NORMAL_REQUEST);
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

/**
 * Async long active message request.
 *
 * @param dest Destination node
 * @param handler Requested remote handler id
 * @param source_addr Source memory address
 * @param nbytes Source memory buffer size
 * @param dest_addr Destination memory address (on remote node)
 * @param numargs Number of arguments
 * @param ... Arguments.
 * @return GASNET_OK if succesfull otherwise an error code
 */
extern int gasnetc_AMRequestLongAsyncM(gasnet_node_t dest, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, ...) {
    int retval;
    va_list argptr;

    logmsg(LOG_INFO,"AMRequestLongAsync dest=%d(phy:%d) handler=%d src=%p dst=%p sz=%lu",dest,node_log2phy(dest),handler,source_addr,dest_addr,nbytes);
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
        retval = _requestOrReplyLong(dest, handler, source_addr, nbytes, dest_addr, numargs, argptr, ASYNC_REQUEST);
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

    logmsg(LOG_INFO,"AMReplyShort token=%p dest_node=%d(phy:%d) handler=%d",(void*)token,info->node,node_log2phy(info->node),handler);
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
/*
#ifdef _BLOCKING_MODE
        payload.head.rdma_token=0;
#else
        payload.head.rdma_token=info->rdma_token;
#endif
*/
        for (i = 0; i < numargs; i++) {
            payload.args[i] = va_arg(argptr, gasnet_handlerarg_t);
        }
        ret = _send_raw(axiom_dev, info->node, info->port, compute_payload_size(numargs), &payload);
        retval = AXIOM_RET_IS_OK(ret) ? GASNET_OK : GASNET_ERR_RAW_MSG;
        if (retval!=GASNET_OK) {
            logmsg(LOG_WARN,"AMReplyShort failed during axiom_send_raw() with ret=%d",ret);
        }
    }
    va_end(argptr);

    logmsg(LOG_DEBUG,"AMReplyShort leave");
    GASNETI_RETURN(retval);
}

/**
 * Active medium replay.
 *
 * @param token To identify the request.
 * @param handler Remote handler to call.
 * @param source_addr Source address.
 * @param nbytes Size of source memory block.
 * @param numargs Number of arguments.
 * @param ... Arguments.
 * @return GASNET_OK in success.
 */
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
    
    logmsg(LOG_INFO,"AMReplyMedium token=%p dest_node=%d(phy:%d) handler=%d src=%p sz=%lu buf[0]=0x%02x",(void*)token,info->node,node_log2phy(info->node),handler,source_addr,nbytes,nbytes>0?*(uint8_t*)source_addr:255);
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
/*
#ifdef _BLOCKING_MODE
        payload.head.rdma_token=0;
#else
        payload.head.rdma_token=info->rdma_token;
#endif
*/
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
        if (retval!=GASNET_OK) {
            logmsg(LOG_WARN,"AMReplyMedium failed during axiom_send_long_iov() with ret=%d",ret);
        }
    }
    va_end(argptr);
    GASNETI_RETURN(retval);
}

/**
 *  Active long reply.
 *
 * @param token To identify the request.
 * @param handler Remote handler to call.
 * @param source_addr Source address.
 * @param nbytes Size of source memory block.
 * @param dest_addr Destination address.
 * @param numargs Number of arguments.
 * @param ... Arguments.
 * @return GASNET_OK in success.
 */
extern int gasnetc_AMReplyLongM(gasnet_token_t token, gasnet_handler_t handler, void *source_addr, size_t nbytes, void *dest_addr, int numargs, ...) {
    gasnetc_axiom_am_info_t *info = (gasnetc_axiom_am_info_t*) token;
    int retval;
    va_list argptr;

    logmsg(LOG_INFO,"AMReplyLong token=%p dest_node=%d(phy:%d) handler=%d src=%p dst=%p sz=%lu",(void*)token,info->node,node_log2phy(info->node),handler,source_addr,dest_addr,nbytes);
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
        retval = _requestOrReplyLong(info->node, handler, source_addr, nbytes, dest_addr, numargs, argptr, REPLAY);
    }
    va_end(argptr);
    logmsg(LOG_DEBUG,"AMReplyLong leave");
    GASNETI_RETURN(retval);
}

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

/*
 *
 *
 * Handler-safe locks
 *
 *
 *
 */

#if !GASNETC_NULL_HSL

/**
 * HSL init.
 * Default implementation.
 * @param hsl The HSL.
 */
extern void gasnetc_hsl_init(gasnet_hsl_t *hsl) {

    GASNETI_CHECKATTACH();
    gasneti_mutex_init(&(hsl->lock));

#if GASNETC_USE_INTERRUPTS
    /* add code here to init conduit-specific HSL state */
#error interrupts not implemented
#endif
}

/**
 * HSL destroy.
 * Default implementation.
 * @param hsl The HSL.
 */
extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {

    GASNETI_CHECKATTACH();
    gasneti_mutex_destroy(&(hsl->lock));

#if GASNETC_USE_INTERRUPTS
    /* add code here to cleanup conduit-specific HSL state */
#error interrupts not implemented
#endif
}

/**
 * HSL lock.
 * Default implementation.
 * @param hsl The HSL.
 */
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

/**
 * HSL unkock.
 * Default implementaion.
 * @param hsl The HSL.
 */
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

/**
 * HSL try lock.
 * Default implementation.
 * @param hsl The HSL.
 * @return GASNET_OK on success
 */
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

/*
 *
 *
 * axiom conduit private handlers
 *
 *
 *
 */

/*
  Private Handlers:
  ================
  see mpi-conduit and extended-ref for examples on how to declare AM handlers here
  (for internal conduit use in bootstrapping, job management, etc.)
 */

/** Private handler table. */
static gasnet_handlerentry_t const gasnetc_handlers[] = {
#ifdef GASNETC_AUXSEG_HANDLERS
    GASNETC_AUXSEG_HANDLERS(),
#endif
    /* ptr-width independent handlers */

    /* ptr-width dependent handlers */ {
        0, NULL
    }
};

/**
 * Return the handler table.
 * @return The handler table.
 */
gasnet_handlerentry_t const *gasnetc_get_handlertable(void) {
    return gasnetc_handlers;
}

