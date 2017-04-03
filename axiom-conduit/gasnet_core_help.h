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

#ifdef _BLOCK_ON_LOOP_EPOLL

extern int gasneti_wait_mode;
extern gasneti_mutex_t gasnetc_mut;

#ifdef EVENTFD_PER_THREAD

typedef struct {
    /** Thread index. From 0 to GASNETI_MAX_THREADS-1. */
    int idx;
    /** A mask for test idx-th thread. */
    uint64_t keymask;
    /** e-poll-file-descriptor to block on (with epoll). */
    int epfd;
    /** event-file-descriptor to signal to wake up. */
    int evfd;
} gasnetc_tls_t;

extern gasnetc_tls_t *gasneti_get_new_thread_keymask();

#define gasneti_define_thread_keymask() \
    gasnetc_tls_t *ptr=(gasnetc_tls_t*)pthread_getspecific(gasnetc_thread_key);\
    if (ptr==NULL) ptr=gasneti_get_new_thread_keymask();

extern int gasnetc_block_on_condition(uint64_t key, int epfd, int evfd);

#define call_gasnetc_block_on_condition() gasnetc_block_on_condition(ptr->keymask,ptr->epfd,ptr->evfd)

#else

extern uint64_t *gasneti_get_new_thread_keymask();

#define gasneti_define_thread_keymask() \
    uint64_t *ptr=pthread_getspecific(gasnetc_thread_key);\
    uint64_t key;\
    if (ptr==NULL) ptr=gasneti_get_new_thread_keymask();\
    key=*ptr;

extern int gasnetc_block_on_condition(uint64_t key);

#define call_gasnetc_block_on_condition() gasnetc_block_on_condition(key)

#endif

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
      gasneti_define_thread_keymask();\
      gasneti_mutex_lock(&gasnetc_mut);\
      while (cnd) { \
        call_gasnetc_block_on_condition();\
        gasneti_mutex_unlock(&gasnetc_mut);\
        gasneti_internal_AMPoll();\
        gasneti_mutex_lock(&gasnetc_mut);\
      }\
      gasneti_mutex_unlock(&gasnetc_mut);\
    } else {\
      /* GASNET_WAIT_SPINBLOCK */\
      gasneti_define_thread_keymask();\
      int to_cont=1;\
      gasneti_mutex_lock(&gasnetc_mut);\
      while (cnd) { \
        call_gasnetc_block_on_condition();\
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

/*
// Can not be inline!!! some program test for the presence of the gasnetc_AMPoll into tht library!!!
#define GASNETI_GASNETC_AMPOLL
GASNETI_INLINE(gasnetc_AMPoll)
int gasnetc_AMPoll(void) {
    register int res=gasnetc_internal_AMPoll();
    return res==GASNET_ERR_AGAIN?GASNET_OK:res;
}
*/

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
