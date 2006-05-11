/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_tools.h,v $
 *     $Date: 2006/05/11 14:22:51 $
 * $Revision: 1.80 $
 * Description: GASNet Tools library 
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#ifndef _GASNET_TOOLS_H
#define _GASNET_TOOLS_H
#define _IN_GASNET_TOOLS_H
#define _INCLUDED_GASNET_TOOLS_H
#if !defined(_INCLUDED_GASNET_H) && \
    (defined(GASNET_SEQ) || defined(GASNET_PARSYNC) || defined(GASNET_PAR))
  #error Applications that use both GASNet and GASNet tools must   \
         include gasnet.h before gasnet_tools.h and must include   \
         _both_ headers in ALL files that need either header  
#endif

/* GASNETT_THREAD_SAFE may be defined by client to enable thread-safety */
#ifdef GASNETT_THREAD_SAFE
  #undef GASNETT_THREAD_SAFE
  #define GASNETT_THREAD_SAFE 1
  #define GASNETT_THREAD_MODEL PAR
#elif defined(GASNET_PARSYNC) || defined(GASNET_PAR) || \
      defined(_REENTRANT) || defined(_THREAD_SAFE) || \
      defined(PTHREAD_MUTEX_INITIALIZER)
  #define GASNETT_THREAD_SAFE 1
  #define GASNETT_THREAD_MODEL PAR
#else
  #define GASNETT_THREAD_MODEL SEQ
#endif

#include <gasnet_config.h>
#include <gasnet_basic.h>
#include <gasnet_toolhelp.h>

#include <stdio.h>
#include <stdlib.h>

/* allow conduit-specific tool helpers (eg elan timers) */
#ifdef GASNETI_TOOLS_HELPER
#include GASNETI_TOOLS_HELPER
#endif

#if defined(GASNET_NDEBUG)
  #define GASNETT_DEBUG_CONFIG nodebug
#else
  #define GASNETT_DEBUG_CONFIG debug
#endif
/* ------------------------------------------------------------------------------------ */
/* portable memory barriers */

#include <gasnet_membar.h>

#define gasnett_local_wmb()          gasneti_local_wmb()
#define gasnett_local_rmb()          gasneti_local_rmb()
#define gasnett_local_mb()           gasneti_local_mb()
#define gasnett_compiler_fence()     gasneti_compiler_fence()

/* ------------------------------------------------------------------------------------ */
/* portable atomic increment/decrement */

#include <gasnet_atomicops.h>

#define GASNETT_ATOMIC_NONE			GASNETI_ATOMIC_NONE
#define GASNETT_ATOMIC_RMB_PRE			GASNETI_ATOMIC_RMB_PRE
#define GASNETT_ATOMIC_RMB_POST			GASNETI_ATOMIC_RMB_POST
#define GASNETT_ATOMIC_RMB_POST_IF_TRUE		GASNETI_ATOMIC_RMB_POST_IF_TRUE
#define GASNETT_ATOMIC_RMB_POST_IF_FALSE	GASNETI_ATOMIC_RMB_POST_IF_FALSE
#define GASNETT_ATOMIC_WMB_PRE			GASNETI_ATOMIC_WMB_PRE
#define GASNETT_ATOMIC_WMB_POST			GASNETI_ATOMIC_WMB_POST
#define GASNETT_ATOMIC_REL			GASNETI_ATOMIC_REL
#define GASNETT_ATOMIC_ACQ			GASNETI_ATOMIC_ACQ
#define GASNETT_ATOMIC_ACQ_IF_TRUE		GASNETI_ATOMIC_ACQ_IF_TRUE
#define GASNETT_ATOMIC_ACQ_IF_FALSE		GASNETI_ATOMIC_ACQ_IF_FALSE
#define GASNETT_ATOMIC_MB_PRE			GASNETI_ATOMIC_MB_PRE
#define GASNETT_ATOMIC_MB_POST			GASNETI_ATOMIC_MB_POST

#define gasnett_atomic_val_t			gasneti_atomic_val_t
#define GASNETT_ATOMIC_MAX			GASNETI_ATOMIC_MAX

