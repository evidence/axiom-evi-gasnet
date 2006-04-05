/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_timer.h,v $
 *     $Date: 2006/04/05 23:27:24 $
 * $Revision: 1.52 $
 * Description: GASNet Timer library (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_TIMER_H
#define _GASNET_TIMER_H

/* all of this to support gasneti_getMicrosecondTimeStamp */
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

GASNETI_BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */
/* High-performance system timer library 

  Implements high-granularity, low-overhead timers using system-specific support, where available

  Interface:
    gasneti_stattime_t - timer datatype representing an integer number of "ticks"
      where a "tick" has a system-specific interpretation
      safe to be handled using integer operations (+,-,<,>,==)
    GASNETI_STATTIME_NOW() - returns the current tick count as a gasneti_stattime_t
    GASNETI_STATTIME_TO_NS(stattime) - convert ticks to nanoseconds as a uint64_t
    GASNETI_STATTIME_MIN - a value representing the minimum value storable in a gasneti_stattime_t
    GASNETI_STATTIME_MAX - a value representing the maximum value storable in a gasneti_stattime_t
*/

/* completely portable (low-performance) microsecond granularity wall-clock timer */
GASNETI_INLINE(gasneti_getMicrosecondTimeStamp)
int64_t gasneti_getMicrosecondTimeStamp(void) {
  int64_t retval;
  struct timeval tv;
  #ifdef __crayx1
  retry:
  #endif
  if (gettimeofday(&tv, NULL)) {
      perror("gettimeofday");
      abort();
  }
  retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
  #ifdef __crayx1
    /* fix an empirically observed bug in UNICOS gettimeofday(),
       which occasionally returns ridiculously incorrect values
       SPR 728120, fixed in kernel 2.4.34 
     */
    if_pf(retval < (((int64_t)3) << 48)) goto retry;
  #endif
  return retval;
}

#if defined(GASNETC_CONDUIT_SPECIFIC_TIMERS)
  #if !defined(GASNETI_STATTIME_TO_NS) || !defined(GASNETI_STATTIME_NOW)
    #error Incomplete conduit-specific timer impl.
  #endif
#elif defined(AIX)
  #include <sys/time.h>
  #include <sys/systemcfg.h>

  /* we want to avoid expensive divide and conversion operations during collection, 
     but timebasestruct_t structs are too difficult to perform arithmetic on
     we stuff the internal cycle counter into a 64-bit holder and expand to realtime later */
  typedef uint64_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    timebasestruct_t t;
    read_real_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) << 32) | ((uint64_t)t.tb_low);
  }
  GASNETI_INLINE(gasneti_stattime_to_ns)
  uint64_t gasneti_stattime_to_ns(gasneti_stattime_t st) {
    timebasestruct_t t;
    gasneti_assert((read_real_time(&t,TIMEBASE_SZ), 
                   t.flag == RTC_POWER_PC)); /* otherwise timer arithmetic (min/max/sum) is compromised */
    t.flag = RTC_POWER_PC;
    t.tb_high = (uint32_t)(st >> 32);
    t.tb_low =  (uint32_t)(st);
    time_base_to_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) * 1000000000) + t.tb_low;
  }
  #define GASNETI_STATTIME_TO_NS(st)  (gasneti_stattime_to_ns(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(CRAYT3E) || defined(CRAYX1)
  #if defined(CRAYT3E) 
    #include <sys/machinfo.h>
    /* 75 Mhz sys. clock, according to docs */
    #define GASNETI_UNICOS_SYS_CLOCK 75 
  #elif defined(CRAYX1)
    #include <intrinsics.h>
    /* 100 Mhz sys. clock, according to Fortran IRTC_RATE() */
    #define GASNETI_UNICOS_SYS_CLOCK 100
  #endif
  #ifdef __GNUC__
    #define _rtc rtclock
  #else
    long    _rtc();
  #endif

  typedef uint64_t gasneti_stattime_t;

  #if 0
    #define GASNETI_STATTIME_TO_NS(st)  ((st) * 1000000000 / GetMachineInfo(mi_hz))
  #else
    #if GASNETI_UNICOS_SYS_CLOCK == 100
      /* 100 Mhz sys. clock */
      #define GASNETI_STATTIME_TO_NS(st)  ((st) * 10)
    #else
      #define GASNETI_STATTIME_TO_NS(st)  ((gasneti_stattime_t)((st) * (1000.0 / GASNETI_UNICOS_SYS_CLOCK)))
    #endif
  #endif
  #define GASNETI_STATTIME_NOW()      (_rtc())
