/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_internal.h         $
 *     $Date: 2004/03/17 00:16:01 $
 * $Revision: 1.40 $
 * Description: GASNet vapi conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <stddef.h>	/* for offsetof() */

#include <gasnet.h>
#include <gasnet_internal.h>
#if GASNETC_USE_FIREHOSE
  #include <firehose.h>
#endif

#include <vapi.h>
#include <evapi.h>
#include <vapi_common.h>

extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

#define GASNETC_CACHE_LINE_SIZE (128)

/* check (even in optimized build) for VAPI errors */
#define GASNETC_VAPI_CHECK(vstat,msg) \
  if_pf ((vstat) != VAPI_OK) \
    { gasneti_fatalerror("Unexpected error %s %s",VAPI_strerror_sym(vstat),(msg)); }

/* check for exit in progress */
extern gasneti_atomic_t gasnetc_exit_running;
#define GASNETC_IS_EXITING() gasneti_atomic_read(&gasnetc_exit_running)

/* ------------------------------------------------------------------------------------ */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_             (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* System-category handlers.
 * These form a separate AM handler space and are available even before _attach()
 */
#define _hidx_gasnetc_SYS_ack             0
#define _hidx_gasnetc_SYS_exit_role_req   1
#define _hidx_gasnetc_SYS_exit_role_rep   2
#define _hidx_gasnetc_SYS_exit_req        3
#define _hidx_gasnetc_SYS_exit_rep        4

/* ------------------------------------------------------------------------------------ */

typedef struct {
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_shortmsg_t;

typedef struct {
  uint16_t		nBytes;
  uint16_t		_pad0;
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_medmsg_t;

typedef struct {
  uintptr_t		destLoc;
  uint32_t		nBytes;
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_longmsg_t;

typedef union {
  uint8_t		raw[GASNETC_BUFSZ];
  gasnetc_shortmsg_t	shortmsg;
  gasnetc_medmsg_t	medmsg;
  gasnetc_longmsg_t	longmsg;
} gasnetc_buffer_t;

typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2,
  gasnetc_System=3
} gasnetc_category_t;

/* The a Medium AM's payload starts as soon after the args as possible while still
   providing the required 8-byte alignment.
*/
#define GASNETC_MSG_MED_OFFSET(nargs)	\
	(offsetof(gasnetc_medmsg_t,args) + 4 * (nargs + ((nargs & 0x1) ^ ((GASNETC_MEDIUM_HDRSZ>>2) & 0x1))))
#define GASNETC_MSG_MED_DATA(msg, nargs) \
	((void *)((uintptr_t)(msg) + GASNETC_MSG_MED_OFFSET(nargs)))

/* Only needed for non-RDMA ReplyLong, used when remote segment not pinned.
   This is a much simpler expression than the medium, since we don't 8-byte align.
*/
#define GASNETC_MSG_LONG_OFFSET(nargs)	\
	(offsetof(gasnetc_longmsg_t,args) + 4 * nargs)
#define GASNETC_MSG_LONG_DATA(msg, nargs) \
	((void *)((uintptr_t)(msg) + GASNETC_MSG_LONG_OFFSET(nargs)))

/* ------------------------------------------------------------------------------------ */

typedef void (*gasnetc_HandlerShort) (gasnet_token_t token, ...);
typedef void (*gasnetc_HandlerMedium)(gasnet_token_t token, void *buf, size_t nbytes, ...);
typedef void (*gasnetc_HandlerLong)  (gasnet_token_t token, void *buf, size_t nbytes, ...);

#define RUN_HANDLER_SHORT(phandlerfn, token, args, numargs) do {                      \
  gasneti_assert(phandlerfn);                                                         \
  switch (numargs) {                                                                  \
    case 0:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token); break;        \
    case 1:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0]); break;         \
    case 2:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1]); break;\
    case 3:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2]); break; \
    case 4:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3]); break; \
    case 5:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4]); break; \
    case 6:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
    case 7:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
    case 8:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
    case 9:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
    case 10: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
    case 11: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
    case 12: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
    case 13: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
    case 14: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
    case 15: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
    case 16: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
    default: abort();                                                                 \
    }                                                                                 \
  } while (0)

