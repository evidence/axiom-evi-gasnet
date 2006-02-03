/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_extended_help.h,v $
 *     $Date: 2006/02/03 19:06:58 $
 * $Revision: 1.29 $
 * Description: GASNet Extended API Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_HELP_H
#define _GASNET_EXTENDED_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

/* ------------------------------------------------------------------------------------ */

#if GASNETI_CLIENT_THREADS
  struct _gasnete_threaddata_t;
  extern struct _gasnete_threaddata_t *gasnete_mythread() __attribute__ ((const));
  #if defined(__xlC__)
    #if 0
      /* this should work according to the &*@&#$! IBM compiler docs, 
         but of course it doesn't... */
      #pragma options pure=gasnete_mythread
    #else
      #pragma isolated_call(gasnete_mythread)
    #endif
  #endif
  /* TODO: mark gasnete_mythread() as a pure function for other compilers */
#endif

/* gasnete_islocal() is used by put/get fns to decide whether shared memory on 
   a given node is "local". By default this is based on comparing the nodeid to
   the local node id, but clients can override this to remove the check overhead
   by defining either GASNETE_PUTGET_ALWAYSLOCAL or GASNETE_PUTGET_ALWAYSREMOTE
 */
#if defined(GASNETE_PUTGET_ALWAYSLOCAL)
  #define gasnete_islocal(nodeid) (1) /* always local */
#elif defined(GASNETE_PUTGET_ALWAYSREMOTE)
  #define gasnete_islocal(nodeid) (0) /* always remote */
#else
  #define gasnete_islocal(nodeid) (nodeid == gasneti_mynode)
#endif

/* ------------------------------------------------------------------------------------ */
#if defined(_CRAYC) || (SIZEOF_SHORT > 2)  /* deal with Cray's crappy lack of 16-bit types */
  #define OMIT_WHEN_MISSING_16BIT(code) 
#else
  #define OMIT_WHEN_MISSING_16BIT(code) code
#endif
/*  undefined results if the regions are overlapping */
#define GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes) do { \
  gasneti_compiler_fence(); /* bug 1389 - we are type-punning here */ \
  switch(nbytes) {                                          \
    case 0:                                                 \
      break;                                                \
    case sizeof(uint8_t):                                   \
      *((uint8_t *)(dest)) = *((uint8_t *)(src));           \
      break;                                                \
  OMIT_WHEN_MISSING_16BIT(                                  \
    case sizeof(uint16_t):                                  \
      *((uint16_t *)(dest)) = *((uint16_t *)(src));         \
      break;                                                \
  )                                                         \
    case sizeof(uint32_t):                                  \
      *((uint32_t *)(dest)) = *((uint32_t *)(src));         \
      break;                                                \
    case sizeof(uint64_t):                                  \
      *((uint64_t *)(dest)) = *((uint64_t *)(src));         \
      break;                                                \
    default:                                                \
      memcpy(dest, src, nbytes);                            \
  }                                                         \
  gasneti_compiler_fence(); /* bug 1389 - we are type-punning here */ \
  } while(0)

#define GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes) memcpy(dest, src, nbytes)

/* given the address of a gasnet_register_value_t object and the number of
   significant bytes, return the byte address where significant bytes begin */
#ifdef WORDS_BIGENDIAN
  #define GASNETE_STARTOFBITS(regvalptr,nbytes) \
    (((uint8_t*)(regvalptr)) + ((sizeof(gasnet_register_value_t)-nbytes)))
#else /* little-endian */
  #define GASNETE_STARTOFBITS(regvalptr,nbytes) (regvalptr)
#endif

/* The value written to the target address is a direct byte copy of the 
   8*nbytes low-order bits of value, written with the endianness appropriate 
   for an nbytes integral value on the current architecture
   */
#define GASNETE_VALUE_ASSIGN(dest, value, nbytes) do {                  \
  gasneti_compiler_fence(); /* bug 1389 - we are type-punning here */   \
  switch (nbytes) {                                                     \
    case 0:                                                             \
      break;                                                            \
    case sizeof(uint8_t):                                               \
      *((uint8_t *)(dest)) = (uint8_t)(value);                          \
      break;                                                            \
  OMIT_WHEN_MISSING_16BIT(                                              \
    case sizeof(uint16_t):                                              \
      *((uint16_t *)(dest)) = (uint16_t)(value);                        \
      break;                                                            \
  )                                                                     \
    case sizeof(uint32_t):                                              \
      *((uint32_t *)(dest)) = (uint32_t)(value);                        \
      break;                                                            \
    case sizeof(uint64_t):                                              \
      *((uint64_t *)(dest)) = (uint64_t)(value);                        \
      break;                                                            \
    default:  /* no such native nbytes integral type */                 \
      memcpy((dest), GASNETE_STARTOFBITS(&(value),nbytes), nbytes);     \
  }                                                                     \
  gasneti_compiler_fence(); /* bug 1389 - we are type-punning here */   \
  } while (0)

#if GASNET_NDEBUG
  #define gasnete_aligncheck(ptr,nbytes)
