/*  $Archive:: /Ti/GASNet/gasnet_atomicops.h                               $
 *     $Date: 2002/12/26 03:43:15 $
 * $Revision: 1.1 $
 * Description: GASNet header for portable atomic memory operations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_TOOLS_H
  #error This file is not meant to be included directly- clients should include gasnet_tools.h
#endif

#ifndef _GASNET_ATOMICOPS_H
#define _GASNET_ATOMICOPS_H

/* ------------------------------------------------------------------------------------ */
/* portable atomic increment/decrement */

#if defined(SOLARIS) || defined(CRAYT3E) || defined(__PGI) || \
    (defined(OSF) && !defined(__DECC))
  #define GASNETI_USE_GENERIC_ATOMICOPS /* TODO: no atomic ops on T3e? */
#endif

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  /* a very slow but portable implementation of atomic ops */
  extern gasnet_hsl_t gasneti_atomicop_lock;

  typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
  #define gasneti_atomic_read(p)      ((p)->ctr)
  #define gasneti_atomic_init(v)      { (v) }
  #define gasneti_atomic_set(p,v)     (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                       (p)->ctr = (v),                          \
                                       gasnet_hsl_unlock(&gasneti_atomicop_lock))
  #define gasneti_atomic_increment(p) (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                      ((p)->ctr)++,                             \
                                       gasnet_hsl_unlock(&gasneti_atomicop_lock))
  #define gasneti_atomic_decrement(p) (gasnet_hsl_lock(&gasneti_atomicop_lock), \
                                      ((p)->ctr)--,                             \
                                       gasnet_hsl_unlock(&gasneti_atomicop_lock))
#else
  #if defined(LINUX)
    #ifdef BROKEN_LINUX_ASM_ATOMIC_H
      /* some versions of the linux kernel ship with a broken atomic.h
         this code based on a non-broken version of the header */
      #include <linux/config.h>
      #ifdef CONFIG_SMP
        #define GASNETI_LOCK "lock ; "
      #else
        #define GASNETI_LOCK ""
      #endif

      #ifndef __i386__
        #error you have broken Linux system headers and a broken CPU. barf...
      #endif

      typedef struct { volatile int counter; } gasneti_atomic_t;
      #define gasneti_atomic_read(p)      ((p)->counter)
      #define gasneti_atomic_init(v)      { (v) }
      #define gasneti_atomic_set(p,v)     ((p)->counter = (v))
      static __inline__ void 
        gasneti_atomic_increment(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "incl %0"
                :"=m" (v->counter)
                :"m" (v->counter));
      }
      static __inline__ void 
        gasneti_atomic_decrement(gasneti_atomic_t *v){
        __asm__ __volatile__(
                GASNETI_LOCK "decl %0"
                :"=m" (v->counter)
                :"m" (v->counter));
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
    #endif
  #elif defined(FREEBSD)
    #include <machine/atomic.h>
    typedef volatile u_int32_t gasneti_atomic_t;
    #define gasneti_atomic_increment(p) atomic_add_int((p),1)
    #define gasneti_atomic_decrement(p) atomic_subtract_int((p),1)
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
  #elif defined(CYGWIN)
    #include <windows.h>
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) InterlockedIncrement((LONG *)&((p)->ctr))
    #define gasneti_atomic_decrement(p) InterlockedDecrement((LONG *)&((p)->ctr))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
  #elif defined(AIX)
    #include <sys/atomic_op.h>
    typedef struct { volatile int ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (fetch_and_add((atomic_p)&((p)->ctr),1))
    #define gasneti_atomic_decrement(p) (fetch_and_add((atomic_p)&((p)->ctr),-1))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
  #elif defined(OSF)
   #if 1
    #include <sys/machine/builtins.h>
    typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (__ATOMIC_INCREMENT_LONG(&((p)->ctr)))
    #define gasneti_atomic_decrement(p) (__ATOMIC_DECREMENT_LONG(&((p)->ctr)))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
   #else
    #include <sys/systm.h>
    typedef struct { volatile int ctr; } gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (atomic_incl(&((p)->ctr)))
    #define gasneti_atomic_increment(p) (atomic_decl(&((p)->ctr)))
    #define gasneti_atomic_read(p)      ((p)->ctr)
    #define gasneti_atomic_set(p,v)     ((p)->ctr = (v))
    #define gasneti_atomic_init(v)      { (v) }
   #endif
  #elif defined(IRIX)
    #include <mutex.h>
    typedef __uint32_t gasneti_atomic_t;
    #define gasneti_atomic_increment(p) (test_then_add32((p),1))
    #define gasneti_atomic_decrement(p) (test_then_add32((p),(uint32_t)-1))
    #define gasneti_atomic_read(p)      (*(p))
    #define gasneti_atomic_set(p,v)     (*(p) = (v))
    #define gasneti_atomic_init(v)      (v)
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

/* a local memory barrier - ensure all previous stores to local mem
   from this proc are globally completed across this SMP 
   (i.e. all reads issued from any CPU subsequent to this call
      returning will see the new value for any previously issued
      writes from this proc)
 */
#ifdef __GNUC__
  #define ASM(mnemonic) asm volatile (#mnemonic)
#elif defined(__digital__)
  #include <c_asm.h>
  #define ASM(mnemonic) asm(#mnemonic)
#elif defined(MIPSPRO_COMPILER)
  #define ASM(mnemonic)  /* broken - doesn't have inline assembly */
#else
  #define ASM(mnemonic) asm { mnemonic }
#endif

#if (defined(_POWER) || defined(_POWERPC)) && !defined(__GNUC__)  
/* VisualAge C compiler (mpcc_r) has no support for inline symbolic assembly
 * you have to hard-code the opcodes in a pragma that defines an assembly 
 * function - see /usr/include/sys/atomic_op.h on AIX for examples
 * opcodes can be aquired by placing the mnemonics in inline.s and running:
 * as -sinline.lst inline.s
 */ 
#pragma mc_func _do_sync { \
  "7c0004ac" /* sync (same opcode used for dcs)*/ \
}
#endif