#define _RUN_HANDLER_MEDLONG(phandlerfn, token, args, numargs, pData, datalen) do {   \
  gasneti_assert(phandlerfn);                                                         \
  switch (numargs) {                                                        \
    case 0:  (*phandlerfn)(token, pData, datalen); break;                    \
    case 1:  (*phandlerfn)(token, pData, datalen, args[0]); break;           \
    case 2:  (*phandlerfn)(token, pData, datalen, args[0], args[1]); break;  \
    case 3:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2]); break; \
    case 4:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3]); break; \
    case 5:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4]); break; \
    case 6:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
    case 7:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
    case 8:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
    case 9:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
    case 10: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
    case 11: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
    case 12: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
    case 13: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
    case 14: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
    case 15: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
    case 16: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
    default: abort();                                                                 \
    }                                                                                 \
  } while (0)

#define RUN_HANDLER_MEDIUM(phandlerfn, token, args, numargs, pData, datalen) do {      \
    gasneti_assert(((uintptr_t)pData) % 8 == 0);  /* we guarantee double-word alignment for data payload of medium xfers */ \
    _RUN_HANDLER_MEDLONG((gasnetc_HandlerMedium)phandlerfn, (gasnet_token_t)token, args, numargs, (void *)pData, (size_t)datalen); \
  } while(0)

#define RUN_HANDLER_LONG(phandlerfn, token, args, numargs, pData, datalen)             \
  _RUN_HANDLER_MEDLONG((gasnetc_HandlerLong)phandlerfn, (gasnet_token_t)token, args, numargs, (void *)pData, (size_t)datalen)

/* ------------------------------------------------------------------------------------ */

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
extern gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
typedef void (*gasnetc_sys_handler_fn_t)(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs);
extern const gasnetc_sys_handler_fn_t gasnetc_sys_handler[GASNETC_MAX_NUMHANDLERS];

#define RUN_HANDLER_SYSTEM(phandlerfn, token, args, numargs) \
    if (phandlerfn != NULL) (*phandlerfn)(token, args, numargs)

#if GASNET_TRACE
  #define _GASNETC_TRACE_SYSTEM(name,dest,handler,numargs) do {                        \
    _GASNETI_TRACE_GATHERARGS(numargs);                                                \
    _GASNETI_STAT_EVENT(C,name);                                                       \
    GASNETI_TRACE_PRINTF(C,(#name": dest=%i handler=%i args:%s",dest,handler,argstr)); \
  } while(0)
  #define GASNETC_TRACE_SYSTEM_REQUEST(dest,handler,numargs) \
          _GASNETC_TRACE_SYSTEM(SYSTEM_REQUEST,dest,handler,numargs)
  #define GASNETC_TRACE_SYSTEM_REPLY(dest,handler,numargs) \
          _GASNETC_TRACE_SYSTEM(SYSTEM_REPLY,dest,handler,numargs)

  #define _GASNETC_TRACE_SYSTEM_HANDLER(name, handlerid, token, numargs, arghandle) do { \
    gasnet_node_t src;                                                                    \
    _GASNETI_TRACE_GATHERHANDLERARGS(numargs, arghandle);                                 \
    _GASNETI_STAT_EVENT(C,name);                                                          \
    if (gasnet_AMGetMsgSource(token,&src) != GASNET_OK)                                   \
	gasneti_fatalerror("gasnet_AMGetMsgSource() failed");                               \
    GASNETI_TRACE_PRINTF(C,(#name": src=%i handler=%i args:%s",                           \
      (int)src,(int)(handlerid),argstr));                                                 \
    GASNETI_TRACE_PRINTF(C,(#name": token: %s",                                           \
                      gasneti_formatdata(&token, sizeof(token))));                        \
    } while(0)
  #define GASNETC_TRACE_SYSTEM_REQHANDLER(handlerid, token, numargs, arghandle) \
         _GASNETC_TRACE_SYSTEM_HANDLER(SYSTEM_REQHANDLER, handlerid, token, numargs, arghandle)
  #define GASNETC_TRACE_SYSTEM_REPHANDLER(handlerid, token, numargs, arghandle) \
         _GASNETC_TRACE_SYSTEM_HANDLER(SYSTEM_REPHANDLER, handlerid, token, numargs, arghandle)
