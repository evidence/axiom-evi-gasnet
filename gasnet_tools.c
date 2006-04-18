/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_tools.c,v $
 *     $Date: 2006/04/18 04:37:08 $
 * $Revision: 1.155 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if defined(GASNET_PARSYNC) || defined(GASNET_PAR) 
  #define GASNETT_THREAD_SAFE 1
#endif
#undef GASNET_SEQ
#undef GASNET_PAR
#undef GASNET_PARSYNC

#include <gasnet_tools.h>

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

/* atomics support */
#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  #ifdef _INCLUDED_GASNET_H
    gasnet_hsl_t gasneti_atomicop_lock = GASNET_HSL_INITIALIZER;
    void *gasneti_patomicop_lock = (void*)&gasneti_atomicop_lock;
    GASNETI_GENERIC_DEC_AND_TEST_DEF
    #ifdef GASNETI_GENERIC_CAS_DEF
      GASNETI_GENERIC_CAS_DEF
    #endif
    #ifdef GASNETI_GENERIC_ADD_SUB_DEF
      GASNETI_GENERIC_ADD_SUB_DEF
    #endif
  #elif defined(_REENTRANT) || defined(_THREAD_SAFE) || \
        defined(PTHREAD_MUTEX_INITIALIZER) ||           \
        defined(HAVE_PTHREAD) || defined(HAVE_PTHREAD_H)
    pthread_mutex_t gasneti_atomicop_mutex = PTHREAD_MUTEX_INITIALIZER;
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* call-based atomic support for C compilers with limited inline assembly */

#if defined(GASNETI_ATOMIC_SET_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_set,
  extern void _gasneti_special_atomic_set()) {
    GASNETI_ATOMIC_SET_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_READ_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_read,
  extern void _gasneti_special_atomic_read()) {
    GASNETI_ATOMIC_READ_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_INCREMENT_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_increment,
  extern void _gasneti_special_atomic_increment()) {
    GASNETI_ATOMIC_INCREMENT_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_DECREMENT_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_decrement,
  extern void _gasneti_special_atomic_decrement()) {
    GASNETI_ATOMIC_DECREMENT_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_decrement_and_test,
  extern void _gasneti_special_atomic_decrement_and_test()) {
    GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_compare_and_swap,
  extern void _gasneti_special_atomic_compare_and_swap()) {
    GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_ADD_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_add,
  extern void _gasneti_special_atomic_add()) {
    GASNETI_ATOMIC_ADD_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_SUBTRACT_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_subtract,
  extern void _gasneti_special_atomic_subtract()) {
    GASNETI_ATOMIC_SUBTRACT_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_FETCHADD_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_fetchadd,
  extern void _gasneti_special_atomic_fetchadd()) {
    GASNETI_ATOMIC_FETCHADD_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_ADDFETCH_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_addfetch,
  extern void _gasneti_special_atomic_addfetch()) {
    GASNETI_ATOMIC_ADDFETCH_BODY
  }
#endif

/* ------------------------------------------------------------------------------------ */
/* call-based membar/atomic support for C++ compilers which lack inline assembly */
#if defined(GASNETI_USING_SLOW_ATOMICS) || \
    defined(GASNETI_USING_SLOW_MEMBARS) || \
    defined(GASNETI_USING_SLOW_TIMERS)
#error gasnet_tools.c must be compiled with support for inline assembly
#endif

#if defined(GASNETI_TICKS_NOW_BODY)
  GASNETI_NEVER_INLINE(gasneti_slow_ticks_now,
  extern void gasneti_slow_ticks_now()) {
    GASNETI_TICKS_NOW_BODY
  }
#else
  extern gasneti_tick_t gasneti_slow_ticks_now() {
    return gasneti_ticks_now();
  }
#endif
extern void gasneti_slow_compiler_fence() {
  gasneti_compiler_fence();
}
extern void gasneti_slow_local_wmb() {
  gasneti_local_wmb();
}
extern void gasneti_slow_local_rmb() {
  gasneti_local_rmb();
}
extern void gasneti_slow_local_mb() {
  gasneti_local_mb();
}
extern uint32_t gasneti_slow_atomic_read(gasneti_atomic_t *p, const int flags) {
  return gasneti_atomic_read(p,flags);
}
extern void gasneti_slow_atomic_set(gasneti_atomic_t *p, uint32_t v, const int flags) {
  gasneti_atomic_set(p, v, flags);
}
extern void gasneti_slow_atomic_increment(gasneti_atomic_t *p, const int flags) {
  gasneti_atomic_increment(p, flags);
}
extern void gasneti_slow_atomic_decrement(gasneti_atomic_t *p, const int flags) {
  gasneti_atomic_decrement(p, flags);
}
extern int gasneti_slow_atomic_decrement_and_test(gasneti_atomic_t *p, const int flags) {
  return gasneti_atomic_decrement_and_test(p, flags);
}
#if defined(GASNETI_HAVE_ATOMIC_CAS)
  extern int gasneti_slow_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval, const int flags) {
    return gasneti_atomic_compare_and_swap(p,oldval,newval,flags);
  }
