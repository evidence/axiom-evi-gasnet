/*  $Archive:: /Ti/GASNet/gasnet_timer.h                                   $
 *     $Date: 2002/08/19 11:10:27 $
 * $Revision: 1.1 $
 * Description: GASNet Timer library (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_TIMER_H
#define _GASNET_TIMER_H

BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */
/* High-performance system timer library 

  Implements high-granularity, low-overhead timers using system-specific support, where available

  Interface:
    gasneti_stattime_t - timer datatype representing an integer number of "ticks"
      where a "tick" has a system-specific interpretation
      safe to be handled using integer operations (+,-,<,>,==)
    GASNETI_STATTIME_NOW() - returns the current tick count as a gasneti_stattime_t
    GASNETI_STATTIME_TO_US(stattime) - convert ticks to microseconds as a uint64_t
    GASNETI_STATTIME_MIN - a value representing the minimum value storable in a gasneti_stattime_t
    GASNETI_STATTIME_MAX - a value representing the maximum value storable in a gasneti_stattime_t
*/
#if defined(GASNETC_CONDUIT_SPECIFIC_TIMERS)
  #if !defined(GASNETI_STATTIME_MIN) || !defined(GASNETI_STATTIME_MAX) || \
      !defined(GASNETI_STATTIME_TO_US) || !defined(GASNETI_STATTIME_NOW)
    #error Incomplete conduit-specific timer impl.
  #endif
#elif defined(AIX)
  #include <sys/time.h>
  #include <sys/systemcfg.h>

  /* we want to avoid expensive divide and conversion operations during collection, 
     but timebasestruct_t structs are too difficult to perform arithmetic on
     we stuff the internal cycle counter into a 64-bit holder and expand to realtime later */
  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    timebasestruct_t t;
    read_real_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) << 32) | ((uint64_t)t.tb_low);
  }
  GASNET_INLINE_MODIFIER(gasneti_stattime_to_us)
  uint64_t gasneti_stattime_to_us(gasneti_stattime_t st) {
    timebasestruct_t t;
    read_real_time(&t,TIMEBASE_SZ);
    assert(t.flag == RTC_POWER_PC); /* otherwise timer arithmetic (min/max/sum) is compromised */

    t.tb_high = (uint32_t)(st >> 32);
    t.tb_low =  (uint32_t)(st);
    time_base_to_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) * 1000000) + (t.tb_low/1000);
  }
  #define GASNETI_STATTIME_TO_US(st)  (gasneti_stattime_to_us(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(CRAYT3E)
  #include <sys/machinfo.h>
  long    _rtc();

  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)

  #if 0
    #define GASNETI_STATTIME_TO_US(st)  ((st) * 1000000 / GetMachineInfo(mi_hz))
  #else
    /* 75 Mhz sys. clock */
    #define GASNETI_STATTIME_TO_US(st)  ((st) / 75)
  #endif
  #define GASNETI_STATTIME_NOW()      (_rtc())
#elif defined(IRIX)
  #include <time.h>
  #include <sys/ptimers.h>

  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    struct timespec t;
    if (clock_gettime(CLOCK_SGI_CYCLE, &t) == -1) abort();
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(SOLARIS)
  #include <sys/time.h>
  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
  #define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
  #define GASNETI_STATTIME_NOW()      (gethrtime())
#elif defined(LINUX) && defined(__GNUC__) && defined(__i386__)
  #include <stdio.h>
  #include <string.h>
  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  uint64_t gasneti_stattime_now (void) {
    unsigned long long ret;
    __asm__ __volatile__("rdtsc"
                        : "=A" (ret)
                        : /* no inputs */); 
    return ret;
  } 
  GASNET_INLINE_MODIFIER(gasneti_stattime_to_us)
  uint64_t gasneti_stattime_to_us(gasneti_stattime_t st) {
    static int firstTime = 1;
    static double MHz;
    if (firstTime) {
      FILE *fp = fopen("/proc/cpuinfo","r");
      char input[255];
      while (!feof(fp) && fgets(input, 255, fp)) {
        if (strstr(input,"cpu MHz")) {
          char *p = strchr(input,':');
          if (p) MHz = atof(p+1);
          firstTime = 0;
        }
      }
      fclose(fp);
      assert(!firstTime);
    }
    return st / MHz;
  }
  #define GASNETI_STATTIME_TO_US(st)  (gasneti_stattime_to_us(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif 0 && defined(OSF)
  /* the precision for this is no better than gettimeofday (~1 ms) */
  /* TODO: use elan real-time counter, or rpcc instruction (which returns
     a 32-bit cycle count that wraps too quickly to be useful by itself)
     luckily, the Quadrics NIC provides a nanosecond clock (with ~1us overhead)
   */
  #include <time.h>

  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t) == -1) abort();
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#else
  #define GASNETI_USING_GETTIMEOFDAY
  /* portable microsecond granularity wall-clock timer */
  extern int64_t gasneti_getMicrosecondTimeStamp(void);
  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
  #define GASNETI_STATTIME_TO_US(st)  (st)
  #define GASNETI_STATTIME_NOW()      ((gasneti_stattime_t)gasneti_getMicrosecondTimeStamp())
#endif


/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif
