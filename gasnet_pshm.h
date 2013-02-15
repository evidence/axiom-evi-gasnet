/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_pshm.h,v $
 *     $Date: 2009/09/18 23:33:23 $
 * $Revision: 1.1 $
 * Description: GASNet infrastructure for shared memory communications
 * Copyright 2009, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_SYSV_H
#define _GASNET_SYSV_H

#if !GASNET_PSHM
  #error "gasnet_pshm.h included in a non-PSHM build"
#endif

#include <gasnet_handler.h> /* Need gasneti_handler_fn_t */

#if GASNET_PAGESIZE < 4096
  #define GASNETI_PSHMNET_PAGESIZE 4096
  #define GASNETI_PSHMNET_PAGESHIFT 12
#else
  #define GASNETI_PSHMNET_PAGESIZE GASNET_PAGESIZE
  #define GASNETI_PSHMNET_PAGESHIFT GASNETI_PAGESHIFT
#endif

/* Max number of processes supported per node */
#ifndef GASNETI_PSHM_MAX_NODES
#define GASNETI_PSHM_MAX_NODES 255
#endif

/* In gasnet_mmap.c */
#define GASNETI_PSHM_UNIQUE_LEN 6
extern const char *gasneti_pshm_makenames(const char *unique);
extern void *gasneti_mmap_vnet(uintptr_t segsize);
extern void gasneti_unlink_vnet(void);

/* Virtual network between processes within a shared
 * memory 'supernode'.  
 * - Implemented as a set of message queues located in a shared memory space
 *   provided by the client of this API.
 */
struct gasneti_pshmnet;			/* opaque type */
typedef struct gasneti_pshmnet gasneti_pshmnet_t;

/* Initialize pshm request and reply networks given a conduit-specific exchange function.
   Returns pointer to shared memory of length "aux_sz" available for conduit-specific use */
extern void *gasneti_pshm_init(gasneti_bootstrapExchangefn_t exchangefn, size_t aux_sz);

extern gasneti_pshmnet_t *gasneti_request_pshmnet;
extern gasneti_pshmnet_t *gasneti_reply_pshmnet;

/* Optional conduit-specific code if defaults in gasnet_pshm.c are not usable.
 * Conduits providing these should #define the appropriate token in gasnet_core_fwd.h
 * When a given token is NOT defined, internal default implementations are used.
 *
 * If gasnet_core_fwd.h defines GASNETC_GET_HANDLER, conduit must provide:
 *   gasnetc_get_handler()
 *     Returns handler function for the given handler index
 *     For use ONLY by gasnet_pshm.[ch]
 *   gasnetc_handler_t
 *     Type (via typdef or #define) used for handlers instead of gasnet_handler_t
 *
 * If gasnet_core_fwd.h defines GASNETC_TOKEN_CREATE, conduit must provide
 * ALL of the following:
 *   gasnetc_token_create()
 *     Returns a token for use in AM handlers, distinguishable from conduit-native ones
 *     For use ONLY by gasnet_pshm.[ch]
 *   gasnetc_token_destroy()
 *     Destroys a token generated by gasnetc_token_create()
 *     For use ONLY by gasnet_pshm.[ch]
 *   gasnetc_token_reply()
 *     Performs reply-time debug checks (if any) on a token generated by gasnetc_token_create()
 *     For use ONLY by gasnet_pshm.[ch]
 *   gasnetc_token_is_pshm()
 *     Return non-zero if the token was generated by gasnetc_token_create()
 *     For general use (conduit use in gasnetc_AMReply*() is encouraged)
 */
#ifdef GASNETC_GET_HANDLER
  #ifndef gasnetc_get_handler
    extern gasneti_handler_fn_t gasnetc_get_handler(gasnetc_handler_t handler);
  #endif
#else
  #define gasnetc_handler_t gasnet_handler_t
#endif
#ifdef GASNETC_TOKEN_CREATE
  #ifndef gasnetc_token_create
    extern gasnet_token_t gasnetc_token_create(gasnet_node_t src, int isRequest);
  #endif
  #ifndef gasnetc_token_destroy
    extern void gasnetc_token_destroy(gasnet_token_t token);
  #endif
  #ifndef gasnetc_token_reply
    extern void gasnetc_token_reply(gasnet_token_t token);
  #endif
  #ifndef gasnetc_token_is_pshm
    extern int gasnetc_token_is_pshm(gasnet_token_t token);
  #endif
