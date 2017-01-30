/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core_internal.h $
 * Description: GASNet AXIOM conduit header for internal definitions in Core API
 *
 * Copyright (C) 2016, Evidence Srl.
 * Terms of use are as specified in COPYING
 *
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>

// RDMA aligmnet request! PS: power of two
#define GASNETC_ALIGN_SIZE 8

// usually 8 MiB (2048*PAGE)=2048*4096=8388608
//#define GASNETC_RESERVED_PAGES 2048
// 256 KiB
#define GASNETC_RESERVED_PAGES 64
// usually 64 MiB 8*8MiB (max num buffer 64! we are using a bitwise uint64_t for free/used buffer)
//#define GASNETC_NUM_BUFFERS 8
#define GASNETC_NUM_BUFFERS 4
#define GASNETC_BUFFER_SIZE (GASNETC_RESERVED_PAGES*GASNET_PAGESIZE)
// 64MiB
#define GASNETC_RESERVED_SPACE (GASNETC_NUM_BUFFERS*GASNETC_BUFFER_SIZE)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
/* add new core API handlers here and to the bottom of gasnet_core.c */

#ifndef GASNETE_HANDLER_BASE
  #define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
#elif GASNETE_HANDLER_BASE != 64
  #error "GASNETE_HANDLER_BASE mismatch between core and extended"
#endif

#define GASNETU_HANDLER_BASE 128

/* ------------------------------------------------------------------------------------ */
/* handler table (recommended impl) */
#define GASNETC_MAX_NUMHANDLERS   256
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
/* AM category (recommended impl if supporting PSHM) */
typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2
} gasnetc_category_t;

#endif