#define gasnett_atomic_sval_t			gasneti_atomic_sval_t
#define gasnett_atomic_signed(v)		gasneti_atomic_signed(v)
#define GASNETT_ATOMIC_SIGNED_MIN		GASNETI_ATOMIC_SIGNED_MIN
#define GASNETT_ATOMIC_SIGNED_MAX		GASNETI_ATOMIC_SIGNED_MAX

#if GASNETT_THREAD_SAFE
  /* PAR, PARSYNC and thread-safe tools clients */
  #define gasnett_atomic_t               gasneti_atomic_t
  #define gasnett_atomic_read(p,f)       gasneti_atomic_read(p,f)
  #define gasnett_atomic_init(v)         gasneti_atomic_init(v)
  #define gasnett_atomic_set(p,v,f)      gasneti_atomic_set(p,v,f)
  #define gasnett_atomic_increment(p,f)  gasneti_atomic_increment(p,f)
  #define gasnett_atomic_decrement(p,f)  gasneti_atomic_decrement(p,f)
  #define gasnett_atomic_decrement_and_test(p,f)  \
                                         gasneti_atomic_decrement_and_test(p,f)
  #ifdef GASNETI_HAVE_ATOMIC_CAS
    #define GASNETT_HAVE_ATOMIC_CAS 1
    #define gasnett_atomic_compare_and_swap(p,oldval,newval,f)  \
                                         gasneti_atomic_compare_and_swap(p,oldval,newval,f)
  #endif

  #ifdef GASNETI_HAVE_ATOMIC_ADD_SUB
    #define GASNETT_HAVE_ATOMIC_ADD_SUB 1
    #define gasnett_atomic_add(p,op,f)      gasneti_atomic_add(p,op,f)
    #define gasnett_atomic_subtract(p,op,f) gasneti_atomic_subtract(p,op,f)
  #endif

  #define gasnett_atomic32_t              gasneti_atomic32_t
  #define gasnett_atomic32_read(p,f)      gasneti_atomic32_read(p,f)
  #define gasnett_atomic32_init(v)        gasneti_atomic32_init(v)
  #define gasnett_atomic32_set(p,v,f)     gasneti_atomic32_set(p,v,f)
  #define gasnett_atomic32_compare_and_swap(p,oldval,newval,f)  \
                                          gasneti_atomic32_compare_and_swap(p,oldval,newval,f)

  #define gasnett_atomic64_t              gasneti_atomic64_t
  #define gasnett_atomic64_read(p,f)      gasneti_atomic64_read(p,f)
  #define gasnett_atomic64_init(v)        gasneti_atomic64_init(v)
  #define gasnett_atomic64_set(p,v,f)     gasneti_atomic64_set(p,v,f)
  #define gasnett_atomic64_compare_and_swap(p,oldval,newval,f)  \
                                            gasneti_atomic64_compare_and_swap(p,oldval,newval,f)
#else /* GASNET_SEQ and non-thread-safe tools clients */
  /* safe to use weak atomics here, because the client is single-threaded and 
     should only be modifying atomics from the host CPU (using these calls). 
     TODO: consider exposing "signal-safe" atomics (only avail on some platforms)
  */
  #define gasnett_atomic_t               gasneti_weakatomic_t
  #define gasnett_atomic_read(p,f)       gasneti_weakatomic_read(p,f)
  #define gasnett_atomic_init(v)         gasneti_weakatomic_init(v)
  #define gasnett_atomic_set(p,v,f)      gasneti_weakatomic_set(p,v,f)
  #define gasnett_atomic_increment(p,f)  gasneti_weakatomic_increment(p,f)
  #define gasnett_atomic_decrement(p,f)  gasneti_weakatomic_decrement(p,f)
  #define gasnett_atomic_decrement_and_test(p,f)  \
                                         gasneti_weakatomic_decrement_and_test(p,f)
  #ifdef GASNETI_HAVE_WEAKATOMIC_CAS
    #define GASNETT_HAVE_ATOMIC_CAS 1
    #define gasnett_atomic_compare_and_swap(p,oldval,newval,f)  \
                                         gasneti_weakatomic_compare_and_swap(p,oldval,newval,f)
  #endif

  #ifdef GASNETI_HAVE_WEAKATOMIC_ADD_SUB
    #define GASNETT_HAVE_ATOMIC_ADD_SUB 1
    #define gasnett_atomic_add(p,op,f)      gasneti_weakatomic_add(p,op,f)
    #define gasnett_atomic_subtract(p,op,f) gasneti_weakatomic_subtract(p,op,f)
  #endif

  #define gasnett_atomic32_t              gasneti_weakatomic32_t
  #define gasnett_atomic32_read(p,f)      gasneti_weakatomic32_read(p,f)
  #define gasnett_atomic32_init(v)        gasneti_weakatomic32_init(v)
  #define gasnett_atomic32_set(p,v,f)     gasneti_weakatomic32_set(p,v,f)
  #define gasnett_atomic32_compare_and_swap(p,oldval,newval,f)  \
                                          gasneti_weakatomic32_compare_and_swap(p,oldval,newval,f)

  #define gasnett_atomic64_t              gasneti_weakatomic64_t
  #define gasnett_atomic64_read(p,f)      gasneti_weakatomic64_read(p,f)
  #define gasnett_atomic64_init(v)        gasneti_weakatomic64_init(v)
  #define gasnett_atomic64_set(p,v,f)     gasneti_weakatomic64_set(p,v,f)
  #define gasnett_atomic64_compare_and_swap(p,oldval,newval,f)  \
                                          gasneti_weakatomic64_compare_and_swap(p,oldval,newval,f)
