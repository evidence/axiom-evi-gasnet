/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_membar.h,v $
 *     $Date: 2005/02/23 01:03:58 $
 * $Revision: 1.61 $
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

#if defined(__sparc)   || /* SPARC seems to have no atomic ops */            \
    defined(CRAYT3E)   || /* T3E seems to have no atomic ops */              \
    defined(_SX)       || /* NEC SX-6 atomics not available to user code? */ \
    defined(__hppa)    || /* PA-RISC seems to have no atomic ops */          \
    defined(__crayx1)  || /* X1 atomics currently broken */                  \
    (defined(__PGI) && defined(BROKEN_LINUX_ASM_ATOMIC_H)) /* haven't implemented atomics for PGI */
  #define GASNETI_USE_GENERIC_ATOMICOPS
#endif
/* misc rerequisites to detection logic below */
#if defined(LINUX)
  #include <linux/config.h>
#endif

/* ------------------------------------------------------------------------------------ */
#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  /* a very slow but portable implementation of atomic ops */
  typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
  #define gasneti_atomic_read(p)      ((p)->ctr)
  #define gasneti_atomic_init(v)      { (v) }
  #ifdef _INCLUDED_GASNET_H
    extern void *gasneti_patomicop_lock; /* bug 693: avoid header dependency cycle */

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
    /* a version for pthreads which is independent of GASNet HSL's */
    #include <pthread.h>
    pthread_mutex_t gasneti_atomicop_mutex = PTHREAD_MUTEX_INITIALIZER;

    #define gasneti_atomic_set(p,v) do {               \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        (p)->ctr = (v);                                \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define gasneti_atomic_increment(p) do {           \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)++;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define gasneti_atomic_decrement(p) do {           \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)--;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    GASNET_INLINE_MODIFIER(gasneti_atomic_decrement_and_test)
    int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) {
      uint32_t newval;
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      newval = p->ctr - 1;
      p->ctr = newval;
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return (newval == 0);
    }
  #else
    /* only one thread - everything atomic by definition */
    /* attempt to generate a compile error if pthreads actually are in use */
    #define PTHREAD_MUTEX_INITIALIZER ERROR_include_pthread_h_before_gasnet_tools_h
    extern int pthread_mutex_lock; 
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_increment(p) (((p)->ctr)++)
    #define gasneti_atomic_decrement(p) (((p)->ctr)--)
    #define gasneti_atomic_decrement_and_test(p) ((--((p)->ctr)) == 0)
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
    #if defined(__GNUC__) || defined(__INTEL_COMPILER)
      #ifdef GASNETI_UNI_BUILD
        #define GASNETI_LOCK ""
      #else
        #define GASNETI_LOCK "lock ; "
      #endif
      typedef struct { volatile int ctr; } gasneti_atomic_t;
      #define gasneti_atomic_read(p)      ((p)->ctr)
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      GASNET_INLINE_MODIFIER(gasneti_atomic_increment)
      void gasneti_atomic_increment(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "incl %0"
                :"=m" (v->ctr)
                :"m" (v->ctr));
      }
      GASNET_INLINE_MODIFIER(gasneti_atomic_decrement)
      void gasneti_atomic_decrement(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "decl %0"
                :"=m" (v->ctr)
                :"m" (v->ctr));
      }
      GASNET_INLINE_MODIFIER(gasneti_atomic_decrement_and_test)
      int gasneti_atomic_decrement_and_test(gasneti_atomic_t *v) {
          unsigned char c;
          __asm__ __volatile__(
	          GASNETI_LOCK "decl %0; sete %1"
	          :"=m" (v->ctr), "=qm" (c)
	          :"m" (v->ctr) : "memory");
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
  #elif defined(__crayx1) /* This works on X1, but NOT the T3E */
    #include <intrinsics.h>
    typedef volatile long gasneti_atomic_t;
    /* DOB: man pages for atomic ops claim gsync is required for using atomic ops,
       but trying to do so leads to crashes. Using atomic ops without gync gives
       incorrect results (testtools fails)
     */
    #if 1
      #define gasneti_atomic_presync()  ((void)0)
      #define gasneti_atomic_postsync() ((void)0)
    #elif 0
      #define gasneti_atomic_presync()  _gsync(0)
      #define gasneti_atomic_postsync() _gsync(0)
    #else
      #define gasneti_atomic_presync()  _msync_msp(0)
      #define gasneti_atomic_postsync() _msync_msp(0)
    #endif
    #define gasneti_atomic_increment(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)1),gasneti_atomic_postsync())
    #define gasneti_atomic_decrement(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)1),gasneti_atomic_postsync())
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
    GASNET_INLINE_MODIFIER(gasneti_atomic_decrement_and_test)
    int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) {
       int retval;
       gasneti_atomic_presync();
       retval = _amo_afadd((p),(long)-1) == 0;
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
/* portable memory barrier support */

/*
 gasneti_local_wmb:
   A local memory write barrier - ensure all stores to local mem from this thread are
   globally completed across this SMP before issuing any subsequent loads or stores.
   (i.e. all loads issued from any CPU subsequent to this call
      returning will see the new value for any previously issued
      stores from this proc, and any subsequent stores from this CPU
      are guaranteed to become globally visible after all previously issued
      stores from this CPU)
   This must also include whatever is needed to prevent the compiler from reordering
   loads and stores across this point.

 gasneti_local_rmb:
   A local memory read barrier - ensure all subsequent loads from local mem from this thread
   will observe previously issued stores from any CPU which have globally completed.  
   For instance, on the Alpha this ensures
   that queued cache invalidations are processed and on the PPC this discards any loads
   that were executed speculatively.
   This must also include whatever is needed to prevent the compiler from reordering
   loads and stores across this point.
 
 gasneti_local_mb:
   A "full" local memory barrer.  This is equivalent to both a wmb() and rmb().
   All oustanding loads and stores must be completed before any subsequent ones
   may begin.

 gasneti_compiler_fence:
   A barrier to compiler optimizations that would reorder any memory references across
   this point in the code.

  Note that for all five memory barriers, we require only that a given architecture's
  "normal" loads and stores are ordered as required.  "Extended" instructions such as
  MMX, SSE, SSE2, Altivec and vector ISAs on various other machines often bypass some
  or all of the machine's memory hierarchy and therefore may not be ordered by the same
  instructions.  Authors of MMX-based memcpy and similar code must therefore take care
  to add appropriate flushes to their code.

  To reduce duplicated assembly code and needless empty macros the following are the
  default behaviors unless a given arch/compiler defines something else.
   + gasneti_compiler_fence() defaults to an empty "volatile" asm section
   + gasneti_local_wmb() is implemented on all architectures
   + gasneti_local_rmb() defaults to just a compiler fence, as only a few architectures
       need more than this
   + gasneti_local_mb() defaults to { gasneti_local_wmb(); gasneti_local_rmb(); }.
       Only a few architectures (notable Alpha) can do this less expensively.

  For more info on memory barriers: http://gee.cs.oswego.edu/dl/jmm/cookbook.html
 */
#ifdef __GNUC__
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__INTEL_COMPILER)
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__PGI) /* note this requires compiler flag -Masmkeyword */
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(__DECC) || defined(__DECCXX)
  #include <c_asm.h>
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(MIPSPRO_COMPILER)
  #define GASNETI_ASM(mnemonic)  /* TODO: broken - doesn't have inline assembly */