#else
  #define gasnetc_token_is_pshm(tok) ((uintptr_t)(tok)&1)

  /* Conduits using the default gasnetc_token_create() will
   * want/need to use this in their gasnetc_AMGetMsgSource().
   * Returns GASNET_OK if token was recognized, GASNET_ERR_BAD_ARG otherwise.
   */
  #if GASNET_DEBUG
    extern int gasneti_AMPSHMGetMsgSource(gasnet_token_t token, gasnet_node_t *src_ptr);
  #else
    GASNETI_INLINE(gasneti_AMPSHMGetMsgSource)
    int gasneti_AMPSHMGetMsgSource(gasnet_token_t token, gasnet_node_t *src_ptr) {
      if (gasnetc_token_is_pshm(token)) {
        *src_ptr = (gasnet_node_t)((uintptr_t)token >> 1);
        return GASNET_OK;
      } else {
        return GASNET_ERR_BAD_ARG;
      }
    }
  #endif

  #if GASNET_DEBUG
    extern void gasnetc_token_reply(gasnet_token_t token);
  #else
    #define gasnetc_token_reply(tok) ((void)0)
  #endif
#endif


#if GASNETI_PSHM_MAX_NODES < 256
  typedef uint8_t gasneti_pshm_rank_t;
#elif GASNETI_PSHM_MAX_NODES < 65536
  typedef uint16_t gasneti_pshm_rank_t;
#else
  #error "GASNETI_PSHM_MAX_NODES too large"
#endif

/*******************************************************************************
 * <PSHM variables that must be initialized by the conduit using PSHM>
 */
/*  PSHMnets needed for PSHM active messages.
 *
 * - Conduits using GASNET_PSHM must initialize these two vnets
 *   to allow a fast implementation of Active Messages to run within the
 *   supernode.  Other vnets may be created as are needed or useful.
 * - Initialize these vnets before use via gasneti_pshmnet_init().
 */
/* # of nodes in my supernode
 * my 0-based rank within it
 * lowest of gasnet node # in supernode */
extern gasneti_pshm_rank_t gasneti_pshm_nodes;
extern gasneti_pshm_rank_t gasneti_pshm_mynode;
extern gasnet_node_t gasneti_pshm_firstnode;

/* Non-NULL only when supernode members are non-contiguous */
extern gasneti_pshm_rank_t *gasneti_pshm_rankmap;

/*
 * </PSHM variables that must be initialized by the conduit using PSHM>
 *******************************************************************************/

/* Returns "local rank" if given node is in the callers supernode.
 * Otherwise returns an "impossible" value >= gasneti_pshm_nodes.
 */
GASNETI_INLINE(gasneti_pshmnet_local_rank)
gasneti_pshm_rank_t gasneti_pshm_local_rank(gasnet_node_t node) {
  if_pt (gasneti_pshm_rankmap == NULL) {
    /* NOTE: gasnet_node_t is an unsigned type, so in the case of
     * (node < gasneti_pshm_firstnode), the subtraction will wrap to
     * a "large" value.
     */
    return (node - gasneti_pshm_firstnode);
  } else {
    return gasneti_pshm_rankmap[node];
  }
}

/* Returns 1 if given node is in the caller's supernode, or 0 if it's not.
 * NOTE: result is false before vnet initialization.
 */
GASNETI_INLINE(gasneti_pshmnet_in_supernode)
int gasneti_pshm_in_supernode(gasnet_node_t node) {
  return (gasneti_pshm_local_rank(node) < gasneti_pshm_nodes);
}

/* Returns amount of memory needed (rounded up to a multiple of the system
 * page size) needed for a new gasneti_pshmnet_t.
 * - Takes the number of nodes in the gasnet supernode.
 * - Reads the GASNET_PSHMNET_QUEUE_DEPTH and GASNET_PSHMNET_QUEUE_MEMORY
 *   environment variables, if present.
 */
extern size_t gasneti_pshmnet_memory_needed(gasneti_pshm_rank_t nodes);

/* Creates a new virtual network within a gasnet shared memory supernode.
 * This function must be called collectively, and with a shared memory region
 * already created that is accessible to all nodes in the supernode
 * - 'start': starting address of region: must be page-aligned
 * - 'len': length of shared region: must be at least as long as the value
 *   returned from gasneti_pshmnet_memory_needed().
 * - 'nodes': count of the nodes in the supernode.
 */
extern gasneti_pshmnet_t *
gasneti_pshmnet_init(void *start, size_t len, gasneti_pshm_rank_t node_count);

/* Bootstrap barrier via pshmnet.
 *
 * This function has the following restrictions:
 * 1) It must be called after gasneti_pshmnet_init() has completed.
 * 2) It must be called collectively by all nodes in the vnet.
 */
extern
void gasneti_pshmnet_bootstrapBarrier(void);

/* Bootstrap broadcast via pshmnet.
 *
 * This function has the following restrictions:
 * 1) It must be called after gasneti_pshmnet_init() has completed.
 * 2) It must be called collectively by all nodes in the vnet.
 * 3) The rootpshmnode is the supernode-local rank
 */
extern
void gasneti_pshmnet_bootstrapBroadcast(gasneti_pshmnet_t *vnet, void *src, 
                                        size_t len, void *dest, int rootpshmnode);

