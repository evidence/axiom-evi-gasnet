/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_atomic_bits.h,v $
 *     $Date: 2005/05/06 09:59:16 $
 * $Revision: 1.67 $
 * Description: GASNet header for portable atomic memory operations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNET_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_ATOMICOPS_H
#define _GASNET_ATOMICOPS_H

/* ------------------------------------------------------------------------------------ */
/* portable atomic increment/decrement 

   these provide a special datatype (gasneti_atomic_t) representing an atomically
    updated unsigned integer value and a set of atomic ops
   atomicity is guaranteed only if ALL accesses to the gasneti_atomic_t data happen
    through the provided operations (i.e. it is an error to directly access the 
    contents of a gasneti_atomic_t), and if the gasneti_atomic_t data is only  
    addressable by the current process (e.g. not in a System V shared memory segment)

    gasneti_atomic_init(v)      initializer for an gasneti_atomic_t to value v
    gasneti_atomic_set(p,v)     atomically sets *p to value v
    gasneti_atomic_read(p)      atomically read and return the value of *p
    gasneti_atomic_increment(p) atomically increment *p (no return value)
    gasneti_atomic_decrement(p) atomically decrement *p (no return value)
    gasneti_atomic_decrement_and_test(p) 
      atomically decrement *p, return non-zero iff the new value is 0
 */

#if defined(CRAYT3E)   || /* T3E seems to have no atomic ops */              \
    defined(_SX)       || /* NEC SX-6 atomics not available to user code? */ \
    defined(__hppa)    || /* PA-RISC seems to have no atomic ops */          \
    (defined(__PGI) && defined(BROKEN_LINUX_ASM_ATOMIC_H)) || /* haven't implemented atomics for PGI */ \
    (defined(__sparc) && /* SPARC pre-v9 lacks RMW instructions */ \
       !(defined(__GNUC__) && (defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_SPARCV9))))
  #define GASNETI_USE_GENERIC_ATOMICOPS
#endif
/* misc rerequisites to detection logic below */
#if defined(LINUX)
  #include <linux/config.h>
#endif

