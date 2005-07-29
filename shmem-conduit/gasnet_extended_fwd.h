/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/shmem-conduit/gasnet_extended_fwd.h,v $
 *     $Date: 2005/07/29 01:19:32 $
 * $Revision: 1.7 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      0.8
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         SHMEM
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)

#define _GASNET_HANDLE_T
typedef int * gasnet_handle_t;
#define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)

#define _GASNET_VALGET_HANDLE_T
typedef uintptr_t gasnet_valget_handle_t;

#define _GASNET_REGISTER_VALUE_T
#define SIZEOF_GASNET_REGISTER_VALUE_T SIZEOF_VOID_P
typedef uintptr_t gasnet_register_value_t;

  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define GASNETE_CONDUIT_STATS(CNT,VAL,TIME)  \
        CNT(C, DYNAMIC_THREADLOOKUP, cnt)    \
	GASNETI_REFVIS_STATS(CNT,VAL,TIME)   \
	GASNETI_REFCOLL_STATS(CNT,VAL,TIME)

#define GASNET_POST_THREADINFO(info)   \
  static uint8_t gasnete_dummy = sizeof(gasnete_dummy) /* prevent a parse error */
#define GASNET_GET_THREADINFO() (NULL)
#define GASNETE_THREAD_FARG_ALONE
#define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())

#define GASNETE_HAVE_EXTENDED_HELP_EXTRA_H

#endif