#elif defined(__SUNPRO_C) /* Sun C works, Sun C++ lacks inline assembly support (man inline) */
  #define GASNETI_ASM(mnemonic)  __asm(mnemonic)
#elif defined(_SX)  
  #define GASNETI_ASM(mnemonic)  asm(mnemonic)
#elif defined(__HP_cc) /* HP C */
  #define GASNETI_ASM(mnemonic)  _asm(mnemonic)
#elif defined(__HP_aCC)
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(__xlC__)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(_CRAY)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(__MTA__)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#else
  #error "Don't know how to use inline assembly for your compiler"
#endif


#if defined(__sparc__) || defined(__sparc) || defined(sparc)
 GASNET_INLINE_MODIFIER(gasneti_local_wmb)
 void gasneti_local_wmb(void) {
   GASNETI_ASM("stbar"); /* SPARC store barrier */
 }
#elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
 GASNET_INLINE_MODIFIER(gasneti_local_wmb)
 void gasneti_local_wmb(void) {
   GASNETI_ASM("sync");  /* MIPS II+ memory barrier */ 
 }
#elif defined(_PA_RISC1_1) || defined(__hppa) /* HP PA-RISC */
 GASNET_INLINE_MODIFIER(gasneti_local_wmb)
 void gasneti_local_wmb(void) {
   GASNETI_ASM("SYNC");  /* PA RISC load/store ordering */ 
 }
 #if defined(__HP_cc) /* HP C doesn't like an empty asm statement */
   #define gasneti_compiler_fence() _asm("OR",0,0,0) /* NOP */
 #endif
