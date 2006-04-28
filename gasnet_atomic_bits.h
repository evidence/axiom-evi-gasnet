/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_atomic_bits.h,v $
 *     $Date: 2006/04/28 00:17:07 $
 * $Revision: 1.167 $
 * Description: GASNet header for platform-specific parts of atomic operations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNET_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_ATOMIC_BITS_H
#define _GASNET_ATOMIC_BITS_H

/* ------------------------------------------------------------------------------------ */
/* Identify special cases lacking native support */

#if defined(GASNETI_FORCE_GENERIC_ATOMICOPS) || /* for debugging */          \
    defined(CRAYT3E)   || /* T3E seems to have no atomic ops */              \
    defined(_SX)       || /* NEC SX-6 atomics not available to user code? */ \
    defined(__MICROBLAZE__) /* no atomic instructions */
  #define GASNETI_USE_GENERIC_ATOMICOPS
#elif defined(GASNETI_FORCE_OS_ATOMICOPS) || /* for debugging */          \
    defined(MTA)   ||  \
    defined(_SGI_COMPILER_VERSION)
  #define GASNETI_USE_OS_ATOMICOPS
#else
#endif

#if defined(GASNETI_USE_GENERIC_ATOMICOPS)
  #define GASNETI_ATOMIC_CONFIG   atomics_mutex
#elif defined(GASNETI_USE_OS_ATOMICOPS)
  #define GASNETI_ATOMIC_CONFIG   atomics_os
#else
  #define GASNETI_ATOMIC_CONFIG   atomics_native
#endif

/* ------------------------------------------------------------------------------------ */
/* Yuck */
#if defined(__x86_64__) || /* x86 and Athlon/Opteron */ \
    defined(__i386__) || defined(__i386) || defined(i386) || \
    defined(__i486__) || defined(__i486) || defined(i486) || \
    defined(__i586__) || defined(__i586) || defined(i586) || \
    defined(__i686__) || defined(__i686) || defined(i686)
  #ifdef GASNETI_UNI_BUILD
    #define GASNETI_X86_LOCK_PREFIX ""
  #else
    #define GASNETI_X86_LOCK_PREFIX "lock\n\t"
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Helpers for "special" call-based atomics on platforms w/ crippled inline asm support. */

#define GASNETI_SPECIAL_ASM_DECL(name) \
	GASNETI_EXTERNC void name(void)
#define GASNETI_SPECIAL_ASM_DEFN(name, body) \
	GASNETI_NEVER_INLINE(name, extern void name(void)) { body; }

/* ------------------------------------------------------------------------------------ */

