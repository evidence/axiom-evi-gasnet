/*  $Archive:: /Ti/GASNet/mpi-conduit/gasnet_core.h                       $
 *     $Date: 2004/06/28 09:36:24 $
 * $Revision: 1.14 $
 * Description: GASNet header for MPI conduit core
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_H
#define _GASNET_CORE_H

#include <ammpi.h>

#include <gasnet_core_help.h>

BEGIN_EXTERNC

/*  TODO enhance AMMPI to support thread-safe MPI libraries */
/*  TODO add MPI bypass to loopback messages */

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* gasnet_init not inlined or renamed because we use redef-name trick on  
   it to ensure proper version linkage */
extern int gasnet_init(int *argc, char ***argv);

extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset);
#define gasnet_attach gasnetc_attach

extern void gasnetc_exit(int exitcode) GASNET_NORETURN;
#define gasnet_exit gasnetc_exit

extern uintptr_t gasnetc_getMaxLocalSegmentSize();
extern uintptr_t gasnetc_getMaxGlobalSegmentSize();
#define gasnet_getMaxLocalSegmentSize   gasnetc_getMaxLocalSegmentSize 
#define gasnet_getMaxGlobalSegmentSize gasnetc_getMaxGlobalSegmentSize 

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries);

GASNET_INLINE_MODIFIER(gasnet_mynode)
gasnet_node_t gasnet_mynode() {
  GASNETI_CHECKINIT();
  return gasnetc_mynode;
}
 
GASNET_INLINE_MODIFIER(gasnet_nodes)
gasnet_node_t gasnet_nodes() {
  GASNETI_CHECKINIT();
  return gasnetc_nodes;
}

#define gasnet_getSegmentInfo gasnetc_getSegmentInfo

GASNET_INLINE_MODIFIER(gasnet_getenv)
char *gasnet_getenv(const char *s) {
  GASNETI_CHECKINIT();
  return gasneti_getenv(s);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
*/
/* conduit may or may not need this based on whether interrupts are used for running handlers */
#if GASNETC_USE_INTERRUPTS || GASNETC_HSL_ERRCHECK
  extern void gasnetc_hold_interrupts();
  extern void gasnetc_resume_interrupts();

  #define gasnet_hold_interrupts    gasnetc_hold_interrupts
  #define gasnet_resume_interrupts  gasnetc_resume_interrupts
#else
  #define gasnet_hold_interrupts()
  #define gasnet_resume_interrupts()
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
#if GASNETC_HSL_ERRCHECK
  /* "magic" tag bit patterns that let us probabilistically detect
     the attempted use of uninitialized locks, or re-initialization of locks
   */
  #define GASNETC_HSL_ERRCHECK_TAGINIT ((uint64_t)0x5C9B5F7E9272EBA5ULL)
  #define GASNETC_HSL_ERRCHECK_TAGDYN  ((uint64_t)0xB82F6C0DE19C8F3DULL)
#endif

typedef struct _gasnet_hsl_t {
  gasneti_mutex_t lock;

  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t acquiretime;
  #endif

  #if GASNETC_HSL_ERRCHECK
    uint64_t tag;
    int islocked;
    int64_t timestamp;
    struct _gasnet_hsl_t *next;
  #endif

  #if GASNETC_USE_INTERRUPTS
    /* more state may be required for conduits using interrupts */
    #error interrupts not implemented
  #endif
} gasnet_hsl_t;

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_LOCK_STAT_INIT ,0 
#else
  #define GASNETC_LOCK_STAT_INIT  
#endif

#if GASNETC_HSL_ERRCHECK
  #define GASNETC_LOCK_ERRCHECK_INIT , GASNETC_HSL_ERRCHECK_TAGINIT, 0, 0, NULL
#else
  #define GASNETC_LOCK_ERRCHECK_INIT 
#endif

#if GASNETC_USE_INTERRUPTS
  #error interrupts not implemented
  #define GASNETC_LOCK_INTERRUPT_INIT 
#else
  #define GASNETC_LOCK_INTERRUPT_INIT  
#endif

#define GASNET_HSL_INITIALIZER { \
  GASNETI_MUTEX_INITIALIZER      \
  GASNETC_LOCK_STAT_INIT         \
  GASNETC_LOCK_ERRCHECK_INIT     \
  GASNETC_LOCK_INTERRUPT_INIT    \
  }

/* decide whether we have "real" HSL's */
#if GASNETI_THREADS || GASNETC_USE_INTERRUPTS || /* need for safety */ \
    GASNET_DEBUG || GASNETI_STATS_OR_TRACE       /* or debug/tracing */
  #ifdef GASNETC_NULL_HSL 
    #error bad defn of GASNETC_NULL_HSL
  #endif
#else
  #define GASNETC_NULL_HSL 1
#endif

#if GASNETC_NULL_HSL
  /* HSL's unnecessary - compile away to nothing */
  #define gasnet_hsl_init(hsl)
  #define gasnet_hsl_destroy(hsl)
  #define gasnet_hsl_lock(hsl)
  #define gasnet_hsl_unlock(hsl)
  #define gasnet_hsl_trylock(hsl)	GASNET_OK
#else
  extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl);
  extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl);
  extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl);
  extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl);
  extern int  gasnetc_hsl_trylock(gasnet_hsl_t *hsl);

  #define gasnet_hsl_init    gasnetc_hsl_init
  #define gasnet_hsl_destroy gasnetc_hsl_destroy
  #define gasnet_hsl_lock    gasnetc_hsl_lock
  #define gasnet_hsl_unlock  gasnetc_hsl_unlock
  #define gasnet_hsl_trylock gasnetc_hsl_trylock
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Active Message Size Limits
  ==========================
*/

#define gasnet_AMMaxArgs()          ((size_t)AM_MaxShort())
#define gasnet_AMMaxMedium()        ((size_t)AM_MaxMedium())
#define gasnet_AMMaxLongRequest()   ((size_t)AM_MaxLong())
#define gasnet_AMMaxLongReply()     ((size_t)AM_MaxLong())

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex);
extern int gasnetc_AMPoll();

#define gasnet_AMPoll          gasnetc_AMPoll
#define gasnet_AMGetMsgSource  gasnetc_AMGetMsgSource

#define GASNET_BLOCKUNTIL(cond) gasneti_polluntil(cond)

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

#define GASNETC_NO_AMREQUESTLONGASYNC
#include <gasnet_ammacros.h>