#endif

/* tight spin loop CPU hint */
#define gasnett_spinloop_hint()      gasneti_spinloop_hint() 

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
#define GASNETT_USING_GENERIC_ATOMICOPS
#endif
#define GASNETT_ATOMIC_CONFIG         GASNETI_ATOMIC_CONFIG
#define GASNETT_ATOMIC32_CONFIG       GASNETI_ATOMIC32_CONFIG
#define GASNETT_ATOMIC64_CONFIG       GASNETI_ATOMIC64_CONFIG
#define GASNETT_CONFIG_STRING                    \
       "PTR=" _STRINGIFY(GASNETI_PTR_CONFIG) "," \
       _STRINGIFY(GASNETT_DEBUG_CONFIG) ","      \
       _STRINGIFY(GASNETT_THREAD_MODEL) ","      \
       _STRINGIFY(GASNETT_TIMER_CONFIG) ","      \
       _STRINGIFY(GASNETT_ATOMIC_CONFIG) ","     \
       _STRINGIFY(GASNETT_ATOMIC32_CONFIG) ","   \
       _STRINGIFY(GASNETT_ATOMIC64_CONFIG)

/* ------------------------------------------------------------------------------------ */
/* portable high-performance, low-overhead timers */

#include <gasnet_timer.h>

#define gasnett_tick_t               gasneti_tick_t
#define GASNETT_TICK_MIN             GASNETI_TICK_MIN
#define GASNETT_TICK_MAX             GASNETI_TICK_MAX
#define gasnett_ticks_to_us(ticks)  (gasneti_ticks_to_ns(ticks)/1000)
#define gasnett_ticks_to_ns(ticks)   gasneti_ticks_to_ns(ticks)
#define gasnett_ticks_now()          gasneti_ticks_now()
#define gasnett_tick_granularityus() gasneti_tick_granularity()
#define gasnett_tick_overheadus()    gasneti_tick_overhead()
#define GASNETT_TIMER_CONFIG         GASNETI_TIMER_CONFIG
#define gasnett_gettimeofday_us()    gasneti_gettimeofday_us()
#ifdef GASNETI_USING_GETTIMEOFDAY
#define GASNETT_USING_GETTIMEOFDAY 1
#endif

/* ------------------------------------------------------------------------------------ */

/* misc */
#define gasnett_sched_yield     gasneti_sched_yield 
#define gasnett_cpu_count       gasneti_cpu_count
#define gasnett_flush_streams   gasneti_flush_streams
#define gasnett_close_streams   gasneti_close_streams
#define gasnett_getPhysMemSz    gasneti_getPhysMemSz
#define gasnett_fatalerror      gasneti_fatalerror
#define gasnett_killmyprocess   gasneti_killmyprocess
#define gasnett_current_loc     gasneti_current_loc
#define gasnett_sighandlerfn_t  gasneti_sighandlerfn_t
#define gasnett_reghandler      gasneti_reghandler
#define gasnett_checksum        gasneti_checksum
#define gasnett_format_number   gasneti_format_number
#define gasnett_parse_int       gasneti_parse_int
#define gasneti_isLittleEndian  gasneti_isLittleEndian
#define gasnett_set_affinity    gasneti_set_affinity