/* ------------------------------------------------------------------------------------ */
#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  /* a very slow but portable implementation of atomic ops */
  #ifdef _INCLUDED_GASNET_H
    extern void *gasneti_patomicop_lock; /* bug 693: avoid header dependency cycle */
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_init(v)      { (v) }
    #define gasneti_atomic_set(p,v) do {                          \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        (p)->ctr = (v);                                           \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    #define gasneti_atomic_increment(p) do {                      \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        ((p)->ctr)++;                                             \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    #define gasneti_atomic_decrement(p) do {                      \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        ((p)->ctr)--;                                             \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    extern int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p);
    #define GASNETI_GENERIC_DEC_AND_TEST_DEF                     \
    int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) { \
      uint32_t newval;                                           \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);    \
      newval = p->ctr - 1;                                       \
      p->ctr = newval;                                           \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);  \
      return (newval == 0);                                      \
    }
  #elif defined(_REENTRANT) || defined(_THREAD_SAFE) || \
        defined(PTHREAD_MUTEX_INITIALIZER) ||           \
        defined(HAVE_PTHREAD) || defined(HAVE_PTHREAD_H)
    /* a version for pthreads which is independent of GASNet HSL's 
       requires the client to #define GASNETT_MAIN in exactly one linked file 
     */
    #include <pthread.h>
    extern pthread_mutex_t gasneti_atomicop_mutex; 
    extern int gasneti_atomicop_initcheck;
    #ifdef GASNETT_MAIN
      pthread_mutex_t gasneti_atomicop_mutex = PTHREAD_MUTEX_INITIALIZER;
      int gasneti_atomicop_initcheck = 1;
    #endif
    #if !defined(NDEBUG) && !defined(GASNET_NDEBUG)
      #define GASNETI_ATOMICOP_INITCHECK() do {                                          \
        if (!gasneti_atomicop_initcheck) {                                               \
          fprintf(stderr, "ERROR: on this platform, gasnet_tools.h "                     \
           "requires exactly one file to #define GASNETT_MAIN in order to use atomics"); \
          abort();                                                                       \
        }                                                                                \
      } while (0)
    #else
      #define GASNETI_ATOMICOP_INITCHECK() ((void)0)
    #endif
    /* intentionally make these a different size than regular 
       GASNet atomics, to cause a link error on attempts to mix them
     */
    typedef struct { volatile uint32_t ctr; char _pad; } gasneti_atomic_t;
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_init(v)      { (v) }
    #define gasneti_atomic_set(p,v) do {               \
        GASNETI_ATOMICOP_INITCHECK();                  \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        (p)->ctr = (v);                                \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define gasneti_atomic_increment(p) do {           \
        GASNETI_ATOMICOP_INITCHECK();                  \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)++;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define gasneti_atomic_decrement(p) do {           \
        GASNETI_ATOMICOP_INITCHECK();                  \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)--;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define gasneti_atomic_decrement_and_test(p) \
           _gasneti_atomic_decrement_and_test((p), \
              &gasneti_atomicop_mutex, gasneti_atomicop_initcheck)
    GASNET_INLINE_MODIFIER(_gasneti_atomic_decrement_and_test)
    int _gasneti_atomic_decrement_and_test(gasneti_atomic_t *p, 
                pthread_mutex_t *m, int gasneti_atomicop_initcheck) {
      uint32_t newval;
      GASNETI_ATOMICOP_INITCHECK();
      pthread_mutex_lock(m);
      newval = p->ctr - 1;
      p->ctr = newval;
      pthread_mutex_unlock(m);
      return (newval == 0);
    }
  #else
    /* only one thread - everything atomic by definition */
    /* attempt to generate a compile error if pthreads actually are in use */
    #define PTHREAD_MUTEX_INITIALIZER ERROR_include_pthread_h_before_gasnet_tools_h
    extern int pthread_mutex_lock; 

    typedef volatile int gasneti_atomic_t;
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_init(v)      (v)
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_increment(p) ((*(p))++)
    #define gasneti_atomic_decrement(p) ((*(p))--)
    #define gasneti_atomic_decrement_and_test(p) ((--(*(p))) == 0)
  #endif