#if defined(GASNETI_USE_GENERIC_ATOMICOPS)
  /* a very slow but portable implementation of atomic ops using mutexes */
  #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1
  #ifdef _INCLUDED_GASNET_H
    extern void *gasneti_patomicop_lock; /* bug 693: avoid header dependency cycle */
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define _gasneti_atomic_read(p)      ((p)->ctr)
    #define _gasneti_atomic_init(v)      { (v) }
    #define _gasneti_atomic_set(p,v) do {                         \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        (p)->ctr = (v);                                           \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    #define _gasneti_atomic_increment(p) do {                     \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        ((p)->ctr)++;                                             \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    #define _gasneti_atomic_decrement(p) do {                     \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        ((p)->ctr)--;                                             \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    extern int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *p);
    #define GASNETI_GENERIC_DEC_AND_TEST_DEF                      \
    int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *p) { \
      uint32_t newval;                                            \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);     \
      newval = p->ctr - 1;                                        \
      p->ctr = newval;                                            \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
      return (newval == 0);                                       \
    }
    #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_32

    extern int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval);
    #define GASNETI_GENERIC_CAS_DEF                              \
    int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p,    \
                                        uint32_t oldval,         \
                                        uint32_t newval) {       \
      int retval;                                                \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);    \
      retval = (p->ctr == oldval);                               \
      if_pt (retval) {                                           \
        p->ctr = newval;                                         \
      }                                                          \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);  \
      return retval;                                             \
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1

    extern uint32_t gasneti_atomic_addfetch_32(gasneti_atomic_t *p, int32_t op);
    #define GASNETI_GENERIC_ADD_SUB_DEF                            \
    extern uint32_t gasneti_atomic_addfetch_32(gasneti_atomic_t *p,\
                                               int32_t op) {       \
      uint32_t retval;                                             \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);      \
      retval = (((p)->ctr) += (op));                               \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);    \
      return retval;                                               \
    }
    #define _gasneti_atomic_addfetch gasneti_atomic_addfetch_32

    #if (GASNET_PAR || GASNETI_CONDUIT_THREADS)
      /* Using real HSLs which yeild an ACQ/RMB before and REL/WMB after the atomic */
      #define GASNETI_ATOMIC_FENCE_SET (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
    #else
      /* HSLs compile away, so use defaults */
    #endif
  #elif defined(_REENTRANT) || defined(_THREAD_SAFE) || \
        defined(PTHREAD_MUTEX_INITIALIZER) ||           \
        defined(HAVE_PTHREAD) || defined(HAVE_PTHREAD_H)
    /* a version for pthreads which is independent of GASNet HSL's */
    #include <pthread.h>
    extern pthread_mutex_t gasneti_atomicop_mutex; 
    /* intentionally make these a different size than regular 
       GASNet atomics, to cause a link error on attempts to mix them
     */
    typedef struct { volatile uint32_t ctr; char _pad; } gasneti_atomic_t;
    #define _gasneti_atomic_read(p)      ((p)->ctr)
    #define _gasneti_atomic_init(v)      { (v) }
    #define _gasneti_atomic_set(p,v) do {              \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        (p)->ctr = (v);                                \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define _gasneti_atomic_increment(p) do {          \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)++;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define _gasneti_atomic_decrement(p) do {          \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)--;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    GASNETI_INLINE(_gasneti_atomic_decrement_and_test_32)
    int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *p) {
      uint32_t newval;
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      newval = p->ctr - 1;
      p->ctr = newval;
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return (newval == 0);
    }
    #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_32

    GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
    int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, 
                           uint32_t oldval, uint32_t newval) {
      int retval;
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      retval = (p->ctr == oldval);
      if_pt (retval) {
        p->ctr = newval;
      }
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return retval;
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1

    GASNETI_INLINE(gasneti_atomic_addfetch_32)
    uint32_t gasneti_atomic_addfetch_32(gasneti_atomic_t *p, int32_t op) {
      uint32_t retval;
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      retval = (((p)->ctr) += op);
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return retval;
    }
    #define _gasneti_atomic_addfetch gasneti_atomic_addfetch_32

    #if (defined(__APPLE__) && defined(__MACH__))
      /* OSX/Darwin tries to be too smart when only 1 thread is running, so use defaults */
      /* XXX: determine what fence (if any) might still be present? */
    #else
      /* Using real mutexes which yeild an ACQ/RMB before and REL/WMB after the atomic */
      #define GASNETI_ATOMIC_FENCE_SET (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
    #endif
  #else
    /* only one thread - everything atomic by definition */
    /* attempt to generate a compile error if pthreads actually are in use */
    #define PTHREAD_MUTEX_INITIALIZER ERROR_include_pthread_h_before_gasnet_tools_h
    extern int pthread_mutex_lock; 

    typedef volatile uint32_t gasneti_atomic_t;
    #define _gasneti_atomic_read(p)      (*(p))
    #define _gasneti_atomic_init(v)      (v)
    #define _gasneti_atomic_set(p,v)     (*(p) = (v))
    #define _gasneti_atomic_increment(p) ((*(p))++)
    #define _gasneti_atomic_decrement(p) ((*(p))--)
    #define _gasneti_atomic_decrement_and_test(p) ((--(*(p))) == 0)

    #define _gasneti_atomic_compare_and_swap(p,oldval,newval) \
              (*(p) == (oldval) ? *(p) = (newval), 1 : 0)
    #define GASNETI_HAVE_ATOMIC_CAS 1

    #define _gasneti_atomic_addfetch(p,op)      ((*(p))+=(op))

    /* Using default fences */
  #endif
#elif defined(GASNETI_USE_OS_ATOMICOPS)
  /* ------------------------------------------------------------------------------------
   * Use OS-provided atomics, which should be CPU-independent and
   * which should work regardless of the compiler's inline assembly support.
   * ------------------------------------------------------------------------------------ */
  #if defined(AIX)
      #include <sys/atomic_op.h>
      typedef struct { volatile unsigned int ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }

      /* Default impls of inc, dec, dec-and-test, add and sub */
      #define _gasneti_atomic_fetchadd(p,op) fetch_and_add((atomic_p)&((p)->ctr), op)

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
        return compare_and_swap( (atomic_p)p, &oldval, newval );
      } 
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* No syncs in these calls, so use default fences */
  #elif defined(IRIX)
      #include <mutex.h>
      #include <ulocks.h>
      typedef __uint32_t gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      (*(volatile __uint32_t *)(p))
      #define _gasneti_atomic_set(p,v)     (*(volatile __uint32_t *)(p) = (v))
      #define _gasneti_atomic_init(v)      (v)

      /* Default impls of inc, dec, dec-and-test, add and sub */
      #define _gasneti_atomic_addfetch(p,op) (add_then_test32((p),(uint32_t)(op))) 

      #if defined(_SGI_COMPILER_VERSION)
        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
            return __compare_and_swap( p, oldval, newval ); /* bug1534: compiler built-in */
        }
      #elif defined(__GNUC__)
        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
           uint32_t temp;
           int retval;
           __asm__ __volatile__ (
                "1:\n\t"
                "ll        %1,%5\n\t"          /* Load from *p */
                "move      %0,$0\n\t"          /* Assume mismatch */
                "bne       %1,%3,2f\n\t"       /* Break loop on mismatch */
                "move      %0,%4\n\t"          /* Move newval to retval */
                "sc        %0,%2\n\t"          /* Try SC to store retval */
                "beqz      %0,1b\n"            /* Retry on contention */
                "2:\n\t"
                : "=&r" (retval), "=&r" (temp), "=m" (*p)
                : "r" (oldval), "r" (newval), "m" (*p) );
          return retval;
        }
      #else /* flaky OS-provided CAS */
        usptr_t * volatile _gasneti_usmem_ptr;
        gasneti_atomic_t _gasneti_usmem_ptr_init;
        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
          if_pf (!_gasneti_usmem_ptr) { /* need exactly one call to usinit on first invocation */
            if (test_and_set32(&_gasneti_usmem_ptr_init,1) == 0) {
              _gasneti_usmem_ptr = usinit("/dev/zero");
            } else while (!_gasneti_usmem_ptr);
          }
          return uscas32( p, oldval, newval, _gasneti_usmem_ptr ); /* from libc */
        }
      #endif
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* Using default fences - the docs claim acquire or release "barriers" for the various 
         intrinsics, but those are only compiler fences and not architectural sync instructions */
  #elif defined(__MTA__)
      /* use MTA intrinsics */
      typedef int64_t gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      ((int64_t)*(volatile int64_t*)(p))
      #define _gasneti_atomic_set(p,v)     ((*(volatile int64_t*)(p)) = (v))
      #define _gasneti_atomic_init(v)      (v)

      /* Default impls of inc, dec, dec-and-test, add and sub */
      #define _gasneti_atomic_fetchadd int_fetch_add

      /* Using default fences, but this machine is Sequential Consistent anyway */
  #elif defined(SOLARIS)	/* BROKEN (and incomplete) */
      /* $%*(! Solaris has atomic functions in the kernel but refuses to expose them
         to the user... after all, what application would be interested in performance? */
      #include <sys/atomic.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (atomic_add_32((uint32_t *)&((p)->ctr),1))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
  #elif defined(CYGWIN)
      /* These are *NOT* Cywgin calls, but Windows API calls that may actually
       * be intrinsics in the MS compilers on 64-bit systems. */
      #include <windows.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) InterlockedIncrement((LONG *)&((p)->ctr))
      #define _gasneti_atomic_decrement(p) InterlockedDecrement((LONG *)&((p)->ctr))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) \
                                          (InterlockedDecrement((LONG *)&((p)->ctr)) == 0)

      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
	   (InterlockedCompareExchange((LONG *)&((p)->ctr),nval,oval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1

      #define _gasneti_atomic_fetchadd(p, op) InterlockedExchangeAdd((LONG *)&((p)->ctr), op)

      /* MSDN docs ensure memory fence in these calls, even on ia64 */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)
  #elif defined(__linux__) 
      /* ------------------------------------------------------------------------------------
       * Linux provides an asm/atomic.h that is sometimes just useless
       * and other times supplies all but compare-and-swap (even when
       * it is implemented).  So, this code is probably only useful when
       * we encounter a new Linux platform.
       * ------------------------------------------------------------------------------------ */
      /* Disable using this code if this is a gasnet-smp build and the 
         linux/config.h settings disagree (due to system config problem or 
         cross-compiling on a uniprocessor frontend for smp nodes) */
      #include <linux/config.h>
      #if !(defined(CONFIG_SMP) || defined(GASNETI_UNI_BUILD))
        #error Building against a uniprocessor kernel.  Configure with --disable-smp-safe (for uniprocessor compute nodes), or build on an SMP host.
      #endif
      #ifdef __alpha__
        /* work-around for a puzzling header bug in alpha Linux */
        #define extern static
      #endif
      #ifdef __cplusplus
        /* work around a really stupid C++ header bug observed in HP Linux */
        #define new new_
      #endif
      #include <asm/bitops.h>
      #include <asm/system.h>
      #include <asm/atomic.h>
      #ifdef __alpha__
        #undef extern
      #endif
      #ifdef __cplusplus
        #undef new
      #endif
      typedef atomic_t gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) atomic_inc(p)
      #define _gasneti_atomic_decrement(p) atomic_dec(p)
      #define _gasneti_atomic_read(p)      atomic_read(p)
      #define _gasneti_atomic_set(p,v)     atomic_set(p,v)
      #define _gasneti_atomic_init(v)      ATOMIC_INIT(v)
      #define _gasneti_atomic_decrement_and_test(p) \
                                          atomic_dec_and_test(p)
      #ifdef cmpxchg
        /* we must violate the Linux atomic_t abstraction below and pass
           cmpxchg a pointer to the struct field, otherwise cmpxchg will
           stupidly attempt to cast its result to a struct type and fail
         */
        #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
             (cmpxchg(&((p)->counter),oval,nval) == (oval))
        #define GASNETI_HAVE_ATOMIC_CAS 1
      #endif

      /* Using default fences as we can't hope to know what to expect on new platforms */
  #else
    #error GASNETI_USE_OS_ATOMICS defined on unsupported OS - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
  #endif