#define GASNETT_IDENT(identName, identText) GASNETI_IDENT(identName, identText)

#if defined(GASNET_PAGESIZE)
  #define GASNETT_PAGESIZE GASNET_PAGESIZE
#elif defined(GASNETI_PAGESIZE)
  #define GASNETT_PAGESIZE GASNETI_PAGESIZE
#endif

#if defined(GASNETI_PAGESHIFT)
  #define GASNETT_PAGESHIFT GASNETI_PAGESHIFT
#endif

#define GASNETT_CACHE_LINE_BYTES GASNETI_CACHE_LINE_BYTES

/* various configure-detected C compiler features available in only some compilers */
#define GASNETT_INLINE                  GASNETI_INLINE
#define GASNETT_ALWAYS_INLINE           GASNETI_ALWAYS_INLINE
#define GASNETT_PLEASE_INLINE           GASNETI_PLEASE_INLINE
#define GASNETT_NEVER_INLINE            GASNETI_NEVER_INLINE
#define GASNETT_RESTRICT                GASNETI_RESTRICT
#define GASNETT_NORETURN                GASNETI_NORETURN
#define GASNETT_NORETURNP               GASNETI_NORETURNP
#define GASNETT_MALLOC                  GASNETI_MALLOC
#define GASNETT_MALLOCP                 GASNETI_MALLOCP
#define GASNETT_PURE                    GASNETI_PURE
#define GASNETT_PUREP                   GASNETI_PUREP
#define GASNETT_CONST                   GASNETI_CONST
#define GASNETT_CONSTP                  GASNETI_CONSTP
#define GASNETT_WARN_UNUSED_RESULT      GASNETI_WARN_UNUSED_RESULT
#define GASNETT_FORMAT_PRINTF           GASNETI_FORMAT_PRINTF

#define GASNETT_CURRENT_FUNCTION        GASNETI_CURRENT_FUNCTION

#define GASNETT_BEGIN_EXTERNC           GASNETI_BEGIN_EXTERNC
#define GASNETT_END_EXTERNC             GASNETI_END_EXTERNC
#define GASNETT_EXTERNC                 GASNETI_EXTERNC
/* ------------------------------------------------------------------------------------ */

/* misc internal GASNet things we wish to expose when available */
GASNETI_BEGIN_EXTERNC

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_SRCLINES)
  #define GASNETT_TRACE_SETSOURCELINE      GASNETI_TRACE_SETSOURCELINE
  #define GASNETT_TRACE_GETSOURCELINE      GASNETI_TRACE_GETSOURCELINE
  #define GASNETT_TRACE_FREEZESOURCELINE   GASNETI_TRACE_FREEZESOURCELINE
  #define GASNETT_TRACE_UNFREEZESOURCELINE GASNETI_TRACE_UNFREEZESOURCELINE
#else
  #define GASNETT_TRACE_SETSOURCELINE(file,line)    ((void)0)
  #define GASNETT_TRACE_GETSOURCELINE(pfile,pline)  ((void)0)
  #define GASNETT_TRACE_FREEZESOURCELINE()          ((void)0)
  #define GASNETT_TRACE_UNFREEZESOURCELINE()        ((void)0)
#endif

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_TRACE)
  #define GASNETT_TRACE_ENABLED  GASNETI_TRACE_ENABLED(H)
  #define GASNETT_TRACE_PRINTF        _gasnett_trace_printf
  #define GASNETT_TRACE_PRINTF_FORCE  _gasnett_trace_printf_force
  GASNETI_FORMAT_PRINTF(_gasnett_trace_printf,1,2,
  extern void _gasnett_trace_printf(const char *format, ...));
  GASNETI_FORMAT_PRINTF(_gasnett_trace_printf_force,1,2,
  extern void _gasnett_trace_printf_force(const char *format, ...));
  #define GASNETT_TRACE_GETMASK()     GASNETI_TRACE_GETMASK()
  #define GASNETT_TRACE_SETMASK(mask) GASNETI_TRACE_SETMASK(mask)
  #define GASNETT_TRACE_GET_TRACELOCAL()        GASNETI_TRACE_GET_TRACELOCAL()
  #define GASNETT_TRACE_SET_TRACELOCAL(newval)  GASNETI_TRACE_SET_TRACELOCAL(newval)
