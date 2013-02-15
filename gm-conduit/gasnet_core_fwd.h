/* $Id: gasnet_core_fwd.h,v 1.1 2002/06/10 07:54:52 csbell Exp $
 * $Date: 2002/06/10 07:54:52 $
 * $Revision: 1.1 $
 * Description: GASNet header for gm conduit core (forward definitions)
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      0.1
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         gm

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS	1
#define GASNET_MAXNODES		1024

#define _GASNET_NODE_T
typedef uint16_t	gasnet_node_t;
#define _GASNET_HANDLER_T
typedef uint8_t		gasnet_handler_t;

#define _GASNET_TOKEN_T
typedef
struct {
	int	did_reply;
}* gasnet_token_t;

#endif /* _GASNET_CORE_FWD_H */
