/*  $Archive:: /Ti/GASNet/elan-conduit/gasnet_extended_fwd.h                  $
 *     $Date: 2002/08/18 08:38:46 $
 * $Revision: 1.1 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      0.1
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         ELAN
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)


#ifdef GASNETI_THREADS
  #define GASNETI_THREADINFO_OPT
#endif

#ifdef GASNETI_THREADINFO_OPT
  /* Here we use a clever trick - GASNET_GET_THREADINFO() uses the sizeof(gasneti_threadinfo_available)
      to determine whether gasneti_threadinfo_cache was bound a value posted by GASNET_POST_THREADINFO()
      of if it bound to the globally declared dummy variables. 
     Even a very stupid C optimizer should constant-fold away the unused calls to gasneti_get_threadinfo() 
      and discard the unused variables
     We need 2 separate variables to ensure correct name-binding semantics for GASNET_BEGIN_FUNCTION()
   */
  static uint8_t gasnete_threadinfo_cache = 0;
  static uint8_t gasnete_threadinfo_available = 
    sizeof(gasnete_threadinfo_cache) + sizeof(gasnete_threadinfo_available);
    /* silly little trick to prevent unused variable warning on gcc -Wall */

  #define GASNET_POST_THREADINFO(info)                     \
    gasnet_threadinfo_t gasnete_threadinfo_cache = (info); \
    uint32_t gasnete_threadinfo_available = 0
    /* if you get an unused variable warning on gasnete_threadinfo_available, 
       it means you POST'ed in a function which made no GASNet calls that needed it */

  #define GASNET_GET_THREADINFO()                  \
    ( (sizeof(gasnete_threadinfo_available) == 1) ? \
      (gasnet_threadinfo_t)gasnete_mythread() :    \
      (gasnet_threadinfo_t)(uintptr_t)gasnete_threadinfo_cache )

  /* the gasnet_threadinfo_t pointer points to a thread data-structure owned by
     the extended API, whose first element is a pointer reserved
     for use by the core API (initialized to NULL)
   */
#else
  #define GASNET_POST_THREADINFO(info)   \
    static uint8_t gasnete_dummy = sizeof(gasnete_dummy) /* prevent a parse error */
  #define GASNET_GET_THREADINFO() (NULL)
#endif

#define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())

  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define CONDUIT_EXTENDED_STATS(CNT,VAL,TIME) \
        CNT(C, DYNAMIC_THREADLOOKUP, )           


#endif

