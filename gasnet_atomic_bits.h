/*  $Archive:: /Ti/GASNet/gasnet_atomicops.h                               $
 *     $Date: 2003/10/16 21:44:04 $
 * $Revision: 1.21 $
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

#if defined(SOLARIS) || /* SPARC seems to have no atomic ops */ \
    defined(CRAYT3E) || /* TODO: no atomic ops on T3e? */       \
    defined(HPUX)    || /* HPUX seems to have no atomic ops */  \
    (defined(__PGI) && defined(BROKEN_LINUX_ASM_ATOMIC_H)) || /* haven't implemented atomics for PGI */ \
    (defined(OSF) && !defined(__DECC)) ||  /* OSF atomics are compiler built-ins */ \
    (defined(__MACH__) && defined(__APPLE__)) || /* we careth not about performance on OSX */ \
    (defined(__INTEL_COMPILER) && defined(__ia64__)) /* IA64 Intel compiler doesn't support asm (contrary to docs) */
  #define GASNETI_USE_GENERIC_ATOMICOPS
#endif

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  /* a very slow but portable implementation of atomic ops */
  typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
  #define gasneti_atomic_read(p)      ((p)->ctr)
  #define gasneti_atomic_init(v)      { (v) }
  #ifdef _INCLUDED_GASNET_H
    extern gasnet_hsl_t gasneti_atomicop_lock;

    #define gasneti_atomic_set(p,v)     (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                         (p)->ctr = (v),                          \
                                         gasnet_hsl_unlock(&gasneti_atomicop_lock))
    #define gasneti_atomic_increment(p) (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                        ((p)->ctr)++,                             \
                                         gasnet_hsl_unlock(&gasneti_atomicop_lock))
    #define gasneti_atomic_decrement(p) (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                        ((p)->ctr)--,                             \
                                         gasnet_hsl_unlock(&gasneti_atomicop_lock))
    GASNET_INLINE_MODIFIER(gasneti_atomic_decrement_and_test)
    int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) {
      uint32_t newval;
      gasnet_hsl_lock(&gasneti_atomicop_lock);
      newval = p->ctr - 1;
      p->ctr = newval;
      gasnet_hsl_unlock(&gasneti_atomicop_lock);
      return (newval == 0);
    }
  #elif defined(_REENTRANT) || defined(_THREAD_SAFE) || \
        defined(PTHREAD_MUTEX_INITIALIZER) ||           \
        defined(HAVE_PTHREAD) || defined(HAVE_PTHREAD_H)
    /* a version for pthreads which is independent of GASNet HSL's */
    #include <pthread.h>
    pthread_mutex_t gasneti_atomicop_mutex = PTHREAD_MUTEX_INITIALIZER;

    #define gasneti_atomic_set(p,v)     (pthread_mutex_lock(&gasneti_atomicop_mutex), \
                                         (p)->ctr = (v),                              \
                                         pthread_mutex_unlock(&gasneti_atomicop_mutex))
    #define gasneti_atomic_increment(p) (pthread_mutex_lock(&gasneti_atomicop_mutex), \
                                        ((p)->ctr)++,                                 \
                                         pthread_mutex_unlock(&gasneti_atomicop_mutex))
    #define gasneti_atomic_decrement(p) (pthread_mutex_lock(&gasneti_atomicop_mutex), \
                                        ((p)->ctr)--,                                 \
                                         pthread_mutex_unlock(&gasneti_atomicop_mutex))
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
    #define PTHREAD_MUTEX_INITIALIZER ERROR: include pthread.h before gasnet_tools.h
    int pthread_mutex_lock; /* attempt to generate a linker error if pthreads are in use */
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_increment(p) (((p)->ctr)++)
    #define gasneti_atomic_decrement(p) (((p)->ctr)--)
    #define gasneti_atomic_decrement_and_test(p) ((--((p)->ctr)) == 0)
  #endif
