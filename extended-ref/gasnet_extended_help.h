/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_help.h                 $
 *     $Date: 2002/06/13 12:14:04 $
 * $Revision: 1.2 $
 * Description: GASNet Extended API Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_HELP_H
#define _GASNET_EXTENDED_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

/* ------------------------------------------------------------------------------------ */
extern gasnet_node_t gasnete_mynode;
extern gasnet_node_t gasnete_nodes;
extern gasnet_seginfo_t *gasnete_seginfo;

#ifdef GASNETI_THREADS
  struct _gasnete_threaddata_t;
  /* TODO: mark this as a pure function for other compilers */
  extern struct _gasnete_threaddata_t *gasnete_mythread() __attribute__ ((const));
  #if defined(__xlC__)
    #if 1
      #pragma options pure=gasnete_mythread
    #else
      #pragma isolated_call(gasnete_mythread)
    #endif
  #endif
#endif

#define gasnete_islocal(nodeid) (nodeid == gasnete_mynode)
#define gasnete_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,e)

/* busy-waits, with no implicit polling (cnd should include an embedded poll)
   differs from GASNET_BLOCKUNTIL because it may be waiting for an event
     caused by the receipt of a non-AM message
 */
#define gasnete_waituntil(cnd) gasnete_waitwhile(!(cnd)) 
#define gasnete_waitwhile(cnd) do { /* could add something here */ } while (cnd) 

/* ------------------------------------------------------------------------------------ */
#ifdef _CRAYC /* deal with Cray C's crappy lack of 16-bit types */
  #define OMIT_ON_CRAYC(code) 
#else
  #define OMIT_ON_CRAYC(code) code
#endif
/*  undefined results if the regions are overlapping */
#define GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes) do { \
  switch(nbytes) {                                          \
    case 0:                                                 \
      break;                                                \
    case sizeof(uint8_t):                                   \
      *((uint8_t *)(dest)) = *((uint8_t *)(src));           \
      break;                                                \
  OMIT_ON_CRAYC(                                            \
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
  } } while(0)

#define GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes) memcpy(dest, src, nbytes)

/* The value written to the target address is a direct byte copy of the 
   8*nbytes low-order bits of value, written with the endianness appropriate 
   for an nbytes integral value on the current architecture
   */
#define GASNETE_VALUE_ASSIGN(dest, value, nbytes) do {                  \
  switch (nbytes) {                                                     \
    case 0:                                                             \
      break;                                                            \
    case sizeof(uint8_t):                                               \
      *((uint8_t *)(dest)) = (uint8_t)(value);                          \
      break;                                                            \
  OMIT_ON_CRAYC(                                                        \
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
    default: { /* no such native nbytes integral type */                \
      gasnet_register_value_t temp = (gasnet_register_value_t)(value);  \
      gasnet_register_value_t *addr = (gasnet_register_value_t*)(dest); \
      gasnet_register_value_t newval;                                   \
      gasnet_register_value_t mask = (1 << (nbytes << 3))-1;            \
      memcpy(&newval, addr, sizeof(gasnet_register_value_t));           \
      newval = (newval & (~mask)) | (temp & mask);                      \
      memcpy(addr, &newval, sizeof(gasnet_register_value_t));           \
      *addr = newval;                                                   \
    }                                                                   \
                                                                        \
  } } while (0)


/* ------------------------------------------------------------------------------------ */
/* thread-id optimization support */
#ifdef GASNETI_THREADINFO_OPT
  #define GASNETE_THREAD_FARG_ALONE   gasnet_threadinfo_t const _threadinfo
  #define GASNETE_THREAD_FARG         , GASNETE_THREAD_FARG_ALONE
  #define GASNETE_THREAD_GET_ALONE    GASNET_GET_THREADINFO()
  #define GASNETE_THREAD_GET          , GASNETE_THREAD_GET_ALONE
  #define GASNETE_THREAD_PASS_ALONE   (_threadinfo)
  #define GASNETE_THREAD_PASS         , GASNETE_THREAD_PASS_ALONE
  #define GASNETE_MYTHREAD            ((gasnete_threaddata_t *)_threadinfo)
#else
  #define GASNETE_THREAD_FARG_ALONE   
  #define GASNETE_THREAD_FARG         
  #define GASNETE_THREAD_GET_ALONE   
  #define GASNETE_THREAD_GET         
  #define GASNETE_THREAD_PASS_ALONE   
  #define GASNETE_THREAD_PASS         
  #define GASNETE_MYTHREAD            (gasnete_mythread())
#endif
/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif
