/* $Id: gasnet_core_help.h,v 1.1 2002/06/10 07:54:52 csbell Exp $
 * $Date: 2002/06/10 07:54:52 $
 * $Revision: 1.1 $
 * Description: GASNet gm conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
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

GASNET_INLINE_MODIFIER(gasnetc_portid);
uint16_t
gasnetc_portid(gasnet_node_t node)
{
	assert(node >= 0 && node < gasnetc_nodes);
	return _gmc.gm_nodes[node].port;
}

GASNET_INLINE_MODIFIER(gasnetc_nodeid);
uint16_t
gasnetc_nodeid(gasnet_node_t node)
{
	assert(node >= 0 && node < gasnetc_nodes);
	return _gmc.gm_nodes[node].id;
}

END_EXTERNC

#endif