#else
  #if defined(LINUX)
    #ifdef BROKEN_LINUX_ASM_ATOMIC_H
      /* some versions of the linux kernel ship with a broken atomic.h
         this code based on a non-broken version of the header */
      #ifdef GASNETI_UNI_BUILD
        #define GASNETI_LOCK ""
      #else
        #define GASNETI_LOCK "lock ; "
      #endif

      #ifndef __i386__
        #error you have broken Linux system headers and a broken CPU. barf...
      #endif

      typedef struct { volatile int counter; } gasneti_atomic_t;
      #define gasneti_atomic_read(p)      ((p)->counter)
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_set(p,v)     ((p)->counter = (v))
      GASNET_INLINE_MODIFIER(gasneti_atomic_increment)
      void gasneti_atomic_increment(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "incl %0"
                :"=m" (v->counter)
                :"m" (v->counter));
      }
      GASNET_INLINE_MODIFIER(gasneti_atomic_decrement)
      void gasneti_atomic_decrement(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "decl %0"
                :"=m" (v->counter)
                :"m" (v->counter));
      }
      GASNET_INLINE_MODIFIER(gasneti_atomic_decrement_and_test)
      int gasneti_atomic_decrement_and_test(gasneti_atomic_t *v) {
	  unsigned char c;
	  __asm__ __volatile__(
		  GASNETI_LOCK "decl %0; sete %1"
		  :"=m" (v->counter), "=qm" (c)
		  :"m" (v->counter) : "memory");
	  return (c != 0);
      }
    #else
      #ifdef __alpha__
        /* work-around for a puzzling header bug in alpha Linux */
        #define extern static
      #endif
      #include <asm/atomic.h>
      #ifdef __alpha__
        #undef extern
      #endif
      typedef atomic_t gasneti_atomic_t;
      #define gasneti_atomic_increment(p) atomic_inc(p)
      #define gasneti_atomic_decrement(p) atomic_dec(p)
      #define gasneti_atomic_read(p)      atomic_read(p)
      #define gasneti_atomic_set(p,v)     atomic_set(p,v)
      #define gasneti_atomic_init(v)      ATOMIC_INIT(v)
      #define gasneti_atomic_decrement_and_test(p) \
                                          atomic_dec_and_test(p)
    #endif
  #elif defined(FREEBSD)
    #include <machine/atomic.h>
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) atomic_add_int(&((p)->ctr),1)
    #define gasneti_atomic_decrement(p) atomic_subtract_int(&((p)->ctr),1)
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
    /* FreeBSD is lacking atomic ops that return a value */
    #ifdef __i386__
      GASNET_INLINE_MODIFIER(_gasneti_atomic_decrement_and_test)
      int _gasneti_atomic_decrement_and_test(volatile uint32_t *ctr) {                                                       \
	unsigned char c;
        __asm__ __volatile__(
	        _STRINGIFY(MPLOCKED) "decl %0; sete %1"
	        :"=m" (*ctr), "=qm" (c)
	        :"m" (*ctr) : "memory");
        return (c != 0);
      }
    #else
      #error need to implement gasneti_atomic_decrement_and_test for FreeBSD on your CPU
    #endif
    #define gasneti_atomic_decrement_and_test(p) \
           _gasneti_atomic_decrement_and_test(&((p)->ctr))
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
  #elif defined(AIX)
    #include <sys/atomic_op.h>
    typedef struct { volatile int ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (fetch_and_add((atomic_p)&((p)->ctr),1))
    #define gasneti_atomic_decrement(p) (fetch_and_add((atomic_p)&((p)->ctr),-1))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
    #define gasneti_atomic_decrement_and_test(p) \
                                        (fetch_and_add((atomic_p)&((p)->ctr),-1) == 1) /* TODO */
  #elif defined(OSF)
   #if 1
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
    #include <sys/systm.h>
    typedef struct { volatile int ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (atomic_incl(&((p)->ctr)))
    #define gasneti_atomic_decrement(p) (atomic_decl(&((p)->ctr)))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
    #define gasneti_atomic_decrement_and_test(p) \
                                        (atomic_decl(&((p)->ctr)) == 1)
   #endif
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
  #elif 0 && defined(SOLARIS)
    /* $%*(! Solaris has atomic functions in the kernel but refuses to expose them
       to the user... after all, what application would be interested in performance? */
    #include <sys/atomic.h>
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (atomic_add_32((uint32_t *)&((p)->ctr),1))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
    #if 0
      /* according to the "Alpha Architecture Handbook", the following code 
         should work on alpha, but the assembler chokes on the load-locked 
         with "unknown opcode", for both GNU and Sun assemblers. Alpha sucks.
       */
      static __inline__ void atomic_add_32(int32_t *v, int32_t op) {
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t temp;
        __asm__ __volatile__( 
          "retry: \n"
          "ldl_l %0, %1\n" 
          "addl %0, %2, %0\n"
          "stl_c %0, %1\n"
          "beq %0, retry\n"
          "nop\n"
          : "=&r" (temp)          /* outputs */
          : "r" (addr), "r" (op)  /* inputs */
          : "memory", "cc");      /* kills */
        assert(temp);
      }
    #endif
  #else
    #error Need to implement atomic increment/decrement for this platform...
  #endif
#endif
/* ------------------------------------------------------------------------------------ */
/* portable memory barrier support */

/* a local memory barrier - ensure all previous loads/stores to local mem
   from this proc are globally completed across this SMP before issuing
   any subsequent loads or stores
   (i.e. all loads issued from any CPU subsequent to this call
      returning will see the new value for any previously issued
      stores from this proc)
   This is sometimes referred to as a full memory "fence" 
   More Info: http://gee.cs.oswego.edu/dl/jmm/cookbook.html
 */
#ifdef __GNUC__
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__INTEL_COMPILER)
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__PGI) /* note this requires compiler flag -Masmkeyword */
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(__digital__)
  #include <c_asm.h>
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(MIPSPRO_COMPILER)
  #define GASNETI_ASM(mnemonic)  /* TODO: broken - doesn't have inline assembly */