#else
  #define GASNETC_TRACE_SYSTEM_REQUEST(dest,handler,numargs)
  #define GASNETC_TRACE_SYSTEM_REPLY(dest,handler,numargs)
  #define GASNETC_TRACE_SYSTEM_REQHANDLER(handlerid, token, numargs, arghandle) 
  #define GASNETC_TRACE_SYSTEM_REPHANDLER(handlerid, token, numargs, arghandle) 
#endif

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_TRACE_WAIT_BEGIN() \
    gasneti_stattime_t _waitstart = GASNETI_STATTIME_NOW_IFENABLED(C)
#else 
  #define GASNETC_TRACE_WAIT_BEGIN() \
    static char _dummy = (char)sizeof(_dummy)
#endif

#define GASNETC_TRACE_WAIT_END(name) \
  GASNETI_TRACE_EVENT_TIME(C,name,GASNETI_STATTIME_NOW() - _waitstart)

#define GASNETC_STAT_EVENT(name) \
  _GASNETI_STAT_EVENT(C,name)
#define GASNETC_STAT_EVENT_VAL(name,val) \
  _GASNETI_STAT_EVENT_VAL(C,name,val)

/* ------------------------------------------------------------------------------------ */

/* Scatter-gather segments.
 * Only 1 makes sense right now for normal use.
 */
#define GASNETC_SND_SG	1		/* maximum number of segments to gather on send */
#define GASNETC_RCV_SG	1		/* maximum number of segments to scatter on rcv */

/* Define non-zero to enable a progress thread for receiving AMs . */
#define GASNETC_RCV_THREAD		1

/* Define non-zero to enable polling for receiving AMs . */
#define GASNETC_RCV_POLL		1

#if GASNETC_VAPI_ENABLE_INLINE_PUTS
  /* AM req/rep <= this size will be done w/ VAPI-level copy, 0 disables */
  #define GASNETC_AM_INLINE_LIMIT	72
#else
  #define GASNETC_AM_INLINE_LIMIT	0
#endif

#if GASNETC_VAPI_ENABLE_INLINE_PUTS
  /* puts <= this size will be done w/ VAPI-level copy, 0 disables */
  #define GASNETC_PUT_INLINE_LIMIT	72
#else
  #define GASNETC_PUT_INLINE_LIMIT	0
#endif

/* puts <= this size will be done w/ local copies iff sender will wait for local completion */
#define GASNETC_PUT_COPY_LIMIT		4096

#define GASNETC_SND_REAP_LIMIT	32
#define GASNETC_RCV_REAP_LIMIT	16

/* ------------------------------------------------------------------------------------ */

/* Measures of concurency
 *
 * GASNETC_ANY_PAR	Non-zero if multiple threads can be executing in GASNet.
 * 			This is inclusive of the AM receive thread.
 * GASNETC_CLI_PAR	Non-zero if multiple _client_ threads can be executing in GASNet.
 * 			This excludes the AM receive thread.
 */

#if GASNET_PAR
  #define GASNETC_CLI_PAR	1
#else
  #define GASNETC_CLI_PAR	0
#endif

#define GASNETC_ANY_PAR		(GASNETC_CLI_PAR || GASNETC_RCV_THREAD)

/* ------------------------------------------------------------------------------------ */

/* gasneti_atomic_swap(p, oldval, newval)
 * Atomic equivalent of:
 *   If (*p == oldval) {
 *      *p = newval;
 *      return NONZERO;
 *   } else {
 *      return 0;
 *   }
 */