#else
  /* ------------------------------------------------------------------------------------
   * Prefer OS-provided atomics, which should be CPU-independent and
   * which should work regardless of the compiler's inline assembly support
   * ------------------------------------------------------------------------------------ */
  #if defined(AIX)
      #include <sys/atomic_op.h>
      typedef struct { volatile int ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (fetch_and_add((atomic_p)&((p)->ctr),1))
      #define gasneti_atomic_decrement(p) (fetch_and_add((atomic_p)&((p)->ctr),-1))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) \
                                          (fetch_and_add((atomic_p)&((p)->ctr),-1) == 1) /* TODO */
  #elif defined(IRIX)
      #include <mutex.h>
      typedef __uint32_t gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (test_then_add32((p),1))
      #define gasneti_atomic_decrement(p) (test_then_add32((p),(uint32_t)-1))
      #define gasneti_atomic_read(p)      (*(p))
      #define gasneti_atomic_set(p,v)     (*(p) = (v))
      #define gasneti_atomic_init(v)      (v)
      #define gasneti_atomic_decrement_and_test(p) \
                                          (add_then_test32((p),(uint32_t)-1) == 0) 
  #elif defined(__MTA__)
      /* use MTA intrinsics */
      typedef int64_t gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (int_fetch_add((p),1))
      #define gasneti_atomic_decrement(p) (int_fetch_add((p),-1))
      #define gasneti_atomic_read(p)      (*(p))
      #define gasneti_atomic_set(p,v)     (*(p) = (v))
      #define gasneti_atomic_init(v)      (v)
      #define gasneti_atomic_decrement_and_test(p) \
                                          (int_fetch_add((p),-1) == 1) 
  #elif 0 && defined(SOLARIS)
      /* $%*(! Solaris has atomic functions in the kernel but refuses to expose them
         to the user... after all, what application would be interested in performance? */
      #include <sys/atomic.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (atomic_add_32((uint32_t *)&((p)->ctr),1))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
  #elif defined(CYGWIN)
      #include <windows.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) InterlockedIncrement((LONG *)&((p)->ctr))
      #define gasneti_atomic_decrement(p) InterlockedDecrement((LONG *)&((p)->ctr))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) \
                                          (InterlockedDecrement((LONG *)&((p)->ctr)) == 0)
  #elif defined(LINUX) && !defined(BROKEN_LINUX_ASM_ATOMIC_H) && \
      (defined(CONFIG_SMP) || defined(GASNETI_UNI_BUILD))
      /* some versions of the linux kernel ship with a broken atomic.h
         Disable using this code if this is a gasnet-smp build and the 
         linux/config.h settings disagree (due to system config problem or 
         cross-compiling on a uniprocessor frontend for smp nodes)
       */
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
      #define gasneti_atomic_increment(p) atomic_inc(p)
      #define gasneti_atomic_decrement(p) atomic_dec(p)
      #define gasneti_atomic_read(p)      atomic_read(p)
      #define gasneti_atomic_set(p,v)     atomic_set(p,v)
      #define gasneti_atomic_init(v)      ATOMIC_INIT(v)
      #define gasneti_atomic_decrement_and_test(p) \
                                          atomic_dec_and_test(p)
      #define GASNETI_USING_LINUX_ASM_HEADERS 1
  /* ------------------------------------------------------------------------------------
   * No OS-provided atomics, so try to provide our own, based on the CPU and compiler 
   * support for inline assembly code
   * ------------------------------------------------------------------------------------ */
  #elif defined(__i386__) || defined(__x86_64__) /* x86 and Athlon/Opteron */
    #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__PATHCC__)
      #ifdef GASNETI_UNI_BUILD
        #define GASNETI_LOCK ""
      #else
        #define GASNETI_LOCK "lock\n\t"
      #endif
      #if defined(__PATHCC__)
        /* Pathscale optimizer is buggy and fails to clobber memory output location correctly
           unless we include an extraneous full memory clobber 
         */
        #define GASNETI_ATOMIC_MEM_CLOBBER ,"memory"
      #else
        #define GASNETI_ATOMIC_MEM_CLOBBER
      #endif
      typedef struct { volatile int ctr; } gasneti_atomic_t;
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      GASNET_INLINE_MODIFIER(gasneti_atomic_increment)
      void gasneti_atomic_increment(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "incl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr)
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      GASNET_INLINE_MODIFIER(gasneti_atomic_decrement)
      void gasneti_atomic_decrement(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "decl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr) 
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      GASNET_INLINE_MODIFIER(gasneti_atomic_decrement_and_test)
      int gasneti_atomic_decrement_and_test(gasneti_atomic_t *v) {
          unsigned char c;
          __asm__ __volatile__(
	          GASNETI_LOCK "decl %0\n\tsete %1"
	          : "=m" (v->ctr), "=mq" (c)
	          : "m" (v->ctr) 
                  : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
          return (c != 0);
      }
    #else
      #error unrecognized x86 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__ia64__) || defined(__ia64) /* Itanium */
    #if defined(__INTEL_COMPILER)
      /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
      #include <ia64intrin.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) _InterlockedIncrement((volatile int *)&((p)->ctr))
      #define gasneti_atomic_decrement(p) _InterlockedDecrement((volatile int *)&((p)->ctr))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) \
                                          (_InterlockedDecrement((volatile int *)&((p)->ctr)) == 0)
    #elif defined(__GNUC__)
      #if GASNET_DEBUG
        #include <stdio.h>
        #include <stdlib.h>
        #define GASNETI_CMPXCHG_BUGCHECK_DECL  int _cmpxchg_bugcheck_count = 128;
        #define GASNETI_CMPXCHG_BUGCHECK(v) do {                                         \
            if (_cmpxchg_bugcheck_count-- <= 0) {                                        \
              void *ip;                                                                  \
              asm ("mov %0=ip" : "=r"(ip));                                              \
              fprintf(stderr,"CMPXCHG_BUGCHECK: stuck at %p on word %p\n", ip, (v));     \
              abort();                                                                   \
            }                                                                            \
          } while (0)
      #else
        #define GASNETI_CMPXCHG_BUGCHECK_DECL
        #define GASNETI_CMPXCHG_BUGCHECK(v)  ((void)0)
      #endif

      GASNET_INLINE_MODIFIER(gasneti_cmpxchg)
      int32_t gasneti_cmpxchg(int32_t volatile *ptr, int32_t oldval, int32_t newval) {                                                                                      \
        int64_t _o_, _r_;
         _o_ = (int64_t)oldval;
         __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(_o_));
         __asm__ __volatile__ ("mf; cmpxchg4.acq %0=[%1],%2,ar.ccv"
                                : "=r"(_r_) : "r"(ptr), "r"(newval) : "memory");
        return (int32_t) _r_;
      }
      GASNET_INLINE_MODIFIER(gasneti_atomic_addandfetch_32)
      int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
        int32_t oldctr, newctr;
        GASNETI_CMPXCHG_BUGCHECK_DECL

        do {
          GASNETI_CMPXCHG_BUGCHECK(v);
          oldctr = *v;
          newctr = oldctr + op;
        } while (gasneti_cmpxchg(v, oldctr, newctr) != oldctr);
        return newctr;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 0)
    #elif defined(__HP_cc) || defined(__HP_aCC) /* HP C/C++ Itanium intrinsics */
      #include <machine/sys/inline.h>
      /* legal values for imm are -16, -8, -4, -1, 1, 4, 8, and 16 
         returns *old* value */
      #define gasneti_atomic_addandfetch_32(ptr, imm) \
         _Asm_fetchadd(_FASZ_W, _SEM_ACQ,             \
                       ptr, imm,                      \
                       _LDHINT_NONE, (_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE))
      GASNET_INLINE_MODIFIER(gasneti_cmpxchg)
      int32_t gasneti_cmpxchg(int32_t volatile *ptr, int32_t oldval, int32_t newval) {                                                                                      \
        register int64_t _r_;
        _Asm_mov_to_ar(_AREG_CCV, (int64_t)oldval);
        _r_ = _Asm_cmpxchg(_SZ_W, _SEM_ACQ, 
                           ptr, newval, 
                           _LDHINT_NONE, (_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE));
        return (int32_t) _r_;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 1)
    #else
      #error unrecognized Itanium compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__alpha__) || defined(__alpha) /* DEC Alpha */
    #if defined(__GNUC__)
      GASNET_INLINE_MODIFIER(gasneti_atomic_addandfetch_32)
      int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t temp;
        register int32_t result;
        __asm__ __volatile__(
          "1: \n\t"
          "ldl_l %0, 0(%2)\n\t"
          "addl %0, %3, %0\n\t"
          "mov %0, %1\n\t"
          "stl_c %0, 0(%2)\n\t"
          "beq %0, 1b\n\t"
          "nop\n"
          : "=&r" (temp), "=&r" (result) /* outputs */
          : "r" (addr), "r" (op)         /* inputs */
          : "memory", "cc");             /* kills */
        return result;
      }
     typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
     #define gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
     #define gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
     #define gasneti_atomic_read(p)      ((p)->ctr)
     #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
     #define gasneti_atomic_init(v)      { (v) }
     #define gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 0)
    #elif (defined(__DECC) || defined(__DECCXX)) && defined(__osf__)
       /* Compaq C / OSF atomics are compiler built-ins */
       #include <sys/machine/builtins.h>
       typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
       #define gasneti_atomic_increment(p) (__ATOMIC_INCREMENT_LONG(&((p)->ctr)))
       #define gasneti_atomic_decrement(p) (__ATOMIC_DECREMENT_LONG(&((p)->ctr)))
       #define gasneti_atomic_read(p)      ((p)->ctr)
       #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
       #define gasneti_atomic_init(v)      { (v) }
       #define gasneti_atomic_decrement_and_test(p) \
                                          (__ATOMIC_DECREMENT_LONG(&((p)->ctr)) == 1)
    #else
      #error unrecognized Alpha compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_SPARCV9) /* SPARC v9 */
    #if defined(__GNUC__)
      static __inline__ int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
        /* SPARC v9 architecture manual, p.333 
         * This function requires the cas instruction in Sparc V9, and therefore gcc -mcpu=ultrasparc
         */
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t oldval;
        register int32_t newval;
        __asm__ __volatile__ ( 
          "0:\t" 
          "membar #StoreLoad | #LoadLoad    \n\t" /* complete all previous ops before next load */
          "ld       [%2],%0 \n\t"    /* oldval = *addr; */
          "add      %0,%3,%1 \n\t"   /* newval = oldval + op; */
          "cas      [%2],%0,%1 \n\t" /* if (*addr == oldval) { *addr = newval; }  newval = *addr; */
          "cmp      %0, %1 \n\t"     /* check if newval == oldval (swap succeeded) */
          "bne      0b \n\t"         /* otherwise, try again */
          "membar #StoreLoad | #StoreStore    \n\t" /* complete previous cas store before all subsequent ops */
          : "=&r"(oldval), "=&r"(newval)
          : "r" (addr), "rn"(op) 
          : "memory");
        return oldval;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 1)

    #else
      #error unrecognized Sparc v9 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__crayx1) /* This works on X1, but NOT the T3E */
    #include <intrinsics.h>
    typedef volatile long gasneti_atomic_t;
    /* man pages for atomic ops claim gsync is required for using atomic ops,
       but it's unclear when exactly it is required for our purposes - 
       experimentally determined using testtools that it's only required after the amo.
       Note gsync call MUST be _gsync(0x1) - the manpage docs are gratuitiously wrong, 
       everything else gives a bus error
     */
    #define gasneti_atomic_presync()  ((void)0)
    #define gasneti_atomic_postsync()  _gsync(0x1)

    #define gasneti_atomic_increment(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)1),gasneti_atomic_postsync())
    #define gasneti_atomic_decrement(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)-1),gasneti_atomic_postsync())
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
    GASNET_INLINE_MODIFIER(gasneti_atomic_decrement_and_test)
    int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) {
       int retval;
       gasneti_atomic_presync();
       retval = (_amo_afadd((p),(long)-1) == 1);
       gasneti_atomic_postsync();
       return retval;
    }
  /* ------------------------------------------------------------------------------------ */
  #elif defined(_SX) /* NEC SX-6 */
    /* these are disabled for now because they don't link */
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
   #if 0
    #include <sys/mplock.h>
    #define gasneti_atomic_increment(p) (atomic_add4(((p)->ctr),1))
    #define gasneti_atomic_decrement(p) (atomic_add4(((p)->ctr),-1))
    #define gasneti_atomic_read(p)      (atomic_read4((p)->ctr))
    #define gasneti_atomic_set(p,v)     (atomic_set4((p)->ctr,(v)))
    #define gasneti_atomic_init(v)      { (v) }
    #define gasneti_atomic_decrement_and_test(p) \
                                        (atomic_add4(((p)->ctr),-1) == 0)
   #else
    #define gasneti_atomic_increment(p) (muadd(&((p)->ctr),1))
    #define gasneti_atomic_decrement(p) (muadd(&((p)->ctr),-1))
    #define gasneti_atomic_read(p)      (muget(&((p)->ctr)))
    #define gasneti_atomic_set(p,v)     (muset(&((p)->ctr),(v)))
    #define gasneti_atomic_init(v)      { (v) }
    #define gasneti_atomic_decrement_and_test(p) \
                                        (muadd(&((p)->ctr),-1) == 0)
   #endif
  /* ------------------------------------------------------------------------------------ */
  #elif (defined(__APPLE__) && defined(__MACH__) && defined(__ppc__)) || (defined(LINUX) && defined(__PPC__))
    /* PowerPC
     * (__APPLE__) && __MACH__ && __ppc__) == OS/X, Darwin
     * (LINUX && __PPC__) == Linux
     */
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
	"4c00012c"	/*    isync			*/ \
      }
      #pragma reg_killed_by gasneti_atomic_inc_32 cr0, gr4

      static void gasneti_atomic_dec_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_dec_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"3884ffff"	/*    subi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
	"4c00012c"	/*    isync			*/ \
      }
      #pragma reg_killed_by gasneti_atomic_dec_32 cr0, gr4

      static int32_t gasneti_atomic_decandfetch_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_decandfetch_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"3884ffff"	/*    subi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
	"4c00012c"	/*    isync			*/ \
	"7c832378"	/*    mr	r3,r4		*/ \
	/* RETURN in r3 = result after dec */ \
      }
      #pragma reg_killed_by gasneti_atomic_decandfetch_32 cr0, gr4

      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (gasneti_atomic_inc_32(&((p)->ctr)))
      #define gasneti_atomic_decrement(p) (gasneti_atomic_dec_32(&((p)->ctr)))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) (gasneti_atomic_decandfetch_32(&((p)->ctr)) == 0)
    #else
      static __inline__ int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t result;
        __asm__ __volatile__ ( 
          "0:\t" 
          "lwarx    %0,0,%1 \n\t" 
          "add%I2   %0,%0,%2 \n\t"
          "stwcx.   %0,0,%1 \n\t"
          "bne-     0b \n\t" 
          "isync"
          : "=&b"(result)		/* constraint b = not in r0 */
          : "r" (addr), "Ir"(op) 
          : "cr0", "memory");
        return result;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 0)
    #endif
  #else
    #error Unrecognized platform - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
  #endif