#else
  /* ------------------------------------------------------------------------------------
   * Not using GENERIC (mutex) or OS-provided atomics, so provide our own based on the
   * CPU and compiler support for inline assembly code
   * ------------------------------------------------------------------------------------ */
  #if defined(__x86_64__) || defined(__amd64) || /* x86 and Athlon/Opteron */ \
      defined(__i386__) || defined(__i386) || defined(i386) || \
      defined(__i486__) || defined(__i486) || defined(i486) || \
      defined(__i586__) || defined(__i586) || defined(i586) || \
      defined(__i686__) || defined(__i686) || defined(i686)
    #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__PATHCC__) || defined(PGI_WITH_REAL_ASM)
     typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
     #define _gasneti_atomic_init(v)      { (v) }
     #if defined(PGI_WITH_REAL_ASM) && defined(__cplusplus) /* PGI C++ lacks inline assembly */
        #define GASNETI_HAVE_ATOMIC_CAS 1	/* Explicit */
        #define GASNETI_HAVE_ATOMIC_ADD_SUB 1	/* Derived */
        #define GASNETI_USING_SLOW_ATOMICS 1
     #else
      #if defined(__PATHCC__)
        /* Pathscale optimizer is buggy and fails to clobber memory output location correctly
           unless we include an extraneous full memory clobber 
         */
        #define GASNETI_ATOMIC_MEM_CLOBBER ,"memory"
      #else
        #define GASNETI_ATOMIC_MEM_CLOBBER
      #endif
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))

      GASNETI_INLINE(_gasneti_atomic_increment_32)
      void _gasneti_atomic_increment_32(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_X86_LOCK_PREFIX
		"incl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr)
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      #define _gasneti_atomic_increment _gasneti_atomic_increment_32
      GASNETI_INLINE(_gasneti_atomic_decrement_32)
      void _gasneti_atomic_decrement_32(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_X86_LOCK_PREFIX
		"decl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr) 
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      #define _gasneti_atomic_decrement _gasneti_atomic_decrement_32
      GASNETI_INLINE(_gasneti_atomic_decrement_and_test_32)
      int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *v) {
          register unsigned char retval;
          __asm__ __volatile__(
	          GASNETI_X86_LOCK_PREFIX
		  "decl %0		\n\t"
		  "sete %1"
	          : "=m" (v->ctr), "=mq" (retval)
	          : "m" (v->ctr) 
                  : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
          return retval;
      }
      #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_32

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *v, uint32_t oldval, uint32_t newval) {
        register unsigned char retval;
        register uint32_t readval;
        __asm__ __volatile__ (
		GASNETI_X86_LOCK_PREFIX
		"cmpxchgl %3, %1	\n\t"
		"sete %0"
		: "=mq" (retval), "=m" (v->ctr), "=a" (readval)
		: "r" (newval), "m" (v->ctr), "a" (oldval)
		: "cc" GASNETI_ATOMIC_MEM_CLOBBER);
        return (int)retval;
      }
      #define GASNETI_HAVE_ATOMIC_CAS 1

      GASNETI_INLINE(gasneti_atomic_fetchadd_32)
      uint32_t gasneti_atomic_fetchadd_32(gasneti_atomic_t *v, int32_t op) {
	/* CAUTION: Both PathScale and Intel compilers have been seen to be
         * rather fragile with respect to this asm template (bug 1563).
         * Change this at your own risk!
         */
	uint32_t retval = op;
        __asm__ __volatile__(
                GASNETI_X86_LOCK_PREFIX
		"xaddl %0, %1"
                : "=&r" (retval), "=m" (v->ctr)
                : "0" (retval), "m" (v->ctr)
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
	return retval;
      }

      /* Default versions of add and subtract */
      #define _gasneti_atomic_fetchadd gasneti_atomic_fetchadd_32

      /* x86 and x86_64 include full memory fence in locked RMW insns */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)
     #endif /* !slow atomics */
    #elif defined(__SUNPRO_C) || defined(__SUNPRO_CC) || defined(__PGI)
      /* First, some macros to hide the x86 vs. x86-64 ABI differences */
      #if defined(__x86_64__) || defined(__amd64)
        #define _gasneti_atomic_addr		"(%rdi)"
        #define _gasneti_atomic_load_arg0	""	/* arg0 in rdi */
        #define _gasneti_atomic_load_arg1	"movl %esi, %eax	\n\t"
	#define _gasneti_atomic_load_arg2	""	/* arg2 in rdx */
      #else
        #define _gasneti_atomic_addr		"(%ecx)"
        #define _gasneti_atomic_load_arg0	"movl 8(%ebp), %ecx	\n\t"
        #define _gasneti_atomic_load_arg1	"movl 12(%ebp), %eax	\n\t"
	#define _gasneti_atomic_load_arg2	"movl 16(%ebp), %edx	\n\t"
      #endif

      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))

      #define GASNETI_ATOMIC_INCREMENT_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "incl " _gasneti_atomic_addr )

      #define GASNETI_ATOMIC_DECREMENT_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "decl " _gasneti_atomic_addr )

      #define GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY			\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "decl " _gasneti_atomic_addr			"\n\t" \
		       "sete %cl					\n\t" \
		       "movzbl %cl, %eax" )

      #define GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY			\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       _gasneti_atomic_load_arg2			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "cmpxchgl %edx, " _gasneti_atomic_addr		"\n\t" \
		       "sete  %cl					\n\t" \
		       "movzbl  %cl, %eax" )
      #define GASNETI_HAVE_ATOMIC_CAS 1

#if 1
      /* Fetch-add version is faster for calls that ignore the result and
       * for subtraction of constants.  In both cases because the "extra"
       * work is done in C code that the optimizer can discard.
       */
      #define GASNETI_ATOMIC_FETCHADD_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "xadd %eax, " _gasneti_atomic_addr	)
#else
      #define GASNETI_ATOMIC_ADD_BODY					\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       "movl %eax, %edx					\n\t" \
		       GASNETI_X86_LOCK_PREFIX				\
		       "xadd %eax, " _gasneti_atomic_addr		"\n\t" \
		       "addl %edx, %eax"	)

      #define GASNETI_ATOMIC_SUBTRACT_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       "movl %eax, %edx					\n\t" \
		       "negl %eax					\n\t" \
		       GASNETI_X86_LOCK_PREFIX				\
		       "xadd %eax, " _gasneti_atomic_addr		"\n\t" \
		       "subl %edx, %eax"	)

      #define GASNETI_HAVE_ATOMIC_ADD_SUB 1