#else
  #if 0
    #define gasnete_aligncheck(ptr,nbytes) do {               \
        if ((nbytes) <= 8 && (nbytes) % 2 == 0)               \
          gasneti_assert(((uintptr_t)(ptr)) % (nbytes) == 0); \
      } while (0)
  #else
    static uint8_t _gasnete_aligncheck[600];
    #define gasnete_aligncheck(ptr,nbytes) do {                                         \
        uint8_t *_gasnete_alignbuf =                                                    \
          (uint8_t *)(((uintptr_t)&(_gasnete_aligncheck[0x100])) & ~((uintptr_t)0xFF)); \
        uintptr_t offset = ((uintptr_t)(ptr)) & 0xFF;                                   \
        uint8_t *p = _gasnete_alignbuf + offset;                                        \
        gasneti_assert(p >= _gasnete_aligncheck &&                                      \
              (p + 8) < (_gasnete_aligncheck+sizeof(_gasnete_aligncheck)));             \
        /* NOTE: a runtime bus error in this code indicates the relevant pointer        \
            was not "properly aligned for accessing objects of size nbytes", as         \
            required by the GASNet spec for src/dest addresses in non-bulk puts/gets    \
         */                                                                             \
        switch (nbytes) {                                                               \
          case 1: *(uint8_t *)p = 0; break;                                             \
        OMIT_WHEN_MISSING_16BIT(                                                        \
          case 2: *(uint16_t *)p = 0; break;                                            \
        )                                                                               \
          case 4: *(uint32_t *)p = 0; break;                                            \
          case 8: *(uint64_t *)p = 0; break;                                            \
        }                                                                               \
      } while (0)
  #endif
#endif


/* gasnete_loopback{get,put}_memsync() go after a get or put is done with both source
 * and destination on the local node.  This is only done if GASNet was configured
 * for the stricter memory consistency model.
 * The put_memsync belongs after the memory copy to ensure that writes are committed in
 * program order.
 * The get_memsync belongs after the memory copy to ensure that if the value(s) read
 * is used to predicate any subsequent reads, that the reads are done in program order.
 * Note that because gasnet_gets may read multiple words, it's possible that the 
 * values fetched in a multi-word get may reflect concurrent strict writes by other CPU's 
 * in a way that appears to violate program order, eg:
 *  CPU0: gasnet_put_val(mynode,&A[0],someval,1) ; gasnet_put_val(mynode,&A[1],someval,1); 
 *  CPU1: gasnet_get(dest,mynode,&A[0],someval,2) ; // may see updated A[1] but not A[0]
 * but there doesn't seem to be much we can do about that (adding another rmb before the
 * get does not solve the problem, because the two puts may globally complete in the middle
 * of the get's execution, after copying A[0] but before copying A[1]). It's a fundamental
 * result of the fact that multi-word gasnet put/gets are not performed atomically 
 * (and for performance reasons, cannot be).
 */
#ifdef GASNETI_MEMSYNC_ON_LOOPBACK
  #define gasnete_loopbackput_memsync() gasneti_local_wmb()
  #define gasnete_loopbackget_memsync() gasneti_local_rmb()
#else
  #define gasnete_loopbackput_memsync() 
  #define gasnete_loopbackget_memsync()
#endif

/* ------------------------------------------------------------------------------------ */
/* thread-id optimization support */
#ifdef GASNETI_THREADINFO_OPT
  #define GASNETE_THREAD_FARG_ALONE   gasnet_threadinfo_t const GASNETI_RESTRICT _threadinfo
  #define GASNETE_THREAD_FARG         , GASNETE_THREAD_FARG_ALONE
  #define GASNETE_THREAD_GET_ALONE    GASNET_GET_THREADINFO()
  #define GASNETE_THREAD_GET          , GASNETE_THREAD_GET_ALONE
  #define GASNETE_THREAD_PASS_ALONE   (_threadinfo)
  #define GASNETE_THREAD_PASS         , GASNETE_THREAD_PASS_ALONE
  #define GASNETE_THREAD_SWALLOW(x)
  #define GASNETE_TISTARTOFBITS(ptr,nbytes,ti) GASNETE_STARTOFBITS(ptr,nbytes)
  #define GASNETE_MYTHREAD            ((gasnete_threaddata_t *)_threadinfo)
#else
  #define GASNETE_THREAD_FARG_ALONE   
  #define GASNETE_THREAD_FARG         
  #define GASNETE_THREAD_GET_ALONE   
  #define GASNETE_THREAD_GET         
  #define GASNETE_THREAD_PASS_ALONE   
  #define GASNETE_THREAD_PASS         
  #define GASNETE_THREAD_SWALLOW(x)
  #define GASNETE_TISTARTOFBITS       GASNETE_STARTOFBITS
  #define GASNETE_MYTHREAD            (gasnete_mythread())
#endif
/* ------------------------------------------------------------------------------------ */

#ifdef GASNETE_HAVE_EXTENDED_HELP_EXTRA_H
  #include <gasnet_extended_help_extra.h>
#endif

END_EXTERNC

#endif