#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  GASNET_INLINE_MODIFIER(gasneti_atomic_swap)
  int gasneti_atomic_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
    int retval;

    #if GASNETC_ANY_PAR
      gasnet_hsl_lock(&gasneti_atomicop_lock);
    #endif
    retval = (p->ctr == oldval);
    if_pt (retval) {
      p->ctr = newval;
    }
    #if GASNETC_ANY_PAR
      gasnet_hsl_unlock(&gasneti_atomicop_lock);
    #endif

    return retval;
  }
  #define GASNETI_HAVE_ATOMIC_SWAP 1
#elif defined(LINUX)
  #ifdef __i386__
    GASNET_INLINE_MODIFIER(gasneti_atomic_swap)
    int gasneti_atomic_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
      register unsigned char retval;
      register uint32_t readval;

      __asm__ __volatile__ (GASNETI_LOCK "cmpxchgl %3, %1; sete %0"
				: "=q" (retval), "=m" (p->counter), "=a" (readval)
				: "r" (newval), "m" (p->counter), "a" (oldval)
				: "memory");
      return retval;
    }
    #define GASNETI_HAVE_ATOMIC_SWAP 1
  #endif
#elif (defined (__ppc__) || defined(_POWERPC)) 
  #if defined(__xlC__)
    /* See GNUC version for some explanation of the assembly */
    static int gasneti_atomic_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval);
    #pragma mc_func gasneti_atomic_swap { \
	/* Precondition: r3=p, r4=oldval, r5=newval */ \
	"7c001828" /*    lwarx   r0,0,r3       */ \
	"7c002000" /*    cmpw    r0,r4         */ \
	"4082000c" /*    bne-    1f            */ \
	"7ca0192d" /*    stwcx.  r5,0,r3       */ \
	"4c00012c" /*    isync                 */ \
	"7c000026" /* 1: mfcr    r0            */ \
	"54031ffe" /*    rlwinm  r3,r0,3,31,31 */ \
	/* Postcondition: result in r3 */ \
    }
    #pragma reg_killed_by gasneti_atomic_swap gr0, gr3
    #define GASNETI_HAVE_ATOMIC_SWAP 1
  #elif defined(__GNUC__)
    GASNET_INLINE_MODIFIER(gasneti_atomic_swap)
    int gasneti_atomic_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
      register uint32_t result;
      register uint32_t temp;

      __asm__ __volatile__ ( 
	"lwarx    %1,0,%2 \n\t" 	/* load to temp */
	"cmpw     cr0,%1,%3 \n\t"	/* compare temp to oldval */
	"bne-     1f \n\t"		/* branch on mismatch */
	"stwcx.   %4,0,%2 \n\t"	 	/* store newval */
	"isync \n" 
	"1:\t"
	/* convert condition code to int w/o any additional branch: */
	"mfcr     %1 \n\t"		/* move CR to temp */
	"rlwinm   %0,%1,3,31,31"	/* extract the CR0[EQ] bit from temp */
	: "=&r"(result), "=&r"(temp)
	: "r" (p), "r"(oldval), "r"(newval)
	: "cr0", "memory"); 

      return result; 
    } 
    #define GASNETI_HAVE_ATOMIC_SWAP 1
  #endif
#endif
#ifndef GASNETI_HAVE_ATOMIC_SWAP
  #define GASNETI_HAVE_ATOMIC_SWAP 0
#endif

#if !GASNETI_HAVE_ATOMIC_SWAP
  #warning "It would be a good idea to add gasneti_atomic_swap for your arch/OS/compiler"
#endif 

/* ------------------------------------------------------------------------------------ */