#endif

      /* x86 and x86_64 include full memory fence in locked RMW insns */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)
    #else
      #error unrecognized x86 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__ia64__) || defined(__ia64) /* Itanium */
    #if defined(__INTEL_COMPILER)
      /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
      #include <ia64intrin.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) _InterlockedIncrement((volatile int *)&((p)->ctr))
      #define _gasneti_atomic_decrement(p) _InterlockedDecrement((volatile int *)&((p)->ctr))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) \
                    (_InterlockedDecrement((volatile int *)&((p)->ctr)) == 0)

      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
                    (_InterlockedCompareExchange_acq((volatile unsigned int *)&((p)->ctr),nval,oval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* The default c-a-s based add and subtract are already the best we can do. */

      /* See fence treatment after #endif */
    #elif defined(__GNUC__)
      GASNETI_INLINE(gasneti_cmpxchg)
      uint32_t gasneti_cmpxchg(int32_t volatile *ptr, uint32_t oldval, uint32_t newval) {
        uint64_t tmp = oldval;

	/* Load "ar.ccv", the special register used for "oldval" in c-a-s */
        __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(tmp));

        __asm__ __volatile__ ("cmpxchg4.acq %0=[%1],%2,ar.ccv"
                                : "=r"(tmp) : "r"(ptr), "r"(newval) );
        return (uint32_t) tmp;
      }
      GASNETI_INLINE(gasneti_atomic_fetchandinc_32)
      uint32_t gasneti_atomic_fetchandinc_32(int32_t volatile *ptr) {
        uint64_t result;\
        asm volatile ("fetchadd4.acq %0=[%1],%2"
                                : "=r"(result) : "r"(ptr), "i" (1) );
        return (uint32_t) result;
      }
      GASNETI_INLINE(gasneti_atomic_fetchanddec_32)
      uint32_t gasneti_atomic_fetchanddec_32(int32_t volatile *ptr) {
        uint64_t result;\
        asm volatile ("fetchadd4.acq %0=[%1],%2"
                                : "=r"(result) : "r"(ptr), "i" (-1) );
        return (uint32_t) result;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (gasneti_atomic_fetchandinc_32(&((p)->ctr)))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_fetchanddec_32(&((p)->ctr)))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchanddec_32(&((p)->ctr)) == 1)

      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
        (gasneti_cmpxchg((volatile int *)&((p)->ctr),oval,nval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* The default c-a-s based add and subtract are already the best we can do. */

      /* See fence treatment after #endif */
    #elif defined(__HP_cc) || defined(__HP_aCC) /* HP C/C++ Itanium intrinsics */
      #include <machine/sys/inline.h>
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }

      /* legal values for imm are -16, -8, -4, -1, 1, 4, 8, and 16 
         returns *old* value */
      #define gasneti_atomic_addandfetch_32(ptr, imm) \
         _Asm_fetchadd(_FASZ_W, _SEM_ACQ,             \
                       ptr, imm,                      \
                       _LDHINT_NONE, (_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE))
      GASNETI_INLINE(gasneti_cmpxchg)
      int32_t gasneti_cmpxchg(int32_t volatile *ptr, uint32_t oldval, uint32_t newval) {
        register uint64_t _r_;
        _Asm_mov_to_ar(_AREG_CCV, (int64_t)oldval);
        _r_ = _Asm_cmpxchg(_SZ_W, _SEM_ACQ, 
                           ptr, newval, 
                           _LDHINT_NONE, (_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE));
        return (int32_t) _r_;
      }
      #define _gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 1)

      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
        (gasneti_cmpxchg((volatile int *)&((p)->ctr),oval,nval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* The default c-a-s based add and subtract are already the best we can do. */

      /* See fence treatment after #endif */
    #else
      #error unrecognized Itanium compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif

    /* Since all 3 compilers are generating r-m-w with .acq variants, we can customize
     * the atomic fencing implementation by noting that "mf;; foo.acq" is a full memory
     * barrier both before and after. */
    #define _gasneti_atomic_fence_before_rmw(flags) \
	if (flags & (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)) gasneti_local_mb();
    #define _gasneti_atomic_fence_after_rmw(flags) \
	/* Nothing */
    #define _gasneti_atomic_fence_after_bool(flags, val) \
	if (!(flags & (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST))) \
	  { _gasneti_atomic_rmb_bool(flags, val) } 
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__alpha__) || defined(__alpha) /* DEC Alpha */
    #if defined(__GNUC__)
      GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
      uint32_t gasneti_atomic_fetchandadd_32(int32_t volatile *v, int32_t op) {
        register int32_t temp;
        register uint32_t result;
        __asm__ __volatile__ (
		"1:	ldl_l	%1, %2		\n"	/* result = *addr */
		"	addl	%1, %3, %0	\n"	/* temp = result + op */
		"	stl_c	%0, %2		\n"	/* *addr = temp; temp = store_OK */
		"	beq	%0, 1b"			/* Retry on ll/ss failure */
		: "=&r" (temp), "=&r" (result), "=m" (*v) /* outputs */
		: "IOr" (op)       /* inputs */
		: "cc"); /* kills */
        return result;
      }
      GASNETI_INLINE(gasneti_atomic_add_32)
      void gasneti_atomic_add_32(int32_t volatile *v, int32_t op) {
        register int32_t temp;
        __asm__ __volatile__ (
		"1:	ldl_l	%0, %1		\n"	/* temp = *addr */
		"	addl	%0, %2, %0	\n"	/* temp += op */
		"	stl_c	%0, %1		\n"	/* *addr = temp; temp = store_OK */
		"	beq	%0, 1b"			/* Retry on ll/ss failure */
		: "=&r" (temp), "=m" (*v) /* outputs */
		: "IOr" (op)              /* inputs */
		: "cc");        /* kills */
      }
     typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
     #define _gasneti_atomic_increment(p) (gasneti_atomic_add_32(&((p)->ctr),1))
     #define _gasneti_atomic_decrement(p) (gasneti_atomic_add_32(&((p)->ctr),-1))
     #define _gasneti_atomic_read(p)      ((p)->ctr)
     #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
     #define _gasneti_atomic_init(v)      { (v) }
     #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchandadd_32(&((p)->ctr),-1) == 1)

     GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
     int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
       unsigned long ret;
       __asm__ __volatile__ (
		"1:	ldl_l	%0,%1\n"	/* Load-linked of current value */
		"	cmpeq	%0,%2,%0\n"	/* compare to oldval */
		"	beq	%0,2f\n"	/* done/fail on mismatch (success/fail in ret) */
		"	mov	%3,%0\n"	/* copy newval to ret */
		"	stl_c	%0,%1\n"	/* Store-conditional of newval (success/fail in ret) */
		"	beq	%0,1b\n"	/* Retry on stl_c failure */
		"2:	"
       		: "=&r"(ret), "=m"(*p)
		: "r"(oldval), "r"(newval)
		: "cc");

       return ret;
     }
     #define GASNETI_HAVE_ATOMIC_CAS 1

     #define _gasneti_atomic_fetchadd(p,op) gasneti_atomic_fetchandadd_32(&((p)->ctr), op)

     /* No fences in our asm, so using default fences */
    #elif (defined(__DECC) || defined(__DECCXX)) && defined(__osf__)
       /* Compaq C / OSF atomics are compiler built-ins */
       #include <sys/machine/builtins.h>
       typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
       #define _gasneti_atomic_increment(p) (__ATOMIC_INCREMENT_LONG(&((p)->ctr)))
       #define _gasneti_atomic_decrement(p) (__ATOMIC_DECREMENT_LONG(&((p)->ctr)))
       #define _gasneti_atomic_read(p)      ((p)->ctr)
       #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
       #define _gasneti_atomic_init(v)      { (v) }
       #define _gasneti_atomic_decrement_and_test(p) \
                                          (__ATOMIC_DECREMENT_LONG(&((p)->ctr)) == 1)

       /* The __CMP_STORE_LONG built-in is insufficient alone because it returns
	  a failure indication if the LL/SC is interrupted by another write to the
          same cache line (it does not retry).
       */
       GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
       int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
         return asm("1:	ldl_l	%v0,(%a0);"	/* Load-linked of current value to %v0 */
		    "	cmpeq	%v0,%a1,%v0;"	/* compare %v0 to oldval w/ result to %v0 */
		    "	beq	%v0,2f;"	/* done/fail on mismatch (success/fail in %v0) */
		    "	mov	%a2,%v0;"	/* copy newval to %v0 */
		    "	stl_c	%v0,(%a0);"	/* Store-conditional of newval (success/fail in %v0) */
		    "	beq	%v0,1b;"	/* Retry on stl_c failure */
		    "2:	", p, oldval, newval);  /* Returns value from %v0 */
       }
       #define GASNETI_HAVE_ATOMIC_CAS 1

       #define _gasneti_atomic_fetchadd(p, op) __ATOMIC_ADD_LONG(&((p)->ctr), op)

       /* Both the instrisics and our asm lack built-in fences.  So, using default fences */
    #else
      #error unrecognized Alpha compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__sparc) || defined(__sparc__)
    #if defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_ULTRASPARC) /* SPARC v9 ISA */
      #if defined(__GNUC__)
        GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
        int32_t gasneti_atomic_fetchandadd_32(int32_t volatile *v, int32_t op) {
          /* SPARC v9 architecture manual, p.333 
           * This function requires the cas instruction in Sparc V9, and therefore gcc -mcpu=ultrasparc
	   * The manual says (sec A.9) no memory fences in CAS (in conflict w/ JMM web page).
           */
          register int32_t volatile * addr = (int32_t volatile *)v;
          register int32_t oldval;
          register int32_t newval;
          __asm__ __volatile__ ( 
            "ld       [%4],%0    \n\t" /* oldval = *addr; */
            "0:			 \t" 
            "add      %0,%3,%1   \n\t" /* newval = oldval + op; */
            "cas      [%4],%0,%1 \n\t" /* if (*addr == oldval) SWAP(*addr,newval); else newval = *addr; */
            "cmp      %0, %1     \n\t" /* check if newval == oldval (swap succeeded) */
            "bne,a,pn %%icc, 0b  \n\t" /* otherwise, retry (,pn == predict not taken; ,a == annul) */
            "  mov    %1, %0     "     /* oldval = newval; (branch delay slot, annulled if not taken) */
            : "=&r"(oldval), "=&r"(newval), "=m"(*addr)
            : "rn"(op), "r"(addr) );
          return oldval;
        }
        typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
        #define _gasneti_atomic_read(p)      ((p)->ctr)
        #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
        #define _gasneti_atomic_init(v)      { (v) }

        /* Default impls of inc, dec, dec-and-test, add and sub */
        #define _gasneti_atomic_fetchadd(p,op) gasneti_atomic_fetchandadd_32(&((p)->ctr),op)

        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *v, uint32_t oldval, uint32_t newval) {
          register volatile uint32_t * addr = (volatile uint32_t *)&(v->ctr);
          __asm__ __volatile__ ( 
              "cas      [%3],%2,%0"  /* if (*addr == oldval) SWAP(*addr,newval); else newval = *addr; */
              : "+r"(newval), "=m"(*addr)
              : "r"(oldval), "r"(addr) );
          return (int)(newval == oldval);
        }
        #define GASNETI_HAVE_ATOMIC_CAS 1

	/* Using default fences, as our asm includes none */
      #elif defined(__SUNPRO_C) || defined(__SUNPRO_CC)
	typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
	#define _gasneti_atomic_init(v)      { (v) }
        #define _gasneti_atomic_read(p)      ((p)->ctr)
        #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))

        /* Default impls of inc, dec, dec-and-test, add and sub */

        #define GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY					\
	    GASNETI_ASM(								\
		/* if (*addr == oldval) SWAP(*addr,newval); else newval = *addr; */	\
		     "cas	[%i0], %i1, %i2		\n\t"				\
		/* retval = (oldval == newval) ? 1 : 0				*/	\
		     "xor	%i2, %i1, %g1		\n\t" /* g1 = 0 IFF old==new */ \
		     "cmp	%g0, %g1		\n\t" /* Set/clear carry bit */	\
		     "subx	%g0, -1, %i0 " )	      /* Subtract w/ carry */
        #define GASNETI_HAVE_ATOMIC_CAS 1

        #define GASNETI_ATOMIC_FETCHADD_BODY /* see gcc asm, above, for more detail */	\
	    GASNETI_ASM(								\
		/* oldval = *addr;						*/	\
		     "ld	[%i0], %g1		\n"				\
		/* while (!cas(addr, oldval, oldval + op)) { oldval = *addr; }	*/	\
		     "0:				\n\t"				\
		     "add	%g1, %i1, %i5		\n\t"				\
		     "cas	[%i0], %g1, %i5		\n\t"				\
		     "cmp	%g1, %i5		\n\t"				\
		     "bne,a,pn	%icc, 0b		\n\t"				\
		     "  mov	%i5, %g1		\n\t" /* annulled delay slot */	\
		/* Retval = oldval						*/	\
		     "mov	%i5, %i0" )

	/* Using default fences, as our asm includes none */
      #else
        #error unrecognized Sparc v9 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
      #endif
    #else /* SPARC pre-v9 lacks RMW instructions - 
             all we get is atomic swap, but that's actually just barely enough  */
      #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1 /* not signal-safe because of "checkout" semantics */
      #if defined(__GNUC__) || defined(__SUNPRO_C) || defined(__SUNPRO_CC)
        /* Only 31 bits: */
        #define GASNETI_ATOMIC_MAX		((uint32_t)0x7FFFFFFFU)
        #define GASNETI_ATOMIC_SIGNED_MIN	((int32_t)0xC0000000)
        #define GASNETI_ATOMIC_SIGNED_MAX	((int32_t)0x3FFFFFFF)
        #define gasneti_atomic_signed(val)	(((int32_t)((val)<<1))>>1)

        #define GASNETI_ATOMIC_PRESENT    ((uint32_t)0x80000000)
        #define GASNETI_ATOMIC_INIT_MAGIC ((uint64_t)0x8BDEF66BAD1E3F3AULL)

        typedef struct { volatile uint64_t initflag; volatile uint32_t ctr; } gasneti_atomic_t;
        #define _gasneti_atomic_init(v)      { GASNETI_ATOMIC_INIT_MAGIC, (GASNETI_ATOMIC_PRESENT|(v)) }

        /* would like to use gasneti_waituntil here, but it requires libgasnet for waitmode */
        #define gasneti_atomic_spinuntil(cond) do {       \
                while (!(cond)) gasneti_compiler_fence(); \
                gasneti_local_rmb();                      \
                } while (0)

        #if defined(__GNUC__)
          GASNETI_INLINE(gasneti_loadandclear_32)
          uint32_t gasneti_loadandclear_32(uint32_t volatile *v) {
            register uint32_t volatile * addr = (uint32_t volatile *)v;
            register uint32_t val = 0;
            __asm__ __volatile__ ( 
              "swap %1, %0 \n"   
              : "+r" (val), "=m" (*addr) );
            return val;
          }
          GASNETI_INLINE(gasneti_checkout_32)
          uint32_t gasneti_checkout_32(gasneti_atomic_t *p) {
	    uint32_t retval;
            gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
            gasneti_local_wmb();
            gasneti_atomic_spinuntil(p->ctr && (retval = gasneti_loadandclear_32(&(p->ctr))));
            gasneti_assert(retval & GASNETI_ATOMIC_PRESENT);
	    return retval;
	  }
	#else
          /* The following is straight-forward asm for
	   *  "while(!(p->ctr && (retval = gasneti_loadandclear_32(&(p->ctr))))) {}" 
	   * If one adds the WAITHOOK() to the gcc version, then this should change too.
	   */
	  #define GASNETI_ATOMIC_EXTRA_SPECIAL                                     \
		GASNETI_SPECIAL_ASM_DEFN(_gasneti_special_atomic_checkout,         \
		GASNETI_ASM("	add	%i0,8,%g1	\n\t"	/* g1 = &p->ctr */ \
			    "1:	ld	[%g1],%i0	\n\t"	/* i0 = *g1     */ \
			    "	cmp	%i0,0		\n\t"	/* if (!i0)     */ \
			    "	be	1b		\n\t"	/*     goto 1   */ \
			    "	 mov	0,%i0		\n\t"	/* i0 = 0       */ \
			    "	swap	[%g1],%i0	\n\t"	/* swap(*g1,i0) */ \
			    "	cmp	%i0,0		\n\t"	/* if (!i0)     */ \
			    "	be	1b		\n\t"	/*     goto 1   */ \
			    "	 nop			")	/* returns i0   */ \
		)
          GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_checkout);
          GASNETI_INLINE(gasneti_checkout_32)
          uint32_t gasneti_checkout_32(gasneti_atomic_t *p) {
	    uint32_t retval;
            gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
            gasneti_local_wmb();
	    retval = (*(uint32_t (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_checkout))(p);
            gasneti_assert(retval & GASNETI_ATOMIC_PRESENT);
	    return retval;
	  }
	#endif

        GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
        uint32_t gasneti_atomic_fetchandadd_32(gasneti_atomic_t *p, int32_t op) {
          const uint32_t tmp = gasneti_checkout_32(p);
          p->ctr = (GASNETI_ATOMIC_PRESENT | (tmp + op));
          return (tmp & ~GASNETI_ATOMIC_PRESENT);
        }
        #if 0
          /* this version fails if set is used in a race with fetchandadd */
          GASNETI_INLINE(_gasneti_atomic_set)
          void _gasneti_atomic_set(gasneti_atomic_t *p, uint32_t val) {
            gasneti_local_wmb();
            p->ctr = (GASNETI_ATOMIC_PRESENT | val);
          }
        #else
          GASNETI_INLINE(_gasneti_atomic_set)
          void _gasneti_atomic_set(gasneti_atomic_t *p, uint32_t val) {
            uint32_t tmp;
            if_pf (p->initflag != GASNETI_ATOMIC_INIT_MAGIC) {
              gasneti_local_wmb();
              p->ctr = (GASNETI_ATOMIC_PRESENT | val);
              gasneti_local_wmb();
              p->initflag = GASNETI_ATOMIC_INIT_MAGIC;
            } else {
	      (void)gasneti_checkout_32(p);
              p->ctr = (GASNETI_ATOMIC_PRESENT | val);
            }
          }
        #endif
        GASNETI_INLINE(_gasneti_atomic_read)
        uint32_t _gasneti_atomic_read(gasneti_atomic_t *p) {
          uint32_t tmp;
          gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
          gasneti_atomic_spinuntil((tmp = p->ctr));
          gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
          return (tmp & ~GASNETI_ATOMIC_PRESENT);
        }

        /* Default impls of inc, dec, dec-and-test, add and sub */
        #define _gasneti_atomic_fetchadd gasneti_atomic_fetchandadd_32

        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
          uint32_t tmp = gasneti_checkout_32(p);
          const int retval = (tmp == (GASNETI_ATOMIC_PRESENT | oldval));
          if_pt (retval) {
            tmp = (GASNETI_ATOMIC_PRESENT | newval);
          }
          p->ctr = tmp;
          return retval;
        }
        #define GASNETI_HAVE_ATOMIC_CAS 1

        /* Our code has the following fences: (noting that RMB is empty) */
	#define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_MB_PRE
	#define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_PRE
      #else
        #error unrecognized Sparc pre-v9 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
      #endif
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__hppa) || defined(__hppa__) /* PA-RISC */
    /* all we get is atomic load-and-clear, but that's actually just barely enough  */
    #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1 /* not signal-safe because of "checkout" semantics */
    /* The load-and-clear requires 16-byte alignment.  Therefore the type (and its
     * initializer) replicate the value field 4 times.  The actual ops will only use
     * the one of them that turns out to be 16-byte aligned.
     */
    typedef struct { volatile uint64_t initflag; volatile uint32_t _ctr[4]; char _pad; } gasneti_atomic_t;
    #define GASNETI_ATOMIC_PRESENT    ((uint32_t)0x80000000)
    #define GASNETI_ATOMIC_INIT_MAGIC ((uint64_t)0x8BDEF66BAD1E3F3AULL)
    #define _gasneti_atomic_init(v)      {    \
            GASNETI_ATOMIC_INIT_MAGIC,       \
            { (GASNETI_ATOMIC_PRESENT|(v)),  \
              (GASNETI_ATOMIC_PRESENT|(v)),  \
              (GASNETI_ATOMIC_PRESENT|(v)),  \
              (GASNETI_ATOMIC_PRESENT|(v)) } \
            }
    /* Only 31 bits: */
    #define GASNETI_ATOMIC_MAX		((uint32_t)0x7FFFFFFFU)
    #define GASNETI_ATOMIC_SIGNED_MIN	((int32_t)0xC0000000)
    #define GASNETI_ATOMIC_SIGNED_MAX	((int32_t)0x3FFFFFFF)
    #define gasneti_atomic_signed(val)	(((int32_t)((val)<<1))>>1)

    #if defined(__HP_aCC) /* HP C++ compiler */
      #define GASNETI_HAVE_ATOMIC_CAS 1		/* Explicit */
      #define GASNETI_HAVE_ATOMIC_ADD_SUB 1	/* Derived */
      #define GASNETI_USING_SLOW_ATOMICS 1
    #else
      GASNETI_INLINE(gasneti_loadandclear_32)
      uint32_t gasneti_loadandclear_32(uint32_t volatile *v) {
        register uint32_t volatile * addr = (uint32_t volatile *)v;
        register uint32_t val = 0;
        gasneti_assert(!(((uintptr_t)addr) & 0xF)); /* ldcws requires 16-byte alignment */
        *(volatile char *)(v+1) = 0; /* fetch this cache line as a dirty word - speeds up ldcw */
        #if defined(__GNUC__)
          __asm__ __volatile__ ( 
          #if 0
            "ldcws %1, %0 \n"  
            /* should be using "ldcws,co" here for better performance, 
               but GNU assembler rejects it (works with system assembler) 
             */
          #else
            "ldcw,co %1, %0 \n"  
            /* this alternate, undocumented pseudo-op instruction appears to do the right thing */
          #endif
            : "=r"(val), "=m" (*addr) );
        #elif defined(__HP_cc) /* HP C compiler */
          _asm("LDCWS,CO",0,0,addr,val);
        #else
          #error unrecognized PA-RISC compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
        #endif
        return val;
      }
      #define GASNETI_ATOMIC_CTR(p)     ((volatile uint32_t *)GASNETI_ALIGNUP(&(p->_ctr),16))
      /* would like to use gasneti_waituntil here, but it requires libgasnet for waitmode */
      #define gasneti_atomic_spinuntil(cond) do {       \
              while (!(cond)) gasneti_compiler_fence(); \
              gasneti_local_rmb();                      \
              } while (0)
      GASNETI_INLINE(gasneti_checkout_32)
      uint32_t gasneti_checkout_32(gasneti_atomic_t *p) {
        volatile uint32_t * const pctr = GASNETI_ATOMIC_CTR(p);
	uint32_t retval;
        gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
        gasneti_local_wmb();
        gasneti_atomic_spinuntil(*pctr && (retval = gasneti_loadandclear_32(pctr)));
        gasneti_assert(retval & GASNETI_ATOMIC_PRESENT);
	return retval;
      }
      GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
      uint32_t gasneti_atomic_fetchandadd_32(gasneti_atomic_t *p, int32_t op) {
        const uint32_t tmp = gasneti_checkout_32(p);
        volatile uint32_t * const pctr = GASNETI_ATOMIC_CTR(p);
        *pctr = (GASNETI_ATOMIC_PRESENT | (tmp + op));
        return (tmp & ~GASNETI_ATOMIC_PRESENT);
      }
      #if 0
        /* this version fails if set is used in a race with fetchandadd */
        GASNETI_INLINE(_gasneti_atomic_set)
        void _gasneti_atomic_set(gasneti_atomic_t *p, uint32_t val) {
          volatile uint32_t * const pctr = GASNETI_ATOMIC_CTR(p);
          gasneti_local_wmb();
          *pctr = (GASNETI_ATOMIC_PRESENT | val);
        }
      #else
        GASNETI_INLINE(_gasneti_atomic_set)
        void _gasneti_atomic_set(gasneti_atomic_t *p, uint32_t val) {
          uint32_t tmp;
          volatile uint32_t * const pctr = GASNETI_ATOMIC_CTR(p);
          if_pf (p->initflag != GASNETI_ATOMIC_INIT_MAGIC) {
            gasneti_local_wmb();
            *pctr = (GASNETI_ATOMIC_PRESENT | val);
            gasneti_local_wmb();
            p->initflag = GASNETI_ATOMIC_INIT_MAGIC;
          } else {
	    (void)gasneti_checkout_32(p);
            *pctr = (GASNETI_ATOMIC_PRESENT | val);
          }
        }
      #endif
      GASNETI_INLINE(_gasneti_atomic_read)
      uint32_t _gasneti_atomic_read(gasneti_atomic_t *p) {
        uint32_t tmp;
        volatile uint32_t * const pctr = GASNETI_ATOMIC_CTR(p);
        gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
        gasneti_atomic_spinuntil((tmp = *pctr));
        gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
        return (tmp & ~GASNETI_ATOMIC_PRESENT);
      }

      /* Default impls of inc, dec, dec-and-test, add and sub */
      #define gasneti_atomic_fetchandadd gasneti_atomic_fetchandadd_32

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
        uint32_t tmp = gasneti_checkout_32(p);
        const int retval = (tmp == (GASNETI_ATOMIC_PRESENT | oldval));
        volatile uint32_t * const pctr = GASNETI_ATOMIC_CTR(p);
        if_pt (retval) {
          tmp = (GASNETI_ATOMIC_PRESENT | newval);
        }
        *pctr = tmp;
        return retval;
      }
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* Our code has the following fences: (noting that RMB is empty) */
      #define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_MB_PRE
      #define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_PRE
    #endif /* ! slow atomics */
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__crayx1) /* This works on X1, but NOT the T3E */
    #include <intrinsics.h>
    typedef volatile long gasneti_atomic_t;
    /* man pages for atomic ops claim gsync is required for using atomic ops,
       but it's unclear when exactly it is required for our purposes - 
       technically we shouldn't need any sync for a bare unfenced AMO, but 
       experimentally determined using testtools that without a gsync in the vicinity
       of the AMO call, the compiler/architecture will break the semantics of the AMO
       with respect to the atomic location, even with only a single thread!
       Note gsync call MUST be _gsync(0x1) - the manpage docs are gratuitiously wrong, 
       everything else gives a bus error
     */
    #define gasneti_atomic_presync()  ((void)0)
    #define gasneti_atomic_postsync()  _gsync(0x1)

    #define _gasneti_atomic_increment(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)1),gasneti_atomic_postsync())
    #define _gasneti_atomic_decrement(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)-1),gasneti_atomic_postsync())
    #define _gasneti_atomic_read(p)      (*(p))
    #define _gasneti_atomic_set(p,v)     (*(p) = (v))
    #define _gasneti_atomic_init(v)      (v)
    GASNETI_INLINE(_gasneti_atomic_decrement_and_test_64)
    int _gasneti_atomic_decrement_and_test_64(gasneti_atomic_t *p) {
       int retval;
       gasneti_atomic_presync();
       retval = (_amo_afadd((p),(long)-1) == 1);
       gasneti_atomic_postsync();
       return retval;
    }
    #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_64

    GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
    int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, long oldval, long newval) {
      long result;
      gasneti_atomic_presync();
      result = _amo_acswap(p, oldval, newval);
      gasneti_atomic_postsync();
      return (result == oldval); 
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1

    GASNETI_INLINE(gasneti_atomic_addfetch_64)
    uint32_t gasneti_atomic_addfetch_64(gasneti_atomic_t *p, int32_t op) {
       uint32_t retval;
       gasneti_atomic_presync();
       retval = _amo_afadd((p),(long)(op));
       gasneti_atomic_postsync();
       return retval;
    }
    #define _gasneti_atomic_addfetch gasneti_atomic_addfetch_64

       /* Both the instrisics and our asm lack built-in fences.  So, using default fences */
    #define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_POST
  /* ------------------------------------------------------------------------------------ */
  #elif defined(_SX) /* NEC SX-6 */
    /* these are disabled for now because they don't link */
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
   #if 0
    #include <sys/mplock.h>
    #define _gasneti_atomic_read(p)      (atomic_read4((p)->ctr))
    #define _gasneti_atomic_set(p,v)     (atomic_set4((p)->ctr,(v)))
    #define _gasneti_atomic_init(v)      { (v) }

    /* Default impls of inc, dec, dec-and-test, add and sub */
    #define _gasneti_atomic_addfetch(p,op) atomic_add4(&((p)->ctr),op)

    /* Using default fences (TODO: VERIFY THAT WE NEED THEM) */
   #else
    #define _gasneti_atomic_read(p)      (muget(&((p)->ctr)))
    #define _gasneti_atomic_set(p,v)     (muset(&((p)->ctr),(v)))
    #define _gasneti_atomic_init(v)      { (v) }

    /* Default impls of inc, dec, dec-and-test, add and sub */
    #define _gasneti_atomic_addfetch(p,op) muadd(&((p)->ctr),op)

    /* Using default fences (TODO: VERIFY THAT WE NEED THEM) */
   #endif
  /* ------------------------------------------------------------------------------------ */
  /* PowerPPC ids:
   * AIX: _POWER
   * Darwin: __ppc__ or __ppc64__
   * Linux: __PPC__
   */
  #elif defined(_POWER) || defined(__PPC__) || defined(__ppc__) || defined(__ppc64__)
    #if defined(__xlC__)
      /* XLC machine code functions are very rigid, thus we produce all
       * three read-modify-write ops as distinct functions in order to
       * get anything near to optimal code.
       */
      static void gasneti_atomic_inc_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_inc_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"38840001"	/*    addi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
      }
      #pragma reg_killed_by gasneti_atomic_inc_32 cr0, gr4

      static void gasneti_atomic_dec_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_dec_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"3884ffff"	/*    subi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
      }
      #pragma reg_killed_by gasneti_atomic_dec_32 cr0, gr4

      static int32_t gasneti_atomic_decandfetch_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_decandfetch_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"3884ffff"	/*    subi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
	"7c832378"	/*    mr	r3,r4		*/ \
	/* RETURN in r3 = result after dec */ \
      }
      #pragma reg_killed_by gasneti_atomic_decandfetch_32 cr0, gr4

      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (gasneti_atomic_inc_32(&((p)->ctr)))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_dec_32(&((p)->ctr)))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_decandfetch_32(&((p)->ctr)) == 0)

      static int gasneti_atomic_swap_not_32(volatile int32_t *v, uint32_t oldval, uint32_t newval);
      #pragma mc_func gasneti_atomic_swap_not_32 {\
	/* ARGS: r3 = p, r4=oldval, r5=newval   LOCAL: r0 = tmp */ \
	"7c001828"	/* 0: lwarx	r0,0,r3		*/ \
	"7c002279"	/*    xor.	r0,r0,r4	*/ \
	"40820010"	/*    bne	1f		*/ \
	"7ca0192d"	/*    stwcx.	r5,0,r3		*/ \
	"40a2fff0"	/*    bne-	0b		*/ \
	"7c030378"	/* 1: mr	r3,r0		*/ \
	/* RETURN in r3 = 0 iff swap took place */ \
      }
      #pragma reg_killed_by gasneti_atomic_swap_not_32 cr0, gr0
      #define _gasneti_atomic_compare_and_swap(p, oldval, newval) \
	(gasneti_atomic_swap_not_32(&((p)->ctr),(oldval),(newval)) == 0)
      #define GASNETI_HAVE_ATOMIC_CAS 1

      static int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op);
      #pragma mc_func gasneti_atomic_addandfetch_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = op, r5 = tmp */ \
	"7ca01828"	/* 0: lwarx	r5,0,r3		*/ \
	"7ca52214"	/*    add	r5,r5,r4	*/ \
	"7ca0192d"	/*    stwcx.	r5,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
	"7ca32b78"	/*    mr	r3,r5		*/ \
	/* RETURN in r3 = result after addition */ \
      }
      #pragma reg_killed_by gasneti_atomic_addandfetch_32 cr0, gr5
      #define _gasneti_atomic_addfetch(p,op) gasneti_atomic_addandfetch_32(&((p)->ctr),op)

      /* Using default fences as we have none in our asms */
    #elif defined(__GNUC__)
      GASNETI_INLINE(gasneti_atomic_addandfetch_32)
      int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t result;
        __asm__ __volatile__ ( 
          "Lga.0.%=:\t"                 /* AIX assembler doesn't grok "0:"-type local labels */
          "lwarx    %0,0,%1 \n\t" 
          "add%I2   %0,%0,%2 \n\t"
          "stwcx.   %0,0,%1 \n\t"
          "bne-     Lga.0.%= \n\t" 
          : "=&b"(result)		/* constraint b = "b"ase register (not r0) */
          : "r" (addr), "Ir"(op) 
          : "cr0");
        return result;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }

      /* Default impls of inc, dec, dec-and-test, add and sub */
      #define _gasneti_atomic_addfetch(p,op) gasneti_atomic_addandfetch_32(&((p)->ctr),op)

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
        register uint32_t result;
        __asm__ __volatile__ (
          "Lga.0.%=:\t"                   /* AIX assembler doesn't grok "0:"-type local labels */
	  "lwarx    %0,0,%1 \n\t"         /* load to result */
	  "xor.     %0,%0,%2 \n\t"        /* xor result w/ oldval */
	  "bne      Lga.1.%= \n\t"        /* branch on mismatch */
	  "stwcx.   %3,0,%1 \n\t"         /* store newval */
	  "bne-     Lga.0.%= \n\t" 
	  "Lga.1.%=:	"
	  : "=&r"(result)
	  : "r" (p), "r"(oldval), "r"(newval)
	  : "cr0");
  
        return (result == 0);
      } 
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* Using default fences as we have none in our asms */
    #else
      #error Unrecognized PowerPC - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  #elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
    #if defined(__GNUC__)
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      GASNETI_INLINE(_gasneti_atomic_increment_32)
      void _gasneti_atomic_increment_32(gasneti_atomic_t *p) {
	uint32_t tmp;
	__asm__ __volatile__(
		"1:		\n\t"
		"ll	%0,%1	\n\t"
		"addu	%0,1	\n\t"
		"sc	%0,%1	\n\t"
		"beqz	%0,1b	"
		: "=&r" (tmp), "=m" (p->ctr)
		: "m" (p->ctr) );
      } 
      #define _gasneti_atomic_increment _gasneti_atomic_increment_32
      GASNETI_INLINE(_gasneti_atomic_decrement_32)
      void _gasneti_atomic_decrement_32(gasneti_atomic_t *p) {
	uint32_t tmp;
	__asm__ __volatile__(
		"1:		\n\t"
		"ll	%0,%1	\n\t"
		"subu	%0,1 	\n\t"
		"sc	%0,%1 	\n\t"
		"beqz	%0,1b	"
		: "=&r" (tmp), "=m" (p->ctr)
		: "m" (p->ctr) );
      }
      #define _gasneti_atomic_decrement _gasneti_atomic_decrement_32
      GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
      uint32_t gasneti_atomic_fetchandadd_32(gasneti_atomic_t *p, int32_t op) {
	uint32_t tmp, retval;
	__asm__ __volatile__(
		"1:			\n\t"
		"ll	%0,%2		\n\t"
		"addu	%1,%0,%3	\n\t"
		"sc	%1,%2		\n\t"
		"beqz	%1,1b		"
		: "=&r" (retval), "=&r" (tmp), "=m" (p->ctr)
		: "Ir" (op), "m" (p->ctr) );
	return retval;
      }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchandadd_32((p),-1) == 1)

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
         uint32_t temp;
         int retval = 0;
         __asm__ __volatile__ (
		"1:			\n\t"
		"ll	%1,%5		\n\t"	/* Load from *p */
		"bne	%1,%3,2f	\n\t"	/* Break loop on mismatch */
		"move	%0,%4		\n\t"	/* Move newval to retval */
		"sc	%0,%2		\n\t"	/* Try SC to store retval */
		"beqz	%0,1b		\n"	/* Retry on contention */
		"2:			"
                : "+r" (retval), "=&r" (temp), "=m" (p->ctr)
                : "r" (oldval), "r" (newval), "m" (p->ctr) );
        return retval;
      }
      #define GASNETI_HAVE_ATOMIC_CAS 1

      #define _gasneti_atomic_fetchadd gasneti_atomic_fetchandadd_32

      /* No memory fences in our asm, so using default fences */
    #endif
  #else
    #error Unrecognized platform - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Wrappers for "special" atomics, if any */