#elif defined(IRIX)
  #include <time.h>
  #include <sys/ptimers.h>

  typedef uint64_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    struct timespec t;
    if_pf (clock_gettime(CLOCK_SGI_CYCLE, &t) == -1) abort();
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define GASNETI_STATTIME_TO_NS(st)  (st)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(__MTA__)
  #include <sys/mta_task.h>
  #include <machine/mtaops.h>

  typedef int64_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_to_ns)
  uint64_t gasneti_stattime_to_ns(gasneti_stattime_t ticks) {
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
  #define GASNETI_STATTIME_TO_NS(st)  (gasneti_stattime_to_ns(st))
  #define GASNETI_STATTIME_NOW()      (MTA_CLOCK(0))
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)(((uint64_t)-1)>>1))
#elif defined(SOLARIS)
#if 1
  /* workaround bizarre failures on gcc 3.2.1 - seems they sometimes use a
     union to implement longlong_t and hence hrtime_t, and the test to
     determine this is (__STDC__ - 0 == 0) which is totally bogus */
  typedef uint64_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    hrtime_t t = gethrtime();
    return *(gasneti_stattime_t *)&t;
  }
  #define GASNETI_STATTIME_TO_NS(st)  (st)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#else
  typedef hrtime_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_to_ns)
  uint64_t gasneti_stattime_to_ns(gasneti_stattime_t st) {
    gasneti_assert(sizeof(gasneti_stattime_t) == 8);
    return *(uint64_t*)&st;
  }
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)(((uint64_t)-1)>>1))
  #define GASNETI_STATTIME_TO_NS(st)  (gasneti_stattime_to_ns(st))
  #define GASNETI_STATTIME_NOW()      (gethrtime())
#endif
#elif defined(__LIBCATAMOUNT__) && defined(__PGI) && !defined(PGI_WITH_REAL_ASM) && 0 /* DISABLED */
  #include <catamount/dclock.h>
  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_TO_NS(st)  (st)
  #define GASNETI_STATTIME_NOW()      ((gasneti_stattime_t)(dclock()*1E9))