/* Lock ops that depend on the level of concurrency */
#define gasnetc_mutex_t                      gasneti_mutex_t
#define GASNETC_MUTEX_INITIALIZER            GASNETI_MUTEX_INITIALIZER
#define gasnetc_mutex_init                   gasneti_mutex_init
#define gasnetc_mutex_destroy                gasneti_mutex_destroy
#define gasnetc_mutex_lock(X,C)              if (C) { gasneti_mutex_lock(X); }
#define gasnetc_mutex_unlock(X,C)            if (C) { gasneti_mutex_unlock(X); }
#define gasnetc_mutex_assertlocked(X,C)      if (C) { gasneti_mutex_assertlocked(X); }
#define gasnetc_mutex_assertunlocked(X,C)    if (C) { gasneti_mutex_assertlocked(X); }

/* ------------------------------------------------------------------------------------ */

/*
 * gasnetc_sema_t
 *
 * This is a simple busy-waiting semaphore used, for instance, to control access to
 * some resource of known multiplicity.
 */
typedef struct {
  #if !GASNETI_HAVE_ATOMIC_SWAP
    gasnetc_mutex_t	lock;
  #endif
  gasneti_atomic_t	count;
} gasnetc_sema_t;

#if GASNETI_HAVE_ATOMIC_SWAP
  #define GASNETC_SEMA_INITIALIZER(N) {gasneti_atomic_init(N)}
#else
  #define GASNETC_SEMA_INITIALIZER(N) {GASNETC_MUTEX_INITIALIZER, gasneti_atomic_init(N)}
#endif

/* gasnetc_sema_init */
GASNET_INLINE_MODIFIER(gasnetc_sema_init)
void gasnetc_sema_init(gasnetc_sema_t *s, int n) {
  #if !GASNETI_HAVE_ATOMIC_SWAP
    gasnetc_mutex_init(&(s->lock));
  #endif
  gasneti_atomic_set(&(s->count), n);
}

/* gasnetc_sema_destroy */
GASNET_INLINE_MODIFIER(gasnetc_sema_destroy)
void gasnetc_sema_destroy(gasnetc_sema_t *s) {
  #if !GASNETI_HAVE_ATOMIC_SWAP
    gasnetc_mutex_destroy(&(s->lock));
  #endif
}

/* gasnetc_sema_up
 *
 * Atomically increments the value of the semaphore.
 * Since this just a busy-waiting semaphore, no waking operations are required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_up)
void gasnetc_sema_up(gasnetc_sema_t *s) {
  /* no locking needed here */
  gasneti_atomic_increment(&(s->count));
}

/* gasnetc_sema_trydown
 *
 * If the value of the semaphore is non-zero, decrements it and returns the old value.
 * If the value is zero, returns zero.
 *
 * If non-zero, the "concurrent" argument indicates that there are multiple threads
 * calling gasnetc_sema_trydown, and thus locking is required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_trydown)
int gasnetc_sema_trydown(gasnetc_sema_t *s, int concurrent) {
  int retval;

  #if GASNETI_HAVE_ATOMIC_SWAP
    uint32_t old = gasneti_atomic_read(&(s->count));
    retval = (old > 0) && gasneti_atomic_swap(&(s->count), old, old - 1);
  #else
    gasnetc_mutex_lock(&(s->lock), concurrent);

    retval = gasneti_atomic_read(&(s->count));
    if_pt(retval != 0)
      gasneti_atomic_decrement(&(s->count));

    gasnetc_mutex_unlock(&(s->lock), concurrent);
  #endif

  return retval;
}

#if GASNET_DEBUG
  GASNET_INLINE_MODIFIER(gasnetc_sema_check)
  void gasnetc_sema_check(gasnetc_sema_t *s, int limit) {
    uint32_t old = gasneti_atomic_read(&(s->count));

    gasneti_assert((old >= 0) && (old <= limit));
  }
  
  #define GASNETC_SEMA_CHECK(S, L)	gasnetc_sema_check((S),(L))
#else
  #define GASNETC_SEMA_CHECK(S, L)
#endif

/* ------------------------------------------------------------------------------------ */
/*
 * gasnetc_spinlock_t
 *
 * This is a simple busy-waiting lock used for mutual exclusion.
 * This type is only available if atomic swap is available
 */
