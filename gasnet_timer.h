/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_timer.h,v $
 *     $Date: 2006/05/27 01:04:27 $
 * $Revision: 1.63 $
 * Description: GASNet Timer library (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_TIMER_H
#define _GASNET_TIMER_H

/* general includes (to avoid repetition below) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

GASNETI_BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */
/* High-performance system timer library 

  Implements high-granularity, low-overhead timers using system-specific support, where available

  Interface:
    gasneti_tick_t - timer datatype representing an integer number of "ticks"
      where a "tick" has a system-specific interpretation
      safe to be handled using integer operations (+,-,<,>,==)
    gasneti_tick_t gasneti_ticks_now() - returns the current tick count 
      note that tick values are THREAD-specific, and do NOT represent a globally-synchronized timer.
      In specific, tick values are very likely to have a different base value across nodes, and 
      might even advance at substantially different rates on different nodes.
      Therefore tick values and tick intervals from different threads should never be directly compared or 
      arithmetically combined, without first converting the relevant tick intervals to wall time intervals.
    gasneti_ticks_to_ns(gasneti_tick_t ticks) - convert ticks to nanoseconds as a uint64_t
    GASNETI_TICK_MIN - a value representing the minimum value storable in a gasneti_tick_t
    GASNETI_TICK_MAX - a value representing the maximum value storable in a gasneti_tick_t
*/

#if defined(GASNETC_CONDUIT_SPECIFIC_TIMERS)
  #if !defined(gasneti_ticks_to_ns) || !defined(gasneti_ticks_now)
    /* conduit-specific timers must be implemented using a macro */
    #error Incomplete conduit-specific timer impl.
  #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_OS_AIX
  #include <sys/time.h>
  #include <sys/systemcfg.h>

  /* we want to avoid expensive divide and conversion operations during collection, 
     but timebasestruct_t structs are too difficult to perform arithmetic on
     we stuff the internal cycle counter into a 64-bit holder and expand to realtime later */
  typedef uint64_t gasneti_tick_t;
  GASNETI_INLINE(gasneti_ticks_now)
  gasneti_tick_t gasneti_ticks_now() {
    timebasestruct_t t;
    read_real_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) << 32) | ((uint64_t)t.tb_low);
  }
  GASNETI_INLINE(gasneti_ticks_to_ns)
  uint64_t gasneti_ticks_to_ns(gasneti_tick_t st) {
    timebasestruct_t t;
    gasneti_assert((read_real_time(&t,TIMEBASE_SZ), 
                   t.flag == RTC_POWER_PC)); /* otherwise timer arithmetic (min/max/sum) is compromised */
    t.flag = RTC_POWER_PC;
    t.tb_high = (uint32_t)(st >> 32);
    t.tb_low =  (uint32_t)(st);
    time_base_to_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) * 1000000000) + t.tb_low;
  }
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_CRAYT3E || PLATFORM_ARCH_CRAYX1
  typedef uint64_t gasneti_tick_t;
  #if PLATFORM_COMPILER_GNU
    #define _rtc rtclock
  #else
    extern long _rtc();
  #endif

  #define gasneti_ticks_now()      (_rtc())

  #if PLATFORM_ARCH_CRAYT3E || defined(GASNETI_UNICOS_SYS_CLOCK)
    #include <sys/machinfo.h>
    #ifndef GASNETI_UNICOS_SYS_CLOCK /* T3E has 75 Mhz sys. clock */
    #define GASNETI_UNICOS_SYS_CLOCK 75000000
    #endif
    #define gasneti_ticks_to_ns(st)  ((gasneti_tick_t)(((gasneti_tick_t)(st)) * (1000000000.0 / GASNETI_UNICOS_SYS_CLOCK)))
  #elif PLATFORM_ARCH_CRAYX1
    #include <intrinsics.h>
    extern long IRTC_RATE();
    /* 100 or 113 Mhz sys. clock, depending on hardware */
    GASNETI_INLINE(gasneti_ticks_to_ns)
    uint64_t gasneti_ticks_to_ns(gasneti_tick_t st) {
      static int gasneti_rtc_rate_set = 0;
      static double gasneti_rtc_rate;
      if_pf (!gasneti_rtc_rate_set) {
        long const rateval = IRTC_RATE();
        gasneti_assert(rateval > 1E6 && rateval < 1E12); /* sanity check */
        gasneti_rtc_rate = 1000000000.0 / rateval;
        gasneti_local_wmb();
        gasneti_rtc_rate_set = 1;
      }
      return st * gasneti_rtc_rate;
    }
  #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_OS_IRIX
  #include <time.h>
  #include <sys/ptimers.h>

  typedef uint64_t gasneti_tick_t;
  GASNETI_INLINE(gasneti_ticks_now)
  gasneti_tick_t gasneti_ticks_now() {
    struct timespec t;
    gasneti_assert_zeroret(clock_gettime(CLOCK_SGI_CYCLE, &t));
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define gasneti_ticks_to_ns(st)  (st)
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_OS_MTA
  #include <sys/mta_task.h>
  #include <machine/mtaops.h>

  typedef int64_t gasneti_tick_t;
  GASNETI_INLINE(gasneti_ticks_to_ns)
  uint64_t gasneti_ticks_to_ns(gasneti_tick_t ticks) {
    static int firsttime = 1;
    static double adjust;
    if_pf(firsttime) {
      double freq = mta_clock_freq();
      adjust = 1.0E9/freq;
      gasneti_sync_writes();
      firsttime = 0;
      #if 0
        printf("first time: ticks=%llu  freq=%f adjust=%f\n", 
              (unsigned long long) ticks, freq, adjust);
      #endif
    } else gasneti_sync_reads();
    return (uint64_t)(((double)ticks) * adjust);
  }
  #define gasneti_ticks_now()      (MTA_CLOCK(0))
  #define GASNETI_TICK_MAX        ((gasneti_tick_t)(((uint64_t)-1)>>1))
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_OS_SOLARIS
#if 1
  /* workaround bizarre failures on gcc 3.2.1 - seems they sometimes use a
     union to implement longlong_t and hence hrtime_t, and the test to
     determine this is (__STDC__ - 0 == 0) which is totally bogus */
  typedef uint64_t gasneti_tick_t;
  GASNETI_INLINE(gasneti_ticks_now)
  gasneti_tick_t gasneti_ticks_now() {
    hrtime_t t = gethrtime();
    return *(gasneti_tick_t *)&t;
  }
  #define gasneti_ticks_to_ns(st)  (st)
#else
  typedef hrtime_t gasneti_tick_t;
  GASNETI_INLINE(gasneti_ticks_to_ns)
  uint64_t gasneti_ticks_to_ns(gasneti_tick_t st) {
    gasneti_assert(sizeof(gasneti_tick_t) == 8);
    return *(uint64_t*)&st;
  }
  #define gasneti_ticks_now()      (gethrtime())
  #define GASNETI_TICK_MAX        ((gasneti_tick_t)(((uint64_t)-1)>>1))
#endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_OS_CATAMOUNT && PLATFORM_COMPILER_PGI && !PGI_WITH_REAL_ASM && 0 /* DISABLED */
  #include <catamount/dclock.h>
  typedef uint64_t gasneti_tick_t;
  #define gasneti_ticks_to_ns(st)  (st)
  #define gasneti_ticks_now()      ((gasneti_tick_t)(dclock()*1E9))
/* ------------------------------------------------------------------------------------ */
#elif (PLATFORM_OS_LINUX || PLATFORM_OS_CATAMOUNT) && \
     (PLATFORM_COMPILER_GNU || PLATFORM_COMPILER_INTEL || \
      PLATFORM_COMPILER_PATHSCALE || PLATFORM_COMPILER_PGI) && \
     (PLATFORM_ARCH_X86 || PLATFORM_ARCH_X86_64 || PLATFORM_ARCH_IA64)
  #if PLATFORM_ARCH_IA64 && PLATFORM_COMPILER_INTEL
    #include <ia64intrin.h>
  #endif
  #if PLATFORM_OS_CATAMOUNT
    extern unsigned int __cpu_mhz; /* system provided */
  #endif
  typedef uint64_t gasneti_tick_t;
 #if PLATFORM_COMPILER_PGI && !PGI_WITH_REAL_ASM
   #if PLATFORM_ARCH_X86
     #define GASNETI_TICKS_NOW_BODY GASNETI_ASM("rdtsc");
   #elif PLATFORM_ARCH_X86_64
     #define GASNETI_TICKS_NOW_BODY                   \
		GASNETI_ASM( "xor %rax, %rax	\n\t" \
			     "rdtsc		\n\t" \
			     "shl $32, %rdx	\n\t" \
			     "or %rdx, %rax" );
   #elif PLATFORM_ARCH_IA64
     /* For completeness. */
     #define GASNETI_TICKS_NOW_BODY \
		GASNETI_ASM( "mov.m r8=ar.itc;" );
   #endif
 #elif PGI_WITH_REAL_ASM && defined(__cplusplus)
  #define GASNETI_USING_SLOW_TIMERS 1
 #else
  GASNETI_INLINE(gasneti_ticks_now)
  uint64_t gasneti_ticks_now (void) {
    uint64_t ret;
    #if PLATFORM_ARCH_X86_64 || (PLATFORM_ARCH_X86 && PGI_WITH_REAL_ASM)
      uint32_t lo, hi;
      __asm__ __volatile__("rdtsc"
                           : "=a" (lo), "=d" (hi)
                           : /* no inputs */); 
      ret = ((uint64_t)lo) | (((uint64_t)hi)<<32);
    #elif PLATFORM_ARCH_X86
      __asm__ __volatile__("rdtsc"
                           : "=A" (ret)
                           : /* no inputs */); 
    #elif PLATFORM_ARCH_IA64 && PLATFORM_COMPILER_INTEL
      ret = (uint64_t)__getReg(_IA64_REG_AR_ITC);
    #elif PLATFORM_ARCH_IA64
      __asm__ __volatile__("mov %0=ar.itc" 
                           : "=r"(ret) 
                           : /* no inputs */);
    #else
      #error "unsupported CPU"
    #endif
    return ret;
  } 
 #endif
  GASNETI_INLINE(gasneti_ticks_to_ns)
  uint64_t gasneti_ticks_to_ns(gasneti_tick_t st) {
    static int firstTime = 1;
    static double Tick = 0.0; /* inverse GHz */
    if_pf (firstTime) {
     #if PLATFORM_OS_CATAMOUNT /* lacks /proc filesystem */
        Tick = 1000.0 / __cpu_mhz;
     #else
      FILE *fp = fopen("/proc/cpuinfo","r");
      char input[255];
      if (!fp) gasneti_fatalerror("*** ERROR: Failure in fopen('/proc/cpuinfo','r')=%s",strerror(errno));
      while (!feof(fp) && fgets(input, 255, fp)) {
      #if PLATFORM_ARCH_IA64 /* itc and cpu need not run at the same rate */
        if (strstr(input,"itc MHz")) {
      #else
        if (strstr(input,"cpu MHz")) {
      #endif
          char *p = strchr(input,':');
	  double MHz = 0.0;
          if (p) MHz = atof(p+1);
          gasneti_assert(MHz > 1 && MHz < 100000); /* ensure it looks reasonable */
          Tick = 1000. / MHz;
          break;
        }
      }
      fclose(fp);
     #endif
      gasneti_assert(Tick != 0.0);
      gasneti_sync_writes();
      firstTime = 0;
    } else gasneti_sync_reads();
    return (uint64_t)(st * Tick);
  }
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_POWERPC && \
      ( PLATFORM_COMPILER_GNU || PLATFORM_COMPILER_XLC ) && \
      ( PLATFORM_OS_LINUX || PLATFORM_OS_BLRTS )
  /* Use the 64-bit "timebase" register on both 32- and 64-bit PowerPC CPUs */
  #include <sys/types.h>
  #include <dirent.h>
  typedef uint64_t gasneti_tick_t;
 #if PLATFORM_COMPILER_GNU
  GASNETI_INLINE(gasneti_ticks_now)
  uint64_t gasneti_ticks_now(void) {
    uint64_t ret;
    #if PLATFORM_ARCH_64
      __asm__ __volatile__("mftb %0"
                           : "=r" (ret)
                           : /* no inputs */); 
    #else
      /* Note we must read hi twice to protect against wrap of lo */
      uint32_t o_hi, hi, lo;
      __asm__ __volatile__("0: \n\t"
		           "mftbu %0\n\t"
			   "mftbl %1\n\t"
			   "mftbu %2\n\t"
			   "cmpw  %0, %2\n\t"
			   "bne- 0b\n\t"
                           : "=r" (o_hi), "=r" (lo), "=r" (hi)
                           : /* no inputs */); 
      ret = ((uint64_t)hi << 32) | lo;
    #endif
    return ret;
  } 
 #elif PLATFORM_COMPILER_XLC
   #if PLATFORM_ARCH_64
      static uint64_t gasneti_ticks_now(void);
      #pragma mc_func gasneti_ticks_now {  \
        "7c6c42e6"      /* mftb r3         */ \
        /* RETURN counter in r3 */            \
      }
      #pragma reg_killed_by gasneti_ticks_now 
   #else
      static uint32_t gasneti_mftb_low(void);
      #pragma mc_func gasneti_mftb_low {  \
        "7c6c42e6"      /* mftb r3     */ \
        /* RETURN counter in r3 */        \
      }
      #pragma reg_killed_by gasneti_mftb_low 
      
      static uint32_t gasneti_mftb_high(void);
      #pragma mc_func gasneti_mftb_high {  \
        "7c6d42e6"      /* mftbu r3     */ \
        /* RETURN counter in r3 */         \
      }
      #pragma reg_killed_by gasneti_mftb_high 
      
      GASNETI_INLINE(gasneti_ticks_now)
      uint64_t gasneti_ticks_now(void) {
        register uint32_t hi, hi2, lo;
        /* Note we must read hi twice to protect against wrap of lo */
        do {
           hi = gasneti_mftb_high();
           lo = gasneti_mftb_low();        
           hi2 = gasneti_mftb_high();
        } while (hi != hi2);
        return ((uint64_t)hi << 32) | lo;
      } 
   #endif
 #endif
  GASNETI_INLINE(gasneti_ticks_to_ns)
  uint64_t gasneti_ticks_to_ns(gasneti_tick_t st) {
    static int firstTime = 1;
    static double Tick = 0.0;
    if_pf (firstTime) {
      uint32_t freq;
     #if PLATFORM_OS_BLRTS
      /* don't know how to query this, so hard-code it for now */
      freq = 700000000;
     #else 
      DIR *dp = opendir("/proc/device-tree/cpus");
      struct dirent *de = NULL;
      FILE *fp = NULL;
      double MHz = 0.0;
      char fname[128];
      if (!dp) gasneti_fatalerror("*** ERROR: Failure in opendir('/proc/device-tree/cpus'): %s",strerror(errno));
      do {
        de = readdir(dp);
	if (de && (de->d_name == strstr(de->d_name, "PowerPC,"))) {
	  break;
	}
      } while (de);
      if (!de) gasneti_fatalerror("*** ERROR: Failure to find a PowerPC CPU in /proc/device-tree/cpus");
      snprintf(fname, sizeof(fname), "/proc/device-tree/cpus/%s/timebase-frequency", de->d_name);
      closedir(dp);
      fp = fopen(fname, "r");
      if (!fp) gasneti_fatalerror("*** ERROR: Failure in fopen('%s','r'): %s\n",fname,strerror(errno));
      if (fread((void *)(&freq), sizeof(uint32_t), 1, fp) != 1) 
        gasneti_fatalerror("*** ERROR: Failure to read timebase frequency from '%s': %s", fname, strerror(errno));
      fclose(fp);
     #endif
      gasneti_assert(freq > 1000000 && freq < 1000000000); /* ensure it looks reasonable (1MHz to 1Ghz) */
      Tick = 1.0e9 / freq;
      gasneti_sync_writes();
      firstTime = 0;
    } else gasneti_sync_reads();
    return (uint64_t)(st * Tick);
  }
