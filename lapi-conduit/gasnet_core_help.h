/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_help.h             $
 *     $Date: 2002/11/15 23:32:26 $
 * $Revision: 1.3 $
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
#define GASNETC_USE_IBH 1
#else
#define GASNETC_USE_IBH 0
#endif

/* NOTE: this should be dependent on whether we compile in
 * 32 or 64 bit mode
 */
#define GASNETC_AM_MAX_ARGS 16

/* This should be the size of LAPI_UHDR_MAX - size of token */
#define GASNETC_AM_MAX_MEDIUM 800

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