#if GASNETI_HAVE_ATOMIC_SWAP

typedef struct {
  gasneti_atomic_t	lock;
} gasnetc_spinlock_t;

#define GASNETC_SPINLOCK_LOCKED		(0xcafef00d)
#define GASNETC_SPINLOCK_UNLOCKED	(0xdeadbeef)

#define GASNETC_SPINLOCK_INITIALIZER {gasneti_atomic_init(GASNETC_SPINLOCK_UNLOCKED)}

/* gasnetc_spinlock_init */
GASNET_INLINE_MODIFIER(gasnetc_spinlock_init)
void gasnetc_spinlock_init(gasnetc_spinlock_t *s) {
  gasneti_atomic_set(&(s->lock), GASNETC_SPINLOCK_UNLOCKED);
}

GASNET_INLINE_MODIFIER(gasnetc_spinlock_destroy)
void gasnetc_spinlock_destroy(gasnetc_spinlock_t *s) {
  gasneti_assert((gasneti_atomic_read(&(s->lock)) == GASNETC_SPINLOCK_LOCKED) ||
		 (gasneti_atomic_read(&(s->lock)) == GASNETC_SPINLOCK_UNLOCKED));
}

/* gasnetc_spinlock_unlock */
GASNET_INLINE_MODIFIER(gasnetc_spinlock_unlock)
void gasnetc_spinlock_unlock(gasnetc_spinlock_t *s) {
  gasneti_assert(gasneti_atomic_read(&(s->lock)) == GASNETC_SPINLOCK_LOCKED);
  gasneti_atomic_set(&(s->lock), GASNETC_SPINLOCK_UNLOCKED);
}

/* gasnetc_spinlock_try */
GASNET_INLINE_MODIFIER(gasnetc_spinlock_try)
int gasnetc_spinlock_try(gasnetc_spinlock_t *s) {
  #if GASNET_DEBUG
    int tmp = gasneti_atomic_read(&(s->lock));
    gasneti_assert((tmp == GASNETC_SPINLOCK_LOCKED) || (tmp == GASNETC_SPINLOCK_UNLOCKED));
  #endif

  return gasneti_atomic_swap(&(s->lock), GASNETC_SPINLOCK_UNLOCKED, GASNETC_SPINLOCK_LOCKED);
}

/* gasnetc_spinlock_lock */
GASNET_INLINE_MODIFIER(gasnetc_spinlock_lock)
void gasnetc_spinlock_lock(gasnetc_spinlock_t *s) {
  gasneti_waituntil(gasnetc_spinlock_try(s));
}

#define GASNETC_HAVE_SPINLOCK 1
#endif

/* ------------------------------------------------------------------------------------ */

/* Global freelist type
 *
 * Freelists in vapi-conduit have multiple consumers and multiple producers.
 * Thread-local lists don't use this data structure.
 *
 * Current implementation is a LIFO (stack) with a mutex.
 * Other possibilities include FIFO (queue) with mutex, or lock-free LIFO or FIFO
 */

/* Use spinlocks by default if they are available */
#ifndef GASNETI_FREELISTS_USE_SPINLOCK
  #define GASNETI_FREELISTS_USE_SPINLOCK GASNETC_HAVE_SPINLOCK
#endif

/*
 * Data type for the linkage of a freelist
 * Must be the first field in the data structure to be held in the list or the
 * data structure should be considered to be a union of this type and the real
 * type.  In the union case one can expect some number of bytes to get clobbered
 * while an element is on the freelist, while making this type the first element
 * (as shown below) preserves the contents of the free element (useful if there
 * are invariants to preserve).
 *
 * struct foobar {
 *   gasneti_freelist_ptr_t	linkage;
 *
 *   int			blah;
 *   double			boo;
 * };
 */
typedef struct _gasneti_freelist_ptr_s {
  struct _gasneti_freelist_ptr_s *next;
} gasneti_freelist_ptr_t;

