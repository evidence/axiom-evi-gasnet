/* $Id: gasnet_core_help.h,v 1.2 2002/06/13 10:09:32 csbell Exp $
 * $Date: 2002/06/13 10:09:32 $
 * $Revision: 1.2 $
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

extern gasnet_node_t 		gasnetc_mynode;
extern gasnet_node_t 		gasnetc_nodes;

/* AM Header stores the following fields
 *
 * 0b01234567
 *
 * 0-1: AM Message type (00=small, 01=medium, 10=long, 11=system)
 * 2-6: AM Number of arguments (00000=0, 00001=1, 00010=2, ...)
 * 7:   AM Request/Reply (0=request, 1=reply)
 *
 * AM Index stores the handler index and is one byte next to the 
 * Header
 */

#define AM_SHORT	0x00
#define AM_MEDIUM	0x01
#define AM_LONG		0x02
#define AM_SYSTEM	0x03

#define AM_REQUEST	0x00
#define	AM_REPLY	0x01

#define GASNETC_AM_LEN  4096 
#define GASNETC_AM_MAX_ARGS	16
#define GASNETC_AM_MAX_HANDLERS 255
#define GASNETC_AM_SHORT_HEADER_LEN(numargs)	((numargs)*4 + 4)
#define GASNETC_AM_MEDIUM_HEADER_LEN(numargs)	((numargs)*4 + 4)
#define GASNETC_AM_LONG_HEADER_LEN(numargs)	((numargs)*4 + 4 + 4 + \
		                                  sizeof(uintptr_t))
#define GASNETC_AM_PAYLOAD_LEN \
		GASNETC_AM_LEN - \
		GASNETC_AM_MEDIUM_HEADER_LEN(GASNETC_AM_MAX_ARGS)
#define GASNETC_AM_SMALL_MAX		GASNETC_AM_PAYLOAD_LEN
#define GASNETC_AM_MEDIUM_MAX		GASNETC_AM_PAYLOAD_LEN
#define GASNETC_AM_LONG_REPLY_MAX	GASNETC_AM_PAYLOAD_LEN	
#define GASNETC_AM_LONG_REQUEST_MAX \
		3*GASNETC_AM_LEN + GASNETC_AM_LEN - \
		GASNETC_AM_LONG_HEADER_LEN(GASNETC_AM_MAX_ARGS)

#define GASNETC_ASSERT_AMSHORT(buf, type, handler, args, req) 		\
	do { 	assert(buf != NULL); 					\
		assert(am_type >= AM_SHORT && am_type <= AM_SYSTEM);	\
		assert(request == 0 || request == 1);			\
		assert(numargs >= 0 && numargs <= 16);			\
		assert(handler >= 0 && handler <= 255);			\
	} while (0)

#define GASNETC_ASSERT_AMMEDIUM(buf, type, handler, args, req, len, src) \
	do {	GASNETC_ASSERT_AMSHORT(buf, type, handler, args, req);   \
		assert(len > 0);					 \
		assert(src != NULL);					 \
	} while (0)

#define GASNETC_ASSERT_AMLONG(buf, type, handler, args, req, len, src, dest) \
	do {	GASNETC_ASSERT_AMMEDIUM(buf, type, handler, args, req, len,  \
			                src);				     \
		assert(dest != NULL);					     \
	} while (0)

END_EXTERNC

#endif
