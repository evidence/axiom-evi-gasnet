/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core_help.h $
 * Description: GASNet AXIOM conduit core Header Helpers
 * (Internal code, not for client use)
 *
 * Copyright (C) 2016, Evidence Srl.
 * Terms of use are as specified in COPYING
 *
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

GASNETI_BEGIN_EXTERNC

/* see explanation into axiom-conduit/gasnet_core.c */
//#define _NOT_BLOCK_ON_LOOP
#define _BLOCK_ON_LOOP_CONDWAIT
//#define _BLOCK_ON_LOOP_EPOLL

#ifdef _BLOCK_ON_LOOP_CONDWAIT

extern gasneti_mutex_t gasnetc_mut;
extern gasneti_cond_t gasnetc_cond;
extern int gasneti_wait_mode;

extern void gasnetc_block_on_condition();

#define gasneti_pollwhile(cnd) do {\
  if (cnd) {\
    gasneti_internal_AMPoll();\
    if (gasneti_wait_mode == GASNET_WAIT_SPIN) {\
      /* GASNET_WAIT_SPIN */\
      while (cnd) {\
        gasneti_internal_AMPoll();\
      }\
    } else if (gasneti_wait_mode == GASNET_WAIT_BLOCK) {\
      /* GASNET_WAIT_BLOCK */\
      gasneti_mutex_lock(&gasnetc_mut);\
      while (cnd) { \
        gasnetc_block_on_condition();\
        gasneti_mutex_unlock(&gasnetc_mut);\
        gasneti_internal_AMPoll();\
        gasneti_mutex_lock(&gasnetc_mut);\
      }\
      gasneti_mutex_unlock(&gasnetc_mut);\
    } else {\
      /* GASNET_WAIT_SPINBLOCK */\
      int to_cont=1;\
      gasneti_mutex_lock(&gasnetc_mut);\
      while (cnd) { \
        gasnetc_block_on_condition();\
        gasneti_mutex_unlock(&gasnetc_mut);\
        while (gasneti_internal_AMPoll()!=GASNET_ERR_AGAIN&&(to_cont=(cnd))) {}\
        if (!to_cont) break;\
        gasneti_mutex_lock(&gasnetc_mut);\
      }\
      if (to_cont) gasneti_mutex_unlock(&gasnetc_mut);\
    }\
    gasneti_local_rmb();\
  }\
} while (0)

#endif

extern int gasnetc_internal_AMPoll(void);

#define GASNETI_GASNETC_AMPOLL
GASNETI_INLINE(gasnetc_AMPoll)
int gasnetc_AMPoll(void) {
    register int res=gasnetc_internal_AMPoll();
    return res==GASNET_ERR_AGAIN?GASNET_OK:res;
}

#include <gasnet_help.h>

GASNETI_INLINE(gasneti_internal AMPoll)
int gasneti_internal_AMPoll(void) {
    int retval;
    gasneti_AMPoll_spinpollers_check();
    gasneti_memcheck_one();
    retval = gasnetc_internal_AMPoll();
    GASNETI_PROGRESSFNS_RUN();
    return retval;
}

// safety
#if GASNETI_THROTTLE_POLLERS
#error GASNETI_THROTTLE_POLLERS not working/implemented
#endif

GASNETI_END_EXTERNC

#endif