#elif defined(__i386__) || defined(__i386) || defined(i386) || \
      defined(__i486__) || defined(__i486) || defined(i486) || \
      defined(__i586__) || defined(__i586) || defined(i586) || \
      defined(__i686__) || defined(__i686) || defined(i686)
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* The instruction here can be any locked read-modify-write operation.
      * This one is chosen because it does not change any registers and is
      * available on all the Intel and clone CPUs.  Also, since it touches
      * only the stack, it is highly unlikely to result in extra coherence
      * traffic.
      */
     #if defined(__PGI)
       GASNETI_ASM("lock; addl $0,0(%esp)");
     #else
       GASNETI_ASM("lock; addl $0,0(%%esp)");
     #endif
   }
#elif defined(__x86_64__) /* Athlon/Opteron */
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("mfence");
   }
#elif defined(__ia64__) || defined(__ia64) /* Itanium */
   #ifdef __INTEL_COMPILER
      /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
      #include <ia64intrin.h>
      #define gasneti_compiler_fence() \
             __memory_barrier() /* compiler optimization barrier */
      #define gasneti_local_wmb() do {      \
        gasneti_compiler_fence();           \
        __mf();  /* memory fence instruction */  \
      } while (0)
   #elif defined(__HP_cc) || defined(__HP_aCC)
      #include <machine/sys/inline.h>
      /* HP compilers have no inline assembly on Itanium - use intrinsics */
      #define gasneti_local_wmb() _Asm_mf((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE))
      #define gasneti_compiler_fence() \
         _Asm_sched_fence((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE)) 
   #else
      /* mf may cause an illegal instruction trap on uniprocessor kernel */
      GASNET_INLINE_MODIFIER(gasneti_local_wmb)
      void gasneti_local_wmb(void) {
        GASNETI_ASM("mf");
      }
   #endif
