/* $Id: gasnet_core_fwd.h,v 1.23 2003/10/11 13:09:59 bonachea Exp $
 * $Date: 2003/10/11 13:09:59 $
 * $Revision: 1.23 $
 * Description: GASNet header for GM conduit core (forward definitions)
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      1.5
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         GM
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_GM        1

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS	1
#define GASNET_MAXNODES		1024

  /* conduits should define GASNETI_CONDUIT_THREADS if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
/* #define GASNETI_CONDUIT_THREADS */

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
#define GASNETC_USE_INTERRUPTS 0

/* only have firehose for now */
#define GASNETC_FIREHOSE

/* Default board number */
#define GASNETC_DEFAULT_GM_BOARD_NUM	0

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_help.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#define _GASNET_NODE_T
typedef uint16_t	gasnet_node_t;
#define _GASNET_HANDLER_T
typedef uint8_t		gasnet_handler_t;

#define _GASNET_TOKEN_T
struct gasnetc_bufdesc;
typedef struct gasnetc_bufdesc *gasnet_token_t;

#endif /* _GASNET_CORE_FWD_H */