#elif defined(__SUNPRO_C)
  #define GASNETI_ASM(mnemonic)  __asm(mnemonic)
#elif defined(__xlC__)  
  #define GASNETI_ASM(mnemonic)  !!! error !!! /* not supported or used */
#else
  #error "Don't know how to use inline assembly for your compiler"
#endif

#if defined(__xlC__)  
/* VisualAge C compiler (mpcc_r) has no support for inline symbolic assembly
 * you have to hard-code the opcodes in a pragma that defines an assembly 
 * function - see /usr/include/sys/atomic_op.h on AIX for examples
 * opcodes can be aquired by placing the mnemonics in inline.s and running:
 * as -sinline.lst inline.s
 */ 
#pragma mc_func _gasneti_do_sync { \
  "7c0004ac" /* sync (same opcode used for dcs)*/ \
  "4c00012c" /* isync (instruction sync to squash speculative loads) */ \
}
#pragma reg_killed_by _gasneti_do_sync
#endif

#if defined(__sparc__) || defined(__sparc) || defined(sparc)
 GASNET_INLINE_MODIFIER(gasneti_local_membar)
 void gasneti_local_membar(void) {
   GASNETI_ASM("stbar"); /* SPARC store barrier */
 }
#elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
 GASNET_INLINE_MODIFIER(gasneti_local_membar)
 void gasneti_local_membar(void) {
   GASNETI_ASM("sync");  /* MIPS II+ memory barrier */ 
 }
#elif defined(_PA_RISC1_1) /* HP PA-RISC */
 GASNET_INLINE_MODIFIER(gasneti_local_membar)
 void gasneti_local_membar(void) {
   GASNETI_ASM("SYNC");  /* PA RISC load/store ordering */ 
 }
#elif defined(__i386__) || defined(__i386) || defined(i386) || \
      defined(__i486__) || defined(__i486) || defined(i486) || \
      defined(__i586__) || defined(__i586) || defined(i586) || \
      defined(__i686__) || defined(__i686) || defined(i686)
 #if defined(GASNETI_UNI_BUILD)
   /* Prevent compiler from reordering across this point. */
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     GASNETI_ASM("");
   }
 #else
   /* Prevent both compiler and the CPU from reordering across this point.
    *
    * Note that MMX, SSE and SSE2 instructions which move memory are *NOT* ordered by
    * this sequence, and must instead have the appropriate [lsm]fence instruction(s).
    * Authors of MMX-based memcpy and similar code must therefore take care not to
    * rely on gasneti_local_membar() in conjunction with these instruction sets.
    */
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     /* The instruction here can be any locked read-modify-write operation.
      * This one is chosen because it does not change any registers and is
      * available on all the Intel and clone CPUs.
      */
     #if defined(__PGI)
       GASNETI_ASM("lock; addl $0,0(%esp)");
     #else
       GASNETI_ASM("lock; addl $0,0(%%esp)");
     #endif
   }
 #endif
#elif defined(__ia64__) /* Itanium */
 #if defined(GASNETI_UNI_BUILD)
   /* Prevent compiler from reordering across this point. */
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     GASNETI_ASM("");
   }
 #else
   /* mf may cause an illegal instruction trap on uniprocessor kernel */
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     GASNETI_ASM("mf");
   }
 #endif
#elif defined(_POWER) /* IBM SP POWER2, POWER3 */
 #ifdef __xlC__
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     _gasneti_do_sync(); 
   }
 #else
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     GASNETI_ASM("dcs");
   }
 #endif
#elif defined(_POWERPC) || defined(__POWERPC__) /* __POWERPC__ == OSX */
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     GASNETI_ASM("sync");
   }
#elif defined(__alpha) && defined(__osf__)
 #if 1
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     GASNETI_ASM("mb");
   }
 #else 
   #include <machine/builtins.h>
   #define gasneti_local_membar() __MB() /* only available as compaq C built-in */
 #endif
#elif defined(_CRAYT3E)
   /* don't have shared memory on T3E - does this take care of e-regs too? */
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     static int volatile x;
     x = 1;
   }
#else
 #error unknown CPU - dont know how to do a local memory barrier for your CPU/OS
#endif

/* ------------------------------------------------------------------------------------ */

#endif
