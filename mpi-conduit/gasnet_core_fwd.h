/*  $Archive:: /Ti/GASNet/mpi-conduit/gasnet_core_fwd.h                   $
 *     $Date: 2002/06/01 14:24:57 $
 * $Revision: 1.1 $
 * Description: GASNet header for MPI conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      0.1
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         AMMPI

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1 

/*  override default error values to use those defined by AMMPI */
#define _GASNET_ERRORS
#define _GASNET_ERR_BASE 10000
#define GASNET_ERR_NOT_INIT             AM_ERR_NOT_INIT
#define GASNET_ERR_RESOURCE             AM_ERR_RESOURCE
#define GASNET_ERR_BAD_ARG              AM_ERR_BAD_ARG
#define GASNET_ERR_NOT_READY            (_GASNET_ERR_BASE+4)
#define GASNET_ERR_BARRIER_MISMATCH     (_GASNET_ERR_BASE+5)


#endif