#elif defined(_POWER) || (defined(__APPLE__) && defined(__MACH__) && defined(__ppc__)) || (defined(LINUX) && defined(__PPC__))
 /* (_POWER) == IBM SP POWER[234]
  * (__APPLE__ && __MACH__ && __ppc__) == Darwin, OS/X
  * (LINUX && __PPC__) == Linux
  */
 #ifdef __xlC__
   /* VisualAge C compiler (mpcc_r) has no support for inline symbolic assembly
    * you have to hard-code the opcodes in a pragma that defines an assembly 
    * function - see /usr/include/sys/atomic_op.h on AIX for examples
    * opcodes can be aquired by placing the mnemonics in inline.s and running:
    * as -sinline.lst inline.s
    */ 
   #pragma mc_func _gasneti_do_wmb { \
     "7c0004ac" /* sync (same opcode used for dcs) */ \
   }
   #pragma reg_killed_by _gasneti_do_wmb
   #define gasneti_local_wmb() _gasneti_do_wmb()

   #pragma mc_func _gasneti_do_rmb { \
     "4c00012c" /* isync (instruction sync to squash speculative loads) */ \
   }
   #pragma reg_killed_by _gasneti_do_rmb
   #define gasneti_local_rmb() _gasneti_do_rmb()

   #pragma mc_func _gasneti_do_compilerfence { "" }
   #pragma reg_killed_by _gasneti_do_compilerfence
   #define gasneti_compiler_fence() _gasneti_do_compilerfence()
 #else
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("sync");
   }
   GASNET_INLINE_MODIFIER(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("isync");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
 #endif
#elif defined(__alpha)
 #if 1 /* tested on OSF1, LINUX, FreeBSD */
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("wmb");
   }
   GASNET_INLINE_MODIFIER(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("mb");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
   GASNET_INLINE_MODIFIER(_gasneti_local_mb)
   void _gasneti_local_mb(void) {
     GASNETI_ASM("mb");
   }
   #define gasneti_local_mb() _gasneti_local_mb()
 #elif defined(__osf__) && 0
   /* Use compaq C built-ins */
   /* Note this is heavier weight than required */
   #include <machine/builtins.h>
   #define gasneti_local_wmb() __MB()
   #define gasneti_local_rmb() __MB()
   #define gasneti_local_mb() __MB()
 #endif
#elif defined(_CRAYT3E) /* Takes care of e-regs also */
   #include <intrinsics.h>
   #define gasneti_local_wmb() _memory_barrier()
   #define gasneti_local_rmb() _memory_barrier()
   #define gasneti_local_mb() _memory_barrier()
   #define gasneti_compiler_fence() do { int volatile x = 0; } while (0)
#elif defined(__crayx1)
   /* Many memory barrier intrinsics on the X1, but none seem to match what we
    * need in a local (scalar-scalar) membar */
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     static int volatile x;
     x = 1;
   }
   #define gasneti_compiler_fence gasneti_local_wmb
#elif defined(__MTA__)
   /* MTA has no caches or write buffers - just need a compiler reordering fence */
   #if 0 /* causes warnings */
     #define gasneti_compiler_fence() (_Pragma("mta fence"))
   #else
     GASNET_INLINE_MODIFIER(_gasneti_compiler_fence)
     void _gasneti_compiler_fence(void) {
       (void)0;
       #pragma mta fence
       (void)0;
     }
     #define gasneti_compiler_fence() _gasneti_compiler_fence()
   #endif
#elif defined(_SX)
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* TODO: probably need more here */
     static int volatile x;
     x = 1;
     /* GASNETI_ASM("nop"); - leads to "FATAL COMPILER ERROR, Unknown statement. c++: Internal Error: Please report." */
   }
#else
 #error unknown CPU - dont know how to do a local memory barrier for your CPU/OS
#endif

/* Default gasneti_compiler_fence() */
#ifndef gasneti_compiler_fence
  #define gasneti_compiler_fence() GASNETI_ASM("")
#endif

/* Default gasneti_local_rmb() */
#ifndef gasneti_local_rmb
  #define gasneti_local_rmb() gasneti_compiler_fence()
#endif

/* Default gasneti_local_wmb() */
#ifndef gasneti_local_wmb
  #define gasneti_local_wmb() gasneti_compiler_fence()
#endif

/* Default gasneti_local_mb() */
#ifndef gasneti_local_mb
  #define gasneti_local_mb() do { gasneti_local_wmb(); gasneti_local_rmb(); } while (0)
#endif

#ifndef gasneti_spinloop_hint
 #ifdef HAVE_X86_PAUSE_INSTRUCTION
   /* Pentium 4 processors get measurably better performance when a "pause" instruction
      is inserted in spin-loops - this instruction is documented as a "spin-loop hint"
      which avoids a memory hazard stall on spin loop exit and reduces power consumption
      Other Intel CPU's treat this instruction as a no-op
   */
   #define gasneti_spinloop_hint() GASNETI_ASM("pause")
 #else
   #define gasneti_spinloop_hint() ((void)0)
 #endif
#endif

/* ------------------------------------------------------------------------------------ */

#endif