/*
 * Data type for the "head" of a freelist.
 */
typedef struct {
  #if GASNETI_FREELISTS_USE_SPINLOCK
    gasnetc_spinlock_t		lock;
  #else
    gasneti_mutex_t		lock;
  #endif
  gasneti_freelist_ptr_t	*head;
} gasneti_freelist_t;

/* Initializer for staticly allocated freelists */
#if GASNETI_FREELISTS_USE_SPINLOCK
  #define GASNETI_FREELIST_INITIALIZER	{ GASNETC_SPINLOCK_INITIALIZER, NULL }
  #define GASNETI_FREELIST_LOCK(fl)	gasnetc_spinlock_lock(&((fl)->lock))
  #define GASNETI_FREELIST_UNLOCK(fl)	gasnetc_spinlock_unlock(&((fl)->lock))
#else
  #define GASNETI_FREELIST_INITIALIZER	{ GASNETC_MUTEX_INITIALIZER, NULL }
  #define GASNETI_FREELIST_LOCK(fl)	gasneti_mutex_lock(&((fl)->lock))
  #define GASNETI_FREELIST_UNLOCK(fl)	gasneti_mutex_unlock(&((fl)->lock))
#endif

/* Initializer for dynamically allocated freelists */
GASNET_INLINE_MODIFIER(gasneti_freelist_init)
void gasneti_freelist_init(gasneti_freelist_t *fl) {
  #if GASNETI_FREELISTS_USE_SPINLOCK
    gasnetc_spinlock_init(&(fl->lock));
  #else
    gasneti_mutex_init(&(fl->lock));
  #endif
  fl->head = NULL;
}

/* Get one element from the freelist or NULL if it is empty */
#ifdef __GNUC__
  GASNET_INLINE_MODIFIER(gasneti_freelist_get)
  void *gasneti_freelist_get(gasneti_freelist_t *fl) __attribute__((__malloc__));
#endif
GASNET_INLINE_MODIFIER(gasneti_freelist_get)
void *gasneti_freelist_get(gasneti_freelist_t *fl) {
  gasneti_freelist_ptr_t *head;

  GASNETI_FREELIST_LOCK(fl);
  head = fl->head;
  if_pt (head != NULL) {
    fl->head = head->next;
  }
  GASNETI_FREELIST_UNLOCK(fl);

  return (void *)head;
}

/* Put an unused element into the freelist */
GASNET_INLINE_MODIFIER(gasneti_freelist_put)
void gasneti_freelist_put(gasneti_freelist_t *fl, void *elem) {
  gasneti_assert(elem != NULL);

  GASNETI_FREELIST_LOCK(fl);
  ((gasneti_freelist_ptr_t *)elem)->next = fl->head;
  fl->head = elem;
  GASNETI_FREELIST_UNLOCK(fl);
}

/* Put a chain of unused elements into the freelist */
GASNET_INLINE_MODIFIER(gasneti_freelist_put_many)
void gasneti_freelist_put_many(gasneti_freelist_t *fl, void *head, void *tail) {
  gasneti_assert(head != NULL);
  gasneti_assert(tail != NULL);

  GASNETI_FREELIST_LOCK(fl);
  ((gasneti_freelist_ptr_t *)tail)->next = fl->head;
  fl->head = head;
  GASNETI_FREELIST_UNLOCK(fl);
}

/* Build a chain (q follows p) for use with _put_many() */
GASNET_INLINE_MODIFIER(gasneti_freelist_link)
void gasneti_freelist_link(void *p, void *q) {
  gasneti_assert(p != NULL);

  ((gasneti_freelist_ptr_t *)p)->next = q;
}

/* Get next element in a chain */
GASNET_INLINE_MODIFIER(gasneti_freelist_next)
void *gasneti_freelist_next(void *elem) {
  gasneti_assert(elem != NULL);

  return (void *)(((gasneti_freelist_ptr_t *)elem)->next);
}

/* ------------------------------------------------------------------------------------ */