#elif defined(__linux__) && \
     (defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__PGI)) && \
     (defined(__i386__) || defined(__x86_64__) || defined(__ia64__))
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <math.h>
  #if defined(__ia64__) && defined(__INTEL_COMPILER)
    #include <ia64intrin.h>
  #endif
  #if defined(__LIBCATAMOUNT__)
    extern unsigned int __cpu_mhz; /* system provided */
  #endif
  typedef uint64_t gasneti_stattime_t;
 #if defined(__PGI) && !defined(PGI_WITH_REAL_ASM)
   #define GASNETI_USING_SLOW_TIMERS_SPECIAL 1
   #if defined(__i386__)
     #define GASNETI_STATTIME_NOW_BODY GASNETI_ASM("rdtsc");
   #elif defined (__x86_64__)
     #define GASNETI_STATTIME_NOW_BODY	\
		GASNETI_ASM( "xor %rax, %rax	\n\t" \
			     "rdtsc		\n\t" \
			     "shl $32, %rdx	\n\t" \
			     "or %rdx, %rax" );
   #elif defined (__ia64__)
     /* For completeness. */
     #define GASNETI_STATTIME_NOW_BODY	\
		GASNETI_ASM( "mov.m r8=ar.itc;" );
   #endif
 #elif defined(PGI_WITH_REAL_ASM) && defined(__cplusplus)
  #define GASNETI_USING_SLOW_TIMERS 1
 #else
  GASNETI_INLINE(gasneti_stattime_now)
  uint64_t gasneti_stattime_now (void) {
    uint64_t ret;
    #if defined(__i386__)
      __asm__ __volatile__("rdtsc"
                           : "=A" (ret)
                           : /* no inputs */); 
    #elif defined(__ia64__) && defined(__INTEL_COMPILER)
      ret = (uint64_t)__getReg(_IA64_REG_AR_ITC);
    #elif defined(__ia64__) 
      __asm__ __volatile__("mov %0=ar.itc" 
                           : "=r"(ret) 
                           : /* no inputs */);
    #elif defined(__x86_64__)
      uint32_t lo, hi;
      __asm__ __volatile__("rdtsc"
                           : "=a" (lo), "=d" (hi)
                           : /* no inputs */); 
      ret = ((uint64_t)lo) | (((uint64_t)hi)<<32);
    #else
      #error "unsupported CPU"
    #endif
    return ret;
  } 
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
 #endif
  GASNETI_INLINE(gasneti_stattime_to_ns)
  uint64_t gasneti_stattime_to_ns(gasneti_stattime_t st) {
    static int firstTime = 1;
    static double Tick = 0.0; /* inverse GHz */
    if_pf (firstTime) {
     #if defined(__LIBCATAMOUNT__) /* lacks /proc filesystem */
        Tick = 1000.0 / __cpu_mhz;
     #else
      FILE *fp = fopen("/proc/cpuinfo","r");
      char input[255];
      if (!fp) {
        fprintf(stderr,"*** ERROR: Failure in fopen('/proc/cpuinfo','r')=%s",strerror(errno));
        abort();
      }
      while (!feof(fp) && fgets(input, 255, fp)) {
      #if defined(__ia64__) /* itc and cpu need not run at the same rate */
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
  #define GASNETI_STATTIME_TO_NS(st)  (gasneti_stattime_to_ns(st))
#elif defined(__PPC__) && \
      ( defined(__GNUC__) || defined(__xlC__) ) && \
      ( defined(__linux__) || defined(__blrts__) )
  /* 
   * This code uses the 64-bit "timebase" register on both 32- and 64-bit PowerPC CPUs.
   */
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <math.h>
  #include <sys/types.h>
  #include <dirent.h>
  typedef uint64_t gasneti_stattime_t;
 #ifdef __GNUC__
  GASNETI_INLINE(gasneti_stattime_now)
  uint64_t gasneti_stattime_now (void) {
    uint64_t ret;
    #if defined(__PPC64__)
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
 #elif defined(__xlC__)
   #if defined(__PPC64__)
      static uint64_t gasneti_stattime_now (void);
      #pragma mc_func gasneti_stattime_now {  \
        "7c6c42e6"      /* mftb r3         */ \
        /* RETURN counter in r3 */            \
      }
      #pragma reg_killed_by gasneti_stattime_now 
   #else
      static uint32_t gasneti_mftb_low (void);
      #pragma mc_func gasneti_mftb_low {  \
        "7c6c42e6"      /* mftb r3     */ \
        /* RETURN counter in r3 */        \
      }
      #pragma reg_killed_by gasneti_mftb_low 
      
      static uint32_t gasneti_mftb_high (void);
      #pragma mc_func gasneti_mftb_high {  \
        "7c6d42e6"      /* mftbu r3     */ \
        /* RETURN counter in r3 */         \
      }
      #pragma reg_killed_by gasneti_mftb_high 
      
      GASNETI_INLINE(gasneti_stattime_now)
      uint64_t gasneti_stattime_now (void) {
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
  GASNETI_INLINE(gasneti_stattime_to_ns)
  uint64_t gasneti_stattime_to_ns(gasneti_stattime_t st) {
    static int firstTime = 1;
    static double Tick = 0.0;
    if_pf (firstTime) {
      uint32_t freq;
     #ifdef __blrts__
      /* don't know how to query this, so hard-code it for now */
      freq = 700000000;
     #else 
      DIR *dp = opendir("/proc/device-tree/cpus");
      struct dirent *de = NULL;
      FILE *fp = NULL;
      double MHz = 0.0;
      char fname[128];
      if (!dp) {
        fprintf(stderr,"*** ERROR: Failure in opendir('/proc/device-tree/cpus'): %s\n",strerror(errno));
        abort();
      }
      do {
        de = readdir(dp);
	if (de && (de->d_name == strstr(de->d_name, "PowerPC,"))) {
	  break;
	}
      } while (de);
      if (!de) {
        fprintf(stderr,"*** ERROR: Failure to find a PowerPC CPU in /proc/device-tree/cpus\n");
	abort();
      }
      snprintf(fname, sizeof(fname), "/proc/device-tree/cpus/%s/timebase-frequency", de->d_name);
      closedir(dp);
      fp = fopen(fname, "r");
      if (!fp) {
	fprintf(stderr,"*** ERROR: Failure in fopen('%s','r'): %s\n",fname,strerror(errno));
	abort();
      }
      if (fread((void *)(&freq), sizeof(uint32_t), 1, fp) != 1) {
        fprintf(stderr,"*** ERROR: Failure to read timebase frequency from '%s': %s\n", fname,strerror(errno));
	abort();
      }
      fclose(fp);
     #endif
      gasneti_assert(freq > 1000000 && freq < 1000000000); /* ensure it looks reasonable (1MHz to 1Ghz) */
      Tick = 1.0e9 / freq;
      gasneti_sync_writes();
      firstTime = 0;
    } else gasneti_sync_reads();
    return (uint64_t)(st * Tick);
  }
  #define GASNETI_STATTIME_TO_NS(st)  (gasneti_stattime_to_ns(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif 0 && defined(OSF)
  /* the precision for this is no better than gettimeofday (~1 ms) */
  /* TODO: use elan real-time counter, or rpcc instruction (which returns
     a 32-bit cycle count that wraps too quickly to be useful by itself)
     luckily, the Quadrics NIC provides a nanosecond clock (with ~1us overhead)
   */
  #include <time.h>

  typedef uint64_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t) == -1) abort();
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define GASNETI_STATTIME_TO_NS(st)  (st)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(CYGWIN)
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
  typedef uint64_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    LARGE_INTEGER val;
    if_pf (!QueryPerformanceCounter(&val)) abort();
    gasneti_assert(val.QuadPart > 0);
    return (gasneti_stattime_t)val.QuadPart;
  }
  GASNETI_INLINE(gasneti_stattime_to_ns)
  uint64_t gasneti_stattime_to_ns(gasneti_stattime_t st) {
    static int firsttime = 1;
    static double freq = 0;
    if_pf (firsttime) {
      LARGE_INTEGER temp;
      if (!QueryPerformanceFrequency(&temp)) abort();
      freq = ((double)temp.QuadPart) / 1.0E9;
      freq = 1 / freq;
      gasneti_sync_writes();
      firsttime = 0;
    } else gasneti_sync_reads();
    return (uint64_t)(st * freq);
  }
  #define GASNETI_STATTIME_TO_NS(st)  (gasneti_stattime_to_ns(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(__APPLE__) && defined(__MACH__)
  /* See http://developer.apple.com/qa/qa2004/qa1398.html */
  #include <mach/mach_time.h>
  typedef uint64_t gasneti_stattime_t;
  #define gasneti_stattime_now() mach_absolute_time()
  GASNETI_INLINE(gasneti_stattime_to_ns)
  uint64_t gasneti_stattime_to_ns(gasneti_stattime_t st) {
    static int firsttime = 1;
    static double freq = 0;
    if_pf (firsttime) {
      mach_timebase_info_data_t tb;
      if (mach_timebase_info(&tb)) abort();
      freq = ((double)tb.numer) / ((double)tb.denom);
      gasneti_sync_writes();
      firsttime = 0;
    } else gasneti_sync_reads();
    return (uint64_t)(st * freq);
  }
  #define GASNETI_STATTIME_TO_NS(st)  (gasneti_stattime_to_ns(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(_POSIX_TIMERS) && 0
  /* POSIX realtime support - disabled for now because haven't found anywhere that it 
     outperforms gettimeofday, and it usually requires an additional library */
  #include <time.h>
  #define GASNETI_USING_POSIX_REALTIME 1
  typedef uint64_t gasneti_stattime_t;
  GASNETI_INLINE(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
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
  #define GASNETI_STATTIME_TO_NS(st)  (st)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#else
  #define GASNETI_USING_GETTIMEOFDAY 1
  /* portable microsecond granularity wall-clock timer */
  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_TO_NS(st)  ((st)*1000)
  #define GASNETI_STATTIME_NOW()      ((gasneti_stattime_t)gasneti_getMicrosecondTimeStamp())
#endif

#if defined(GASNETI_USING_SLOW_TIMERS) || defined(GASNETI_USING_SLOW_TIMERS_SPECIAL)
  GASNETI_EXTERNC void gasneti_slow_stattime_now(void);
  #define GASNETI_STATTIME_NOW()    ((*(gasneti_stattime_t (*)(void))(&gasneti_slow_stattime_now))())
#endif

#ifndef GASNETI_STATTIME_MIN
#define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
#endif
#ifndef GASNETI_STATTIME_MAX
#define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
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
#define GASNETI_STATTIME_GRANULARITY() gasneti_stattime_metric(0)
#define GASNETI_STATTIME_OVERHEAD()    gasneti_stattime_metric(1)
#if defined(_INCLUDED_GASNET_H) 
  extern double *_gasneti_stattime_metric;
#else
 #if !defined(__cplusplus)
  /* use a tentative definition, so all files can share the same metric
     data structures, and to ensure we pay the timing overhead at most once per run */
  extern double *_gasneti_stattime_metric;
  double *_gasneti_stattime_metric; 
 #else
  /* C++ outlaws tentative definitions, and we have no other place to 
     reliably place this data in gasnet_tools mode. 
     So we're forced to place it in each compilation unit and possibly
     pay one timing overhead for each compilation unit that asks
   */
  static double *_gasneti_stattime_metric; 
 #endif
#endif
GASNETI_INLINE(gasneti_stattime_metric)
double gasneti_stattime_metric(unsigned int idx) {
  gasneti_assert(idx <= 1);
  if_pf (_gasneti_stattime_metric == NULL) {
    int i, ticks, iters = 1000, minticks = 10;
    double *_tmp_metric;
    gasneti_stattime_t min = GASNETI_STATTIME_MAX;
    gasneti_stattime_t start = GASNETI_STATTIME_NOW();
    gasneti_stattime_t last = start;
    for (i=0,ticks=0; i < iters || ticks < minticks; i++) {
      gasneti_stattime_t x = GASNETI_STATTIME_NOW();
      gasneti_stattime_t curr = (x - last);
      if_pt (curr > 0) { 
        ticks++;
        if_pf (curr < min) min = curr;
      }
      last = x;
    }
    _tmp_metric = (double *)malloc(2*sizeof(double));
    gasneti_assert(_tmp_metric != NULL);
    /* granularity */
    _tmp_metric[0] = ((double)GASNETI_STATTIME_TO_NS(min))/1000.0;
    /* overhead */
    _tmp_metric[1] = ((double)(GASNETI_STATTIME_TO_NS(last - start)))/(i*1000.0);
    gasneti_sync_writes();
    _gasneti_stattime_metric = _tmp_metric;
  } else gasneti_sync_reads();
  return _gasneti_stattime_metric[idx];
}
/* ------------------------------------------------------------------------------------ */

GASNETI_END_EXTERNC

#endif
