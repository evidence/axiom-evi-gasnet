/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/elan-conduit/Attic/gasnet_extended_fwd.h,v $
 *     $Date: 2006/01/23 17:34:05 $
 * $Revision: 1.22 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      1.7
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         ELAN
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)

/* ------------------------------------------------------------------------------------ */
/*
  Extended API Tuning Parameters
  ==============================
*/
#define GASNETE_MAX_COPYBUFFER_SZ  1048576    /* largest temp buffer we'll allocate for put/get */

#ifndef GASNETE_DEFAULT_NBI_THROTTLE
  #define GASNETE_DEFAULT_NBI_THROTTLE 1024
#endif

/* the size threshold where gets/puts stop using medium messages and start using longs */
#ifndef GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
#define GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD   gasnet_AMMaxMedium()
#endif

/* true if we should try to use Long replies in gets (only possible if dest falls in segment) */
#ifndef GASNETE_USE_LONG_GETS
#define GASNETE_USE_LONG_GETS 1
#endif

/* true if we should use elan put/get (setting to zero means all put/gets use AM only) */
#ifndef GASNETE_USE_ELAN_PUTGET
#define GASNETE_USE_ELAN_PUTGET 1
#endif

/* true to use elan hardware supported barrier */
#ifndef GASNETE_USE_ELAN_BARRIER
  #define GASNETE_USE_ELAN_BARRIER 1
#endif

/* true to "bend" the rules of barrier to improve performance
   (may deadlock if threads disagree on named/anon barrier flags) */
#ifndef GASNETE_FAST_ELAN_BARRIER
  #define GASNETE_FAST_ELAN_BARRIER 1
#endif

/* Ratio of elan pollfn callbacks to true AMPolls while barrier blocking
   must be power of two : BEWARE - raising this value hurts attentiveness at barriers
*/
#ifndef GASNETE_BARRIERBLOCKING_POLLFREQ
#if GASNETC_ELAN3
  #define GASNETE_BARRIERBLOCKING_POLLFREQ 1
#else
  #define GASNETE_BARRIERBLOCKING_POLLFREQ 1
#endif
#endif

#if GASNETE_USE_ELAN_BARRIER
  #define GASNETE_BARRIER_PROGRESSFN(FN) 
#endif

/* ------------------------------------------------------------------------------------ */

#define _GASNET_HANDLE_T
/*  an opaque type representing a non-blocking operation in-progress initiated using the extended API */
struct _gasnete_op_t;
typedef struct _gasnete_op_t *gasnet_handle_t;
#define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)

#if GASNETI_CLIENT_THREADS
  #define GASNETI_THREADINFO_OPT
  #define GASNETI_LAZY_BEGINFUNCTION
#endif

#ifdef GASNETI_THREADINFO_OPT
  /* Here we use a clever trick - GASNET_GET_THREADINFO() uses the sizeof(gasneti_threadinfo_available)
      to determine whether gasneti_threadinfo_cache was bound a value posted by GASNET_POST_THREADINFO()
      of if it bound to the globally declared dummy variables. 
     Even a very stupid C optimizer should constant-fold away the unused calls to gasneti_get_threadinfo() 
      and discard the unused variables
     We need 2 separate variables to ensure correct name-binding semantics for GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())
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

  #ifdef GASNETI_LAZY_BEGINFUNCTION
    #define GASNET_GET_THREADINFO()                              \
      ( (sizeof(gasnete_threadinfo_available) == 1) ?            \
        (gasnet_threadinfo_t)gasnete_mythread() :                \
        ( (uintptr_t)gasnete_threadinfo_cache == 0 ?             \
          ((*(gasnet_threadinfo_t *)&gasnete_threadinfo_cache) = \
            (gasnet_threadinfo_t)gasnete_mythread()) :           \
          (gasnet_threadinfo_t)(uintptr_t)gasnete_threadinfo_cache) )
  #else
    #define GASNET_GET_THREADINFO()                   \
      ( (sizeof(gasnete_threadinfo_available) == 1) ? \
        (gasnet_threadinfo_t)gasnete_mythread() :     \
        (gasnet_threadinfo_t)(uintptr_t)gasnete_threadinfo_cache )
  #endif

  /* the gasnet_threadinfo_t pointer points to a thread data-structure owned by
     the extended API, whose first element is a pointer reserved
     for use by the core API (initialized to NULL)
   */

  #ifdef GASNETI_LAZY_BEGINFUNCTION
    /* postpone thread discovery to first use */
    #define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(0)
  #else
    #define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())
  #endif

#else
  #define GASNET_POST_THREADINFO(info)   \
    static uint8_t gasnete_dummy = sizeof(gasnete_dummy) /* prevent a parse error */
  #define GASNET_GET_THREADINFO() (NULL)
  #define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())
#endif


  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define GASNETE_CONDUIT_STATS(CNT,VAL,TIME)  \
        GASNETI_REFVIS_STATS(CNT,VAL,TIME)   \
        GASNETI_REFCOLL_STATS(CNT,VAL,TIME)  \
        CNT(C, DYNAMIC_THREADLOOKUP, cnt)    \
        CNT(C, POLL_CALLBACK_BARRIER, cnt)   \
        CNT(C, POLL_CALLBACK_NOOP, cnt)      \
        CNT(C, EXHAUSTED_ELAN_MEMORY, cnt)

#endif

