/*  $Archive:: /Ti/GASNet/tests/test.h                                    $
 *     $Date: 2002/06/01 14:24:57 $
 * $Revision: 1.1 $
 * Description: helpers for GASNet tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */


#ifndef _TEST_H
#define _TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include "gasnet.h"

static char *_ErrorName(int errval) {
  switch (errval) {
    case GASNET_OK:           return "GASNET_OK";      
    case GASNET_ERR_NOT_INIT: return "GASNET_ERR_NOT_INIT";      
    case GASNET_ERR_BAD_ARG:  return "GASNET_ERR_BAD_ARG";       
    case GASNET_ERR_RESOURCE: return "GASNET_ERR_RESOURCE";      
    case GASNET_ERR_BARRIER_MISMATCH: return "GASNET_ERR_BARRIER_MISMATCH";      
    case GASNET_ERR_NOT_READY: return "GASNET_ERR_NOT_READY";      
    default: return "*unknown*";
    }
  }
static char *_ErrorDesc(int errval) {
  switch (errval) {
    case GASNET_OK:           return "No error";      
    case GASNET_ERR_NOT_INIT: return "GASNet message layer not initialized"; 
    case GASNET_ERR_BAD_ARG:  return "Invalid function parameter passed";    
    case GASNET_ERR_RESOURCE: return "Problem with requested resource";      
    case GASNET_ERR_BARRIER_MISMATCH: return "Barrier id's mismatched";      
    case GASNET_ERR_NOT_READY: return "Non-blocking operation not complete";      
    default: return "no description available";
    }
  }

#define GASNET_Safe(fncall) do {                            \
    int retval;                                             \
    if ((retval = fncall) != GASNET_OK) {                   \
            fprintf(stderr, "Error calling: %s\n"           \
                   " at: %s:%i\n"                           \
                   " error: %s (%s)\n",                     \
                   #fncall, __FILE__, __LINE__,             \
                   _ErrorName(retval), _ErrorDesc(retval)); \
            fflush(stderr);                                 \
            gasnet_exit(retval);                            \
    }                                                       \
  } while(0)

static int64_t mygetMicrosecondTimeStamp(void)
{
    int64_t retval;
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
	perror("gettimeofday");
	abort();
    }
    retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
    return retval;
}

/* return a microsecond time-stamp */
#define TIME() mygetMicrosecondTimeStamp()

#define MSG(s) do {                                                              \
  printf("node %i/%i %s\n", gasnet_mynode(), gasnet_nodes(), s); fflush(stdout); \
  } while(0)

#define BARRIER() do {                                                \
  gasnete_barrier_notify(0,GASNET_BARRIERFLAGS_ANONYMOUS);            \
  GASNET_Safe(gasnete_barrier_wait(0,GASNET_BARRIERFLAGS_ANONYMOUS)); \
} while (0)

#ifdef IRIX
  #define PAGESZ 16384
#else
  #define PAGESZ 4096
#endif

/* declare a properly page-aligned, globally-aligned static memory area with given size
 */
#define DECLARE_ALIGNED_SEG(requestedsegsz)             \
  extern char _hidden_seg[];                            \
  char _hidden_seg[(requestedsegsz)+PAGESZ];            \
  const uintptr_t _segsz = (uintptr_t)(requestedsegsz); \
  void *_get_seg() {                                    \
    return (((uint8_t*)_hidden_seg) +                   \
    (((((uintptr_t)_hidden_seg)%PAGESZ) == 0)? 0 :      \
     (PAGESZ-(((uintptr_t)_hidden_seg)%PAGESZ))));      \
  }

#define MYSEG() (_get_seg())
#define SEGSZ() (_segsz)

#endif