#else
  #define GASNETT_TRACE_ENABLED  0
  #define GASNETT_TRACE_PRINTF        _gasnett_trace_printf
  #define GASNETT_TRACE_PRINTF_FORCE  _gasnett_trace_printf
  /*GASNETI_INLINE(_gasnett_trace_printf) 
   * causes many warnings because vararg fns cannot be inlined */
  static void _gasnett_trace_printf(const char *_format, ...) { 
    #ifdef __PGI
      va_list _ap; va_start(_ap,_format); va_end(_ap); /* avoid a silly warning */
    #endif
    return; 
  }
  #define GASNETT_TRACE_GETMASK()               ""
  #define GASNETT_TRACE_SETMASK(mask)           ((void)0)
  #define GASNETT_TRACE_GET_TRACELOCAL()        (0)
  #define GASNETT_TRACE_SET_TRACELOCAL(newval)  ((void)0)
#endif

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_STATS)
  /* GASNETT_STATS_INIT can be called at any time to register a callback function, which 
     will be invoked at stats dumping time (provided H stats are enabled)
     and passed a printf-like function that can be used to write output into the stats
   */
  #define GASNETT_STATS_INIT(callbackfn) \
    (gasnett_stats_callback = (callbackfn), GASNETI_STATS_ENABLED(H))
  #define GASNETT_STATS_GETMASK()     GASNETI_STATS_GETMASK()
  #define GASNETT_STATS_SETMASK(mask) GASNETI_STATS_SETMASK(mask)
#else
  #define GASNETT_STATS_INIT(callbackfn) 0
  #define GASNETT_STATS_GETMASK()     ""
  #define GASNETT_STATS_SETMASK(mask) ((void)0)
#endif

#if defined(_INCLUDED_GASNET_H) 
  /* these tools ONLY available when linking a libgasnet.a */
  #ifdef HAVE_MMAP
    extern void *gasneti_mmap(uintptr_t segsize);
    #define gasnett_mmap(sz) gasneti_mmap(sz)
  #else
    #define gasnett_mmap(sz) gasnett_fatalerror("gasnett_mmap not available")
  #endif
  #define gasnett_print_backtrace gasneti_print_backtrace
  extern int gasneti_run_diagnostics(int iters, int threadcnt, 
                                     const char *testsections, gasnet_seginfo_t const *seginfo);
  extern void gasneti_diagnostic_gethandlers(gasnet_handlerentry_t **htable, int *htable_cnt);
  #define gasnett_run_diagnostics gasneti_run_diagnostics
  #define gasnett_diagnostic_gethandlers gasneti_diagnostic_gethandlers

  #define gasnett_threadkey_t           gasneti_threadkey_t
  #define GASNETT_THREADKEY_INITIALIZER GASNETI_THREADKEY_INITIALIZER
  #define gasnett_threadkey_get(key)                gasneti_threadkey_get(key)
  #define gasnett_threadkey_set(key,newval)         gasneti_threadkey_set(key,newval)
  #define gasnett_threadkey_init(pkey)              gasneti_threadkey_init(pkey)
  #define gasnett_threadkey_get_noinit(key)         gasneti_threadkey_get_noinit(key)
  #define gasnett_threadkey_set_noinit(key,newval)  gasneti_threadkey_set_noinit(key,newval)

  #define GASNETT_FAST_ALIGNED_MEMCPY   GASNETE_FAST_ALIGNED_MEMCPY
  #define GASNETT_FAST_UNALIGNED_MEMCPY GASNETE_FAST_UNALIGNED_MEMCPY
  #define GASNETT_VALUE_ASSIGN          GASNETE_VALUE_ASSIGN
  #define GASNETT_VALUE_RETURN          GASNETE_VALUE_RETURN

  #if GASNET_DEBUG
    #define gasnett_debug_malloc(sz)      gasneti_extern_malloc(sz) 
    #define gasnett_debug_realloc(ptr,sz) gasneti_extern_realloc((ptr),(sz))
    #define gasnett_debug_calloc(N,S)     gasneti_extern_calloc((N),(S))
    #define gasnett_debug_free(ptr)       gasneti_extern_free(ptr)  
    #define gasnett_debug_strdup(s)       gasneti_extern_strdup(s)
    #define gasnett_debug_strndup(s,n)    gasneti_extern_strndup(s,n)
    #define gasnett_debug_memcheck(ptr)   gasneti_memcheck(ptr)
    #define gasnett_debug_memcheck_one()  gasneti_memcheck_one()
    #define gasnett_debug_memcheck_all()  gasneti_memcheck_all()
    #define gasnett_heapstats_t           gasneti_heapstats_t
    #define gasnett_getheapstats(pstat)   gasneti_getheapstats(pstat)
  #endif

  /* VIS string formatting */
  #define gasnett_format_memveclist_bufsz gasneti_format_memveclist_bufsz 
  #define gasnett_format_memveclist       gasneti_format_memveclist       
  #define gasnett_format_putvgetv_bufsz   gasneti_format_putvgetv_bufsz   
  #define gasnett_format_putvgetv         gasneti_format_putvgetv         
  #define gasnett_format_addrlist_bufsz   gasneti_format_addrlist_bufsz   
  #define gasnett_format_addrlist         gasneti_format_addrlist         
  #define gasnett_format_putigeti_bufsz   gasneti_format_putigeti_bufsz   
  #define gasnett_format_putigeti         gasneti_format_putigeti         
  #define gasnett_format_strides_bufsz    gasneti_format_strides_bufsz    
  #define gasnett_format_strides          gasneti_format_strides          
  #define gasnett_format_putsgets_bufsz   gasneti_format_putsgets_bufsz   
  #define gasnett_format_putsgets         gasneti_format_putsgets         

