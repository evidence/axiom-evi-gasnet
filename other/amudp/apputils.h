/*  $Archive:: /Ti/AMUDP/apputils.h                                       $
 *     $Date: 2004/01/19 12:57:33 $
 * $Revision: 1.3 $
 * Description: Application utilities on AMUDP
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _APPUTILS_H
#define _APPUTILS_H

#include <errno.h>
#ifdef WIN32
  #include <windows.h>  
  #define sleep(x) Sleep(1000*x)
#endif

#if defined(AMUDP)
  #include <amudp.h>
  #include <amudp_spmd.h>
#elif defined(AMMPI)
  #include <ammpi.h>
  #include <ammpi_spmd.h>
#else
  #error You should #define AMUDP/AMMPI (or #include amudp.h/ammpi.h) before including apputils.h
#endif

#if !defined(DEBUG) && !defined(NDEBUG)
  #ifdef AMX_DEBUG
    #define DEBUG 1
  #else
    #define NDEBUG 1
  #endif
#endif

#ifndef VERBOSE
  #if AMX_DEBUG_VERBOSE || GASNET_DEBUG_VERBOSE
    #define VERBOSE 1
  #else
    #define VERBOSE 0
  #endif
#endif

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifdef _MSC_VER
  #pragma warning(disable: 4127)
#endif

/* in a multi-threaded program, this would also include a lock */
#define AM_Safe(fncall) do {                \
  if ((fncall) != AM_OK) {                  \
    printf("Error calling: %s\n", #fncall); \
    AMX_SPMDExit(-1);                       \
    abort();                                \
    }                                       \
  } while(0)

#define AM_PollBlock(eb) do {                       \
        AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY));  \
        AM_Safe(AM_WaitSema(eb));                   \
        AM_Safe(AM_Poll(eb));                       \
        } while (0)

/* app can define this before including to move our handlers 
   NO - that doesn't work unless apputils.c is recompiled */
#ifndef APPUTIL_HANDLER_BASE
  #define APPUTIL_HANDLER_BASE  225
#endif

/* call first to setup handlers for all app utils */
void setupUtilHandlers(ep_t activeep, eb_t activeeb);

void printGlobalStats();


#ifdef UETH
  #define getCurrentTimeMicrosec() ueth_getustime()
#else
  extern int64_t getCurrentTimeMicrosec();
#endif

#ifndef APPUTILS_OMIT_READWRITE
uint32_t getWord(int proc, void *addr);
void putWord(int proc, void *addr, uint32_t val);

void readWord(void *destaddr, int proc, void *addr);
void readSync();

void writeWord(int proc, void *addr, uint32_t val);
void writeSync();
#endif

#endif