/* Bootstrap exchange via pshmnet.
 *
 * This function has the following restrictions:
 * 1) It must be called after gasneti_pshmnet_init() has completed.
 * 2) It must be called collectively by all nodes in the vnet.
 */
extern
void gasneti_pshmnet_bootstrapExchange(gasneti_pshmnet_t *vnet, void *src, 
                                       size_t len, void *dest);

/* returns the maximum size payload that pshmnet can offer.  This is the
 * maximum size one can ask of gasneti_pshmnet_get_send_buffer.
 */
extern
size_t gasneti_pshmnet_max_payload(void);

/* Returns send buffer, into which message should be written.  Then
 * deliver_send_buffer() must be called, after which is is not safe to touch the
 * buffer any more.
 * - 'nbytes' must be <= gasneti_pshmnet_max_payload().
 * - Returns NULL if no buffer is available (poll your receive queue, then try
 *   again).
 * 'target' is rank relative to the supernode
 */
extern
void * gasneti_pshmnet_get_send_buffer(gasneti_pshmnet_t *vnet, size_t nbytes, 
                                       gasneti_pshm_rank_t target);

/* "Sends" message to target process.
 * Notifies target that message is ready to be received.  After calling, 'buf'
 * logically belongs to the target process, and the caller should not touch
 * the memory pointed to by 'buf' again.
 * 'target' is rank relative to the supernode
 *
 * Returns nonzero if no message can be sent (message queue full).  Poll your
 * own queues and try again later.
 */
extern
int gasneti_pshmnet_deliver_send_buffer(gasneti_pshmnet_t *vnet, void *buf, size_t nbytes,
                                        gasneti_pshm_rank_t target);


/* Polls receipt queue for any messages from any sender.
 * - 'pbuf': address of pointer which will point to message (if successful)
 * - 'psize': out parameter (msg length will be written into memory)
 * - 'from': out parameter (sender supernode-relative rank written into memory)
 *
 * returns nonzero if no message to receive */
extern
int gasneti_pshmnet_recv(gasneti_pshmnet_t *vnet, void **pbuf, size_t *psize, 
                         gasneti_pshm_rank_t *from);

/* Called by msg receiver, to release memory after message processed.
 * It is not safe to refer to the memory pointed to by 'buf' after this call
 * is made.
 */
extern
void gasneti_pshmnet_recv_release(gasneti_pshmnet_t *vnet, void *buf); 

/*******************************************************************************
 * AMPSHM: Active Messages over PSHMnet
 *******************************************************************************/

/* Processes pending messages:  if 'repliesOnly', only checks the 'reply'
 * PSHM network (i.e. gasneti_reply_pshmnet).  */
extern int gasneti_AMPSHMPoll(int repliesOnly);

/* Don't call this function directly: internal pshm function */
extern
int gasnetc_AMPSHM_ReqRepGeneric(int category, int isReq, gasnet_node_t dest,
                                 gasnetc_handler_t handler, void *source_addr, size_t nbytes, 
                                 void *dest_addr, int numargs, va_list argptr);

/* Generic AM handler for PSHMnet.
 * Divert your conduit's regular AM requests to this function if a call to
 * gasneti_pshm_in_supernode(dest) is nonzero */ 
GASNETI_INLINE(gasneti_AMPSHM_RequestGeneric)
int gasneti_AMPSHM_RequestGeneric(int category, gasnet_node_t dest, 
                                  gasnetc_handler_t handler, void *source_addr, size_t nbytes,
                                  void *dest_addr, int numargs, va_list argptr) 
{
  gasneti_assert(gasneti_pshm_in_supernode(dest));
  return gasnetc_AMPSHM_ReqRepGeneric(category, 1, dest, handler, source_addr,
                                      nbytes, dest_addr, numargs, argptr); 
}

/* Generic AM handler for PSHMnet.
 * Divert your conduit's regular AM replies to this function if a call to
 * gasneti_pshm_in_supernode(dest) or gasnetc_token_is_pshm(token) is nonzero */ 
GASNETI_INLINE(gasneti_AMPSHM_ReplyGeneric)
int gasneti_AMPSHM_ReplyGeneric(int category, gasnet_token_t token, 
                                gasnetc_handler_t handler, void *source_addr, 
                                size_t nbytes, void *dest_addr, int numargs, 
                                va_list argptr) 
{
  int retval;
  gasnet_node_t sourceid;
  gasneti_assert(gasnetc_token_is_pshm(token));
  gasnetc_AMGetMsgSource(token, &sourceid);
  gasneti_assert(gasneti_pshm_in_supernode(sourceid));
  gasnetc_token_reply(token);
  retval = gasnetc_AMPSHM_ReqRepGeneric(category, 0, sourceid, handler, source_addr, 
                                        nbytes, dest_addr, numargs, argptr); 
  return retval;
}

#endif /* _GASNET_SYSV_H */