/* ------------------------------------------------------------------------------------ */
#elif 0 && PLATFORM_OS_TRU64
  /* the precision for this is no better than gettimeofday (~1 ms) */
  /* TODO: use elan real-time counter, or rpcc instruction (which returns
     a 32-bit cycle count that wraps too quickly to be useful by itself)
     luckily, the Quadrics NIC provides a nanosecond clock (with ~1us overhead)
   */
  #include <time.h>

  typedef uint64_t gasneti_tick_t;
  GASNETI_INLINE(gasneti_ticks_now)
  gasneti_tick_t gasneti_ticks_now() {
    struct timespec t;
    gasneti_assert_zeroret(clock_gettime(CLOCK_REALTIME, &t));
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define gasneti_ticks_to_ns(st)  (st)
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_OS_CYGWIN
  #include <windows.h>
  /* note: QueryPerformanceCounter is a Win32 system call and thus has ~1us overhead
     Most systems have a QueryPerformanceFrequency() == 3,579,545, which is the
     ACPI counter that should be reliable across CPU cycle speedstepping, etc.
     rdtsc has lower overhead, but only works on Pentium or later,
     produces wildly incorrect results if the  CPU decides to change clock rate 
     mid-run (and there's no reliable way to get the correct cycle multiplier 
     short of timing a known-length delay and hoping for the best)
     See http://www.geisswerks.com/ryan/FAQS/timing.html
         http://softwareforums.intel.com/ids/board/message?board.id=16&message.id=1509
  */
  typedef uint64_t gasneti_tick_t;
  GASNETI_INLINE(gasneti_ticks_now)
  gasneti_tick_t gasneti_ticks_now() {
    LARGE_INTEGER val;
    gasneti_assert_nzeroret(QueryPerformanceCounter(&val));
    gasneti_assert(val.QuadPart > 0);
    return (gasneti_tick_t)val.QuadPart;
  }
  GASNETI_INLINE(gasneti_ticks_to_ns)
  uint64_t gasneti_ticks_to_ns(gasneti_tick_t st) {
    static int firsttime = 1;
    static double freq = 0;
    if_pf (firsttime) {
      LARGE_INTEGER temp;
      gasneti_assert_nzeroret(QueryPerformanceFrequency(&temp));
      freq = ((double)temp.QuadPart) / 1.0E9;
      freq = 1 / freq;
      gasneti_sync_writes();
      firsttime = 0;
    } else gasneti_sync_reads();
    return (uint64_t)(st * freq);
  }
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_OS_DARWIN
  /* See http://developer.apple.com/qa/qa2004/qa1398.html */
  #include <mach/mach_time.h>
  typedef uint64_t gasneti_tick_t;
  #define gasneti_ticks_now() mach_absolute_time()
  GASNETI_INLINE(gasneti_ticks_to_ns)
  uint64_t gasneti_ticks_to_ns(gasneti_tick_t st) {
    static int firsttime = 1;
    static double freq = 0;
    if_pf (firsttime) {
      mach_timebase_info_data_t tb;
      gasneti_assert_zeroret(mach_timebase_info(&tb));
      freq = ((double)tb.numer) / ((double)tb.denom);
      gasneti_sync_writes();
      firsttime = 0;
    } else gasneti_sync_reads();
    return (uint64_t)(st * freq);
  }
/* ------------------------------------------------------------------------------------ */
#elif defined(_POSIX_TIMERS) && 0
  /* POSIX realtime support - disabled for now because haven't found anywhere that it 
     outperforms gettimeofday, and it usually requires an additional library */
  #define GASNETI_FORCE_POSIX_REALTIME 1
/* ------------------------------------------------------------------------------------ */
#else /* use slow, portable timers */
  #define GASNETI_FORCE_GETTIMEOFDAY 1
#endif
/* ------------------------------------------------------------------------------------ */
/* completely portable (low-performance) microsecond granularity wall-clock time */
extern uint64_t gasneti_gettimeofday_us(void);

/* portable implementations */
#if defined(GASNETI_FORCE_GETTIMEOFDAY)
  #define GASNETI_USING_GETTIMEOFDAY 1
  /* portable microsecond granularity wall-clock timer */
  typedef uint64_t _gasneti_tick_t;
  #undef gasneti_tick_t
  #define gasneti_tick_t _gasneti_tick_t
  #undef gasneti_ticks_to_ns
  #define gasneti_ticks_to_ns(st)  (((gasneti_tick_t)(st))*1000)
  #undef gasneti_ticks_now
  #define gasneti_ticks_now()      ((gasneti_tick_t)gasneti_gettimeofday_us())
#elif defined(GASNETI_FORCE_POSIX_REALTIME)
  #include <time.h>
  #define GASNETI_USING_POSIX_REALTIME 1
  typedef uint64_t _gasneti_tick_t;
  #undef gasneti_tick_t
  #define gasneti_tick_t _gasneti_tick_t
  GASNETI_INLINE(gasneti_ticks_now_posixrt)
  gasneti_tick_t gasneti_ticks_now_posixrt() {
    struct timespec tm;
    #if defined(_POSIX_MONOTONIC_CLOCK) && 0 
      /* this is probably the better timer to use, but 
         some implementations define the symbol and then fail at runtime */
      gasneti_assert_zeroret(clock_gettime(CLOCK_MONOTONIC,&tm));
    #else
      gasneti_assert_zeroret(clock_gettime(CLOCK_REALTIME,&tm));
    #endif
    return tm.tv_sec*((uint64_t)1E9)+tm.tv_nsec;
  }
  #undef gasneti_ticks_now
  #define gasneti_ticks_now() gasneti_ticks_now_posixrt()
  #undef gasneti_ticks_to_ns
  #define gasneti_ticks_to_ns(st)  (st)
#endif

#if defined(GASNETI_USING_SLOW_TIMERS) || defined(GASNETI_TICKS_NOW_BODY)
  GASNETI_EXTERNC void gasneti_slow_ticks_now(void);
  #define gasneti_ticks_now()    ((*(gasneti_tick_t (*)(void))(&gasneti_slow_ticks_now))())
#endif

#ifndef GASNETI_TICK_MIN
#define GASNETI_TICK_MIN        ((gasneti_tick_t)0)
#endif
#ifndef GASNETI_TICK_MAX
#define GASNETI_TICK_MAX        ((gasneti_tick_t)-1)
#endif

#if GASNETI_USING_GETTIMEOFDAY
  #define GASNETI_TIMER_CONFIG   timers_os
#elif GASNETI_USING_POSIX_REALTIME
  #define GASNETI_TIMER_CONFIG   timers_posixrt
#else
  #define GASNETI_TIMER_CONFIG   timers_native
#endif

/* return a double value representing the approximate microsecond
   overhead and granularity of the current timers. Overhead is the 
   average wall-clock time consumed while reading the timer value once,
   and granularity is the minimum observable non-zero interval between 
   two timer readings (which may be limited only by overhead, or may 
   be significantly higher on systems where the underlying timer 
   advances in discrete "jumps" of time much larger than the overhead)
   When measuring an event of length (L) using two surrounding timer calls,
   the measured time interval will be: L + overhead +- granularity
*/
extern double gasneti_tick_metric(int idx);
#define gasneti_tick_granularity() gasneti_tick_metric(0)
#define gasneti_tick_overhead()    gasneti_tick_metric(1)
/* ------------------------------------------------------------------------------------ */

GASNETI_END_EXTERNC

#endif