#endif
#if defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
  extern uint32_t gasneti_slow_atomic_add(gasneti_atomic_t *p, uint32_t op, const int flags) {
    return gasneti_atomic_add(p,op,flags);
  }
  extern uint32_t gasneti_slow_atomic_subtract(gasneti_atomic_t *p, uint32_t op, const int flags) {
    return gasneti_atomic_subtract(p,op,flags);
  }
#endif
/* ------------------------------------------------------------------------------------ */

#define GASNETT_THREADMODEL_STR _STRINGIFY(GASNETT_THREAD_MODEL)
GASNETI_IDENT(gasnett_IdentString_ThreadModel, "$GASNetToolsThreadModel: " GASNETT_THREADMODEL_STR " $");

GASNETI_IDENT(gasnett_IdentString_Config, "$GASNetToolsConfig: " GASNETT_CONFIG_STRING " $");

GASNETI_IDENT(gasnett_IdentString_BuildTimestamp, 
             "$GASNetBuildTimestamp: " __DATE__ " " __TIME__ " $");

GASNETI_IDENT(gasnett_IdentString_BuildID, 
             "$GASNetBuildId: " GASNETI_BUILD_ID " $");
GASNETI_IDENT(gasnett_IdentString_ConfigureArgs, 
             "$GASNetConfigureArgs: " GASNETI_CONFIGURE_ARGS " $");
GASNETI_IDENT(gasnett_IdentString_SystemTuple, 
             "$GASNetSystemTuple: " GASNETI_SYSTEM_TUPLE " $");
GASNETI_IDENT(gasnett_IdentString_SystemName, 
             "$GASNetSystemName: " GASNETI_SYSTEM_NAME " $");

int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_THREADMODEL) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_DEBUG_CONFIG) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_PTR_CONFIG) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_TIMER_CONFIG) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC_CONFIG) = 1;

/* ------------------------------------------------------------------------------------ */
extern double gasneti_tick_metric(int idx) {
  static double *_gasneti_tick_metric = NULL;
  gasneti_assert(idx <= 1);
  if_pf (_gasneti_tick_metric == NULL) {
    int i, ticks, iters = 1000, minticks = 10;
    double *_tmp_metric;
    gasneti_tick_t min = GASNETI_TICK_MAX;
    gasneti_tick_t start = gasneti_ticks_now();
    gasneti_tick_t last = start;
    for (i=0,ticks=0; i < iters || ticks < minticks; i++) {
      gasneti_tick_t x = gasneti_ticks_now();
      gasneti_tick_t curr = (x - last);
      if_pt (curr > 0) { 
        ticks++;
        if_pf (curr < min) min = curr;
      }
      last = x;
    }
    _tmp_metric = (double *)malloc(2*sizeof(double));
    gasneti_assert(_tmp_metric != NULL);
    /* granularity */
    _tmp_metric[0] = ((double)gasneti_ticks_to_ns(min))/1000.0;
    /* overhead */
    _tmp_metric[1] = ((double)(gasneti_ticks_to_ns(last - start)))/(i*1000.0);
    gasneti_sync_writes();
    _gasneti_tick_metric = _tmp_metric;
  } else gasneti_sync_reads();
  return _gasneti_tick_metric[idx];
}

/* ------------------------------------------------------------------------------------ */
#if defined(__sgi) || defined(__crayx1)
#define _SC_NPROCESSORS_ONLN _SC_NPROC_ONLN
#elif defined(_CRAYT3E)
#define _SC_NPROCESSORS_ONLN _SC_CRAY_MAXPES
#elif defined(HPUX)
#include <sys/param.h>
#include <sys/pstat.h>
#elif defined(__APPLE__) || defined(FREEBSD) || defined(NETBSD)
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
/* return the physical count of CPU's on this node, 
   or zero if that cannot be determined */
extern int gasneti_cpu_count() {
  static int hwprocs = -1;
  if (hwprocs >= 0) return hwprocs;

  #if defined(__APPLE__) || defined(FREEBSD) || defined(NETBSD)
      {
        int mib[2];
        size_t len;

        mib[0] = CTL_HW;
        mib[1] = HW_NCPU;
        len = sizeof(hwprocs);
        if (sysctl(mib, 2, &hwprocs, &len, NULL, 0)) {
           perror("sysctl");
           abort();
        }
        if (hwprocs < 1) hwprocs = 0;
      }
  #elif defined(HPUX) 
      {
        struct pst_dynamic psd;
        if (pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0) == -1) {
          perror("pstat_getdynamic");
          abort();
        } else {
          hwprocs = psd.psd_proc_cnt;
        }
      }
  #elif defined(SUPERUX) || defined(__MTA__)
      hwprocs = 0; /* appears to be no way to query CPU count on these */
  #else
      hwprocs = sysconf(_SC_NPROCESSORS_ONLN);
      if (hwprocs < 1) hwprocs = 0; /* catch failures on Solaris/Cygwin */
  #endif

  gasneti_assert_always(hwprocs >= 0);
  return hwprocs;
}
/* ------------------------------------------------------------------------------------ */