#else
  #define gasnett_mmap(sz)        gasnett_fatalerror("gasnett_mmap not available")

  #define gasnett_threadkey_t           char
  #define GASNETT_THREADKEY_INITIALIZER 0
  #define gasnett_threadkey_get(key)                gasnett_fatalerror("gasnett_threadkey_* not available")
  #define gasnett_threadkey_set(key,newval)         gasnett_fatalerror("gasnett_threadkey_* not available")
  #define gasnett_threadkey_init(pkey)              gasnett_fatalerror("gasnett_threadkey_* not available")
  #define gasnett_threadkey_get_noinit(key)         gasnett_fatalerror("gasnett_threadkey_* not available")
  #define gasnett_threadkey_set_noinit(key,newval)  gasnett_fatalerror("gasnett_threadkey_* not available")
#endif

/* ------------------------------------------------------------------------------------ */
/* Build config sanity checking */
#define GASNETT_LINKCONFIG_IDIOTCHECK(name) _CONCAT(gasnett_linkconfig_idiotcheck_,name)
extern int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_THREADMODEL);
extern int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_DEBUG_CONFIG);
extern int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_PTR_CONFIG);
extern int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_TIMER_CONFIG);
extern int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC_CONFIG);
extern int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC32_CONFIG);
extern int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC64_CONFIG);
static int *gasnett_linkconfig_idiotcheck();
static int *(*_gasnett_linkconfig_idiotcheck)() = &gasnett_linkconfig_idiotcheck;
static int *gasnett_linkconfig_idiotcheck() {
  static int val;
  val +=  GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_THREADMODEL)
        + GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_DEBUG_CONFIG)
        + GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_PTR_CONFIG)
        + GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_TIMER_CONFIG)
        + GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC_CONFIG)
        + GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC32_CONFIG)
        + GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC64_CONFIG)
        ;
  if (_gasnett_linkconfig_idiotcheck != gasnett_linkconfig_idiotcheck)
    val += *_gasnett_linkconfig_idiotcheck();
  return &val;
}

/* ------------------------------------------------------------------------------------ */

GASNETI_END_EXTERNC

#undef _IN_GASNET_TOOLS_H
#endif