#if defined(__sparc__) || defined(__sparc) || defined(sparc)
 GASNET_INLINE_MODIFIER(gasneti_local_membar)
 void gasneti_local_membar(void) {
   ASM(stbar); /* SPARC store barrier */
 }
#elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
 GASNET_INLINE_MODIFIER(gasneti_local_membar)
 void gasneti_local_membar(void) {
   ASM(sync);  /* MIPS II+ memory barrier */ 
 }
#elif defined(__i386__) || defined(__i386) || defined(i386) || \
      defined(__i486__) || defined(__i486) || defined(i486) || \
      defined(__i586__) || defined(__i586) || defined(i586) || \
      defined(__i686__) || defined(__i686) || defined(i686)
 #if 0
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     ASM(mfence); /* only works on pentiums and higher? */
   }
 #else
  #ifdef __linux__
    #include <linux/config.h>
    #if !defined(__SMP__) && !defined(CONFIG_SMP)
      #define gasneti_local_membar()
    #else
      /* sfence causes an illegal instruction trap on uniprocessor kernel */
      GASNET_INLINE_MODIFIER(gasneti_local_membar)
      void gasneti_local_membar(void) {
        ASM(sfence);
      }
    #endif
  #else 
    GASNET_INLINE_MODIFIER(gasneti_local_membar)
    void gasneti_local_membar(void) {
      ASM(sfence);
    }
  #endif
 #endif
#elif defined(__ia64__) /* Itanium */
  #ifdef __linux__
    #include <linux/config.h>
    #if !defined(__SMP__) && !defined(CONFIG_SMP)
      #define gasneti_local_membar()
    #else
      /* mf may cause an illegal instruction trap on uniprocessor kernel */
      GASNET_INLINE_MODIFIER(gasneti_local_membar)
      void gasneti_local_membar(void) {
        ASM(mf);
      }
    #endif
  #else
    GASNET_INLINE_MODIFIER(gasneti_local_membar)
    void gasneti_local_membar(void)  {
      ASM(mf);
    }
  #endif
#elif defined(_POWER) /* IBM SP POWER2, POWER3 */
 #ifdef __GNUC__
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     ASM(dcs);
   }
 #else
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     _do_sync(); 
   }
 #endif
#elif defined(_POWERPC)
 #ifdef __GNUC__
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     ASM(sync);
   }
 #else
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     _do_sync(); 
   }
 #endif
#elif defined(__alpha) && defined(__osf__)
 #if 1
   GASNET_INLINE_MODIFIER(gasneti_local_membar)
   void gasneti_local_membar(void) {
     ASM(mb);
   }
 #else 
   #include <machine/builtins.h>
   #define gasneti_local_membar() __MB() /* only available as compaq C built-in */
 #endif
#else
 #error unknown CPU - dont know how to do a local memory barrier for your CPU/OS
#endif

/* ------------------------------------------------------------------------------------ */

#endif