#endif
/* ------------------------------------------------------------------------------------ */
/* GASNet weak atomics - these operations are guaranteed to be atomic if and only if 
    the sole updates are from the host processor(s), with no signals involved.
   if !GASNETI_THREADS, they compile away to a non-atomic counter
    thereby saving the overhead of unnecessary atomic-memory CPU instructions. 
   Otherwise, they expand to regular gasneti_atomic_t's
 */
#if GASNETI_THREADS || defined(GASNETI_FORCE_TRUE_WEAKATOMICS)
  typedef gasneti_atomic_t gasneti_weakatomic_t;
  #define gasneti_weakatomic_init(v)                gasneti_atomic_init(v)
  #define gasneti_weakatomic_set(p,v)               gasneti_atomic_set(p,v)
  #define gasneti_weakatomic_read(p)                gasneti_atomic_read(p)
  #define gasneti_weakatomic_increment(p)           gasneti_atomic_increment(p)
  #define gasneti_weakatomic_decrement(p)           gasneti_atomic_decrement(p)
  #define gasneti_weakatomic_decrement_and_test(p)  gasneti_atomic_decrement_and_test(p) 
#else
  typedef volatile int gasneti_weakatomic_t;
  #define gasneti_weakatomic_init(v)                (v)
  #define gasneti_weakatomic_set(p,v)               (*(p) = (v))
  #define gasneti_weakatomic_read(p)                (*(p))
  #define gasneti_weakatomic_increment(p)           ((*p)++)
  #define gasneti_weakatomic_decrement(p)           ((*p)--)
  #define gasneti_weakatomic_decrement_and_test(p)  (!(--(*p))) 
#endif

/* ------------------------------------------------------------------------------------ */

#endif