/* Structure for a cep (connection end-point)
 * Include whatever per-node data we need.
 */
typedef struct {
  gasnetc_sema_t	op_sema;	/* control in-flight RDMA ops */
  gasnetc_sema_t	am_sema;	/* control in-flight AM Requests */
  VAPI_qp_hndl_t	qp_handle;
  #if GASNETC_PIN_SEGMENT
    /* RKey for the segment, registered at attach time */
    VAPI_rkey_t		rkey;
  #else
  #endif
} gasnetc_cep_t;

/* Description of a pre-pinned memory region */
typedef struct {
  VAPI_mr_hndl_t	handle;	/* used to release or modify the region */
  VAPI_lkey_t		lkey;	/* used for local access by HCA */
  VAPI_rkey_t		rkey;	/* used for remote access by HCA */
  uintptr_t		addr;
  size_t		len;
  uintptr_t		end;	/* inclusive */

  /* requested values, before rounding by HCA */
  void *		req_addr;
  size_t		req_size;
} gasnetc_memreg_t;

/* Bootstrap helper routines in gasnet_bootstrap_*.c */
extern void gasnetc_bootstrapInit(int *argc, char ***argv, gasnet_node_t *nodes, gasnet_node_t *mynode);
extern void gasnetc_bootstrapFini(void);
extern void gasnetc_bootstrapAbort(int exitcode) GASNET_NORETURN;
extern void gasnetc_bootstrapBarrier(void);
extern void gasnetc_bootstrapAllgather(void *src, size_t len, void *dest);
extern void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest);
extern void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode);

/* Routines in gasnet_core_sndrcv.c */
extern void gasnetc_sndrcv_init(void);
extern void gasnetc_sndrcv_fini(void);
extern void gasnetc_sndrcv_init_cep(gasnetc_cep_t *cep);
extern void gasnetc_sndrcv_fini_cep(gasnetc_cep_t *cep);
extern void gasnetc_sndrcv_poll(void);
extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_counter_t *mem_oust, va_list argptr);
extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				void *src_addr, int nbytes, void *dst_addr,
				int numargs, gasnetc_counter_t *mem_oust, va_list argptr);

/* General routines in gasnet_core.c */
extern void *gasnetc_alloc_pinned(size_t size, VAPI_mrw_acl_t acl, gasnetc_memreg_t *reg);
extern void gasnetc_free_pinned(gasnetc_memreg_t *reg);

/* Global configuration variables */
extern char		*gasnetc_hca_id;
extern IB_port_t	gasnetc_port_num;
extern int		gasnetc_op_oust_limit;
extern int		gasnetc_op_oust_pp;
extern int		gasnetc_am_oust_limit;
extern int		gasnetc_am_oust_pp;
extern int		gasnetc_am_spares;
extern int		gasnetc_bbuf_limit;
extern int		gasnetc_use_poll_lock;

/* Global variables */
extern gasnetc_cep_t	*gasnetc_cep;
extern VAPI_hca_hndl_t	gasnetc_hca;
extern VAPI_hca_cap_t	gasnetc_hca_cap;
extern VAPI_hca_port_t	gasnetc_hca_port;
extern VAPI_pd_hndl_t	gasnetc_pd;
extern gasnetc_memreg_t		gasnetc_snd_reg;
extern gasnetc_memreg_t		gasnetc_rcv_reg;
#if GASNETC_PIN_SEGMENT
  extern gasnetc_memreg_t	gasnetc_seg_reg;
#endif
#if GASNETC_USE_FIREHOSE
  extern size_t			gasnetc_fh_maxsz;
  extern firehose_info_t	gasnetc_firehose_info;
  #if FIREHOSE_VAPI_USE_FMR
    extern EVAPI_fmr_t		gasnetc_fmr_props;
  #endif
#endif

extern VAPI_cq_hndl_t	gasnetc_snd_cq;
extern VAPI_cq_hndl_t	gasnetc_rcv_cq;


#endif
