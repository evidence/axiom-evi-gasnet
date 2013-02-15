/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_fwd.h              $
 *     $Date: 2002/07/03 19:41:24 $
 * $Revision: 1.1 $
 * Description: GASNet header for lapi conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      0.1
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         LAPI
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)

/* this can be used to add conduit-specific 
 * statistical collection values (see gasnet_help.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

/* ===================  LAPI SPECIFIC CONSTANTS  ======================= */

/* defined to be 1 if gasnet_init guarantees that the remote-access memory
 * segment will be aligned at the same virtual address on all nodes. defined
 * to 0 otherwise */
/* On AIX, we SHOULD be able to mmap the same segment on each node.
 * I believe we can run different binaries in the same LAPI job so
 * this could fail if we just use sbrk. */
#define GASNET_ALIGNED_SEGMENTS   1

/* LAPI has no hard max, but we have to set something... */
#define GASNET_MAXNODES 1023


/* ===================  LAPI SPECIFIC TYPES  ======================= */

/* In LAPI, node identifiers are (signed) integers, gotten by LAPI_Qenv calls */
#define _GASNET_NODE_T
typedef int  gasnet_node_t;

/* Do we want to define a GASNET_TOKEN structure? */

/* Do we want to define thread local structure? (gasnet_threadinfo_t) */

/* Can use default HANDLERARG, HANDLERENTRY, and SEGINFO types */

#endif
