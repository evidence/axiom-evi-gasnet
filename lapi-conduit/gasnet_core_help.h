/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_help.h             $
 *     $Date: 2002/12/11 19:53:36 $
 * $Revision: 1.5 $
 * Description: GASNet lapi conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

/* we dont need no stinkin Interrupt Based Handlers in LAPI */
#if DEBUG
/* #define GASNETC_USE_IBH 1 */
#define GASNETC_USE_IBH 0
#else
#define GASNETC_USE_IBH 0
#endif

/* NOTE: this should be dependent on whether we compile in
 * 32 or 64 bit mode
 */
#define GASNETC_AM_MAX_ARGS 16

/* This should be the size of the largest uhdr argument LAPI will
 * allow.  Note that this value is only availible from the LAPI
 * at runtime.  In future releases of LAPI it may have to be
 * adjusted.
 */
#if defined(__64BIT__)
#define GASNETC_UHDR_SIZE 908
#else
#define GASNETC_UHDR_SIZE 944
#endif

/* The max size of a medium message.  Can really be arbitrary size
 * but the receiving task must malloc space for the incoming
 * message.  Note that messages that fit into a single token
 * are optimized.
 */
#define GASNETC_AM_MAX_MEDIUM 16384

/* In 32 bit mode, this is 2^31 - 1 bytes.  */
#define GASNETC_AM_MAX_LONG 2147483647

/* stuff needed for the BLOCKUNTIL macro */
#include <lapi.h>
typedef enum {
    gasnetc_Interrupt = 0,
    gasnetc_Polling
} gasnetc_lapimode_t;

extern lapi_handle_t      gasnetc_lapi_context;
extern gasnetc_lapimode_t gasnetc_lapi_default_mode;

extern gasnet_node_t gasnetc_mynode;
extern gasnet_node_t gasnetc_nodes;

END_EXTERNC

#endif