#ifdef GASNETI_ATOMIC_READ_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_read);
  #define _gasneti_atomic_read \
	(*(uint32_t (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_read))
#endif
#ifdef GASNETI_ATOMIC_SET_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_set);
  #define _gasneti_atomic_set \
	(*(void (*)(gasneti_atomic_t *p, uint32_t))(&_gasneti_special_atomic_increment))
#endif
#ifdef GASNETI_ATOMIC_INCREMENT_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_increment);
  #define _gasneti_atomic_increment \
	(*(void (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_increment))
#endif
#ifdef GASNETI_ATOMIC_DECREMENT_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_decrement);
  #define _gasneti_atomic_decrement \
	(*(void (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_decrement))
#endif
#ifdef GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_decrement_and_test);
  #define _gasneti_atomic_decrement_and_test \
	(*(int (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_decrement_and_test))
#endif
#ifdef GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_compare_and_swap);
  #define _gasneti_atomic_compare_and_swap \
	(*(int (*)(gasneti_atomic_t *, uint32_t, uint32_t))(&_gasneti_special_atomic_compare_and_swap))
  #ifndef GASNETI_HAVE_ATOMIC_CAS
    #error GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY defined when GASNETI_HAVE_ATOMIC_CAS is not.
  #endif
#endif
#ifdef GASNETI_ATOMIC_ADD_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_add);
  #define _gasneti_atomic_add \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_add))
  #ifndef GASNETI_HAVE_ATOMIC_ADD_SUB
    #error GASNETI_ATOMIC_ADD_BODY defined when GASNETI_HAVE_ATOMIC_ADD_SUB is not.
  #endif
#endif
#ifdef GASNETI_ATOMIC_SUBTRACT_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_subtract);
  #define _gasneti_atomic_subtract \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_subtract))
  #ifndef GASNETI_HAVE_ATOMIC_ADD_SUB
    #error GASNETI_ATOMIC_SUBTACT_BODY defined when GASNETI_HAVE_ATOMIC_ADD_SUB is not.
  #endif
#endif
#ifdef GASNETI_ATOMIC_FETCHADD_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_fetchadd);
  #define _gasneti_atomic_fetchadd \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_fetchadd))
#endif
#ifdef GASNETI_ATOMIC_ADDFETCH_BODY
  GASNETI_SPECIAL_ASM_DECL(_gasneti_special_atomic_addfetch);
  #define _gasneti_atomic_addfetch \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_addfetch))
#endif

/* ------------------------------------------------------------------------------------ */
/* Slow function-call based atomics
 * Used at client compile time for any compiler w/o inline asm support
 */

#if defined(GASNETI_USING_SLOW_ATOMICS)
  GASNETI_EXTERNC uint32_t gasneti_slow_atomic_read(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_read gasneti_slow_atomic_read
  GASNETI_EXTERNC void gasneti_slow_atomic_set(gasneti_atomic_t *p, uint32_t v, const int flags);
  #define gasneti_atomic_set gasneti_slow_atomic_set
  GASNETI_EXTERNC void gasneti_slow_atomic_increment(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_increment gasneti_slow_atomic_increment
  GASNETI_EXTERNC void gasneti_slow_atomic_decrement(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_decrement gasneti_slow_atomic_decrement
  GASNETI_EXTERNC int gasneti_slow_atomic_decrement_and_test(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_decrement_and_test gasneti_slow_atomic_decrement_and_test
  #if defined(GASNETI_HAVE_ATOMIC_CAS)
    GASNETI_EXTERNC int gasneti_slow_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval, const int flags);
    #define gasneti_atomic_compare_and_swap gasneti_slow_atomic_compare_and_swap
  #endif
  #if defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
    GASNETI_EXTERNC uint32_t gasneti_slow_atomic_add(gasneti_atomic_t *p, uint32_t op, const int flags);
    #define gasneti_atomic_add gasneti_slow_atomic_add
    GASNETI_EXTERNC uint32_t gasneti_slow_atomic_subtract(gasneti_atomic_t *p, uint32_t op, const int flags);
    #define gasneti_atomic_subtract gasneti_slow_atomic_subtract
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
#endif
