/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_help.h             $
 *     $Date: 2002/08/05 10:23:44 $
 * $Revision: 1.2 $
 * Description: GASNet elan conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

extern gasnet_node_t gasnetc_mynode;
extern gasnet_node_t gasnetc_nodes;

#define GASNETI_EADDRFMT "0x%08x"
#define GASNETI_EADDRSTR(ptr) ((uint32_t)(uintptr_t)(ptr))

END_EXTERNC

#endif
