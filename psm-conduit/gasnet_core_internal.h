/*   $Source: bitbucket.org:berkeleylab/gasnet.git/psm-conduit/gasnet_core_internal.h $
 *     $Date: 2009/09/18 23:33:48 $
 * Description: GASNet psm conduit header for internal definitions in Core API
 * Copyright (c) 2013-2015 Intel Corporation. All rights reserved.
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>

#include <psm2.h>
#include <psm2_am.h>
#include <psm2_mq.h>


/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* -------------------------------------------------------------------------- */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
#define _hidx_gasnetc_handler_barrier2        (GASNETC_HANDLER_BASE+1)
#define _hidx_gasnetc_handler_exit2           (GASNETC_HANDLER_BASE+2)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* -------------------------------------------------------------------------- */
/* handler table (recommended impl) */
#define GASNETC_MAX_NUMHANDLERS   256
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* -------------------------------------------------------------------------- */
/* AM category (recommended impl if supporting PSHM) */
typedef enum {
    gasnetc_Short=0,
    gasnetc_Medium=1,
    gasnetc_Long=2
} gasnetc_category_t;


#if HAVE_SSH_SPAWNER
#include <ssh-spawner/gasnet_bootstrap_internal.h>
#endif
#if HAVE_MPI_SPAWNER
#include <mpi-spawner/gasnet_bootstrap_internal.h>
#endif
#if HAVE_PMI_SPAWNER
#include <pmi-spawner/gasnet_bootstrap_internal.h>
#endif

/*
 * Multi-thread support
 * PSM is not thread-safe, so all PSM calls must be wrapped by a lock.
 */

extern gasneti_atomic_t gasnetc_psm_lock;

#define GASNETC_PSM_LOCK() gasneti_spinlock_lock(&gasnetc_psm_state.psm_lock)
#define GASNETC_PSM_TRYLOCK() gasneti_spinlock_trylock(&gasnetc_psm_state.psm_lock)
#define GASNETC_PSM_UNLOCK() gasneti_spinlock_unlock(&gasnetc_psm_state.psm_lock)

#if GASNET_PSHM
/* When PSHM is enabled, the progress thread will poll PSHM to provide passive
   progress.  These lock macros protect calls to PSHM. */
#define GASNETC_PSM_PSHM_LOCK() gasneti_spinlock_lock(&gasnetc_psm_state.pshm_lock)
#define GASNETC_PSM_PSHM_TRYLOCK() gasneti_spinlock_trylock(&gasnetc_psm_state.pshm_lock)
#define GASNETC_PSM_PSHM_UNLOCK() gasneti_spinlock_unlock(&gasnetc_psm_state.pshm_lock)
#endif

/*
 * Bootstrap support
 */
extern void (*gasneti_bootstrapFini_p)(void);
extern void (*gasneti_bootstrapAbort_p)(int exitcode);
extern void (*gasneti_bootstrapBarrier_p)(void);
extern void (*gasneti_bootstrapExchange_p)(void *src, size_t len, void *dest);
extern void (*gasneti_bootstrapAlltoall_p)(void *src, size_t len, void *dest);
extern void (*gasneti_bootstrapBroadcast_p)(void *src, size_t len, void *dest, int rootnode);
extern void (*gasneti_bootstrapCleanup_p)(void);
#define gasneti_bootstrapFini           (*gasneti_bootstrapFini_p)
#define gasneti_bootstrapAbort          (*gasneti_bootstrapAbort_p)
#define gasneti_bootstrapBarrier        (*gasneti_bootstrapBarrier_p)
#define gasneti_bootstrapExchange       (*gasneti_bootstrapExchange_p)
#define gasneti_bootstrapAlltoall       (*gasneti_bootstrapAlltoall_p)
#define gasneti_bootstrapBroadcast      (*gasneti_bootstrapBroadcast_p)
#define gasneti_bootstrapCleanup        (*gasneti_bootstrapCleanup_p)

#define AM_HANDLER_SHORT    0 /* gasnetc_handler_short */
#define AM_HANDLER_MED      1 /* gasnetc_handler_med */
#define AM_HANDLER_LONG     2 /* gasnetc_handler_long */
#define AM_HANDLER_PUT      3 /* gasnete_handler_put */
#define AM_HANDLER_GET_REQUEST 4 /* gasnete_handler_get_request */
#define AM_HANDLER_GET_REPLY   5 /* gasnete_handler_get_reply */
#define AM_HANDLER_LONG_PUT 6 /* gasnete_handler_long_put */
#define AM_HANDLER_LONG_GET 7 /* gasnete_handler_long_get */
#define AM_HANDLER_NUM      8

/* Set this bit in the handler index argument to indicate reply. */
#define REQUEST_BIT 0x100

/* -------------------------------------------------------------------------- */
/* Generic, thread-safe single-linked queue */

typedef struct _gasnetc_item {
    struct _gasnetc_item *next;
} gasnetc_item_t;

typedef struct _gasnetc_list {
    gasneti_atomic_t lock;
    gasnetc_item_t head;
    gasnetc_item_t *tail;
} gasnetc_list_t;


static void gasnetc_list_init(gasnetc_list_t* list,
        unsigned int num_items, size_t item_size)
{
    gasneti_spinlock_init(&list->lock);

    if(num_items == 0) {
        list->head.next = NULL;
        list->tail = &list->head;
    } else {
        uintptr_t items;
        gasnetc_item_t *item;
        int i;

        items = (uintptr_t)gasneti_malloc(num_items * item_size);
        gasneti_leak((void*)items);

        for(i = 0; i < num_items - 1; i++) {
            item = (gasnetc_item_t *)(items + (i * item_size));
            item->next = (gasnetc_item_t *)
                    (items + ((i + 1) * item_size));
        }

        item = (gasnetc_item_t *)
            (items + ((num_items - 1) * item_size));
        item->next = NULL;
        list->head.next = (gasnetc_item_t *)items;
        list->tail = item;
    }
}

static gasnetc_item_t *gasnetc_list_remove_alloc_inner(gasnetc_list_t *list,
        unsigned int num_items, size_t item_size)
{
    /* Allocate new items.  There's a minor race condition here:
       The branch above is performed without the lock held, so it is
       possible for multiple processes to enter and allocate a new block of
       items.  The new items are added safely, so it's not much of a
       problem. */
    uintptr_t slab;
    uintptr_t cur_slab;
    gasnetc_item_t *cur_item;
    int i;

    gasneti_assert(num_items > 0);
    gasneti_assert(item_size > 0);

    /* Skip initializing and adding the first item allocated.  That
       item will be returned to the caller. */
    slab = (uintptr_t)gasneti_malloc(num_items * item_size);
    gasneti_leak((void*)slab);

    cur_slab = slab + item_size;
    cur_item = (gasnetc_item_t *)cur_slab;
    for(i = 1; i < num_items - 1; i++) {
        cur_slab += item_size;
        cur_item->next = (gasnetc_item_t *)cur_slab;
        cur_item = (gasnetc_item_t *)cur_slab;
    }

    /* At this point, cur_item points to the last new slab item. */
    gasneti_spinlock_lock(&list->lock);
    cur_item->next = list->head.next;
    list->head.next = (gasnetc_item_t *)(slab + item_size);
    if(list->tail == &list->head) {
        list->tail = cur_item;
    }
    gasneti_spinlock_unlock(&list->lock);

    return (gasnetc_item_t *)slab;
}

/* Try to remove an item from the list.  If the list is empty, allocate
   a slab of num_items and return a new item. */
GASNETI_INLINE(gasnetc_list_remove_alloc)
gasnetc_item_t *gasnetc_list_remove_alloc(gasnetc_list_t *list,
        unsigned int num_items, size_t item_size)
{
    gasnetc_item_t *item;

    if(list->head.next == NULL) {
        return gasnetc_list_remove_alloc_inner(list,
                num_items, item_size);
    } else {
        gasneti_spinlock_lock(&list->lock);
        item = list->head.next;
        if(item != NULL) {
            list->head.next = item->next;
            if(item->next == NULL) {
                list->tail = &list->head;
            }
        }
        gasneti_spinlock_unlock(&list->lock);
    }

    return item;
}

GASNETI_INLINE(gasnetc_list_add_head)
void gasnetc_list_add_head(gasnetc_list_t *list,
        gasnetc_item_t *item)
{
    gasneti_spinlock_lock(&list->lock);
    if(list->head.next == NULL)
        list->tail = item;
    item->next = list->head.next;
    list->head.next = item;
    gasneti_spinlock_unlock(&list->lock);
}

GASNETI_INLINE(gasnetc_list_add_tail)
void gasnetc_list_add_tail(gasnetc_list_t *list,
        gasnetc_item_t *item)
{
    item->next = NULL;

    gasneti_spinlock_lock(&list->lock);
    list->tail->next = item;
    list->tail = item;
    gasneti_spinlock_unlock(&list->lock);
}

GASNETI_INLINE(gasnetc_list_remove)
gasnetc_item_t *gasnetc_list_remove(gasnetc_list_t *list)
{
    gasnetc_item_t *item;

    if(list->head.next == NULL) {
        return NULL;
    }

    gasneti_spinlock_lock(&list->lock);
    item = list->head.next;
    if(item != NULL) {
        list->head.next = item->next;
        if(item->next == NULL) {
            list->tail = &list->head;
        }
    }
    gasneti_spinlock_unlock(&list->lock);

    return item;
}

GASNETI_INLINE(gasnetc_list_drain)
gasnetc_item_t *gasnetc_list_drain(gasnetc_list_t* list)
{
    gasnetc_item_t *head;

    gasneti_spinlock_lock(&list->lock);
    head = list->head.next;
    list->head.next = NULL;
    list->tail = &list->head;
    gasneti_spinlock_unlock(&list->lock);

    return head;
}

typedef struct _gasnete_transfer {
    void *context;
    uint32_t frags_remaining;
    uint32_t optype;
} gasnete_transfer_t;

/* -------------------------------------------------------------------------- */
/* General PSM conduit state */

typedef struct _gasnetc_psm_state {
    psm_ep_t ep;
    psm_mq_t mq;
    psm_epid_t epid;

    gasneti_atomic_t psm_lock;

    int periodic_poll;
    uint32_t long_msg_threshold;

    void* getreq_slab;        /* Slab of memory for get items */
    int getreq_alloc;        /* Items allocated for get item slab */
    gasnetc_list_t getreqs;    /* Free get items in slab */

    gasnetc_list_t avail_mq_ops;
    /* Queue of pending MQ send/receives to be posted */
    gasnetc_list_t pending_mq_ops;

    /* List of outstanding MQ requests to be completed */
    psm_mq_req_t *posted_reqs;
    int posted_reqs_length;
    int posted_reqs_alloc;

    /* List of transfers to be completed */
    gasnete_transfer_t *transfers;
    int transfers_count;
    int transfers_alloc;

    uint64_t mq_op_id;
    int am_handlers[AM_HANDLER_NUM];
    psm_epaddr_t* peer_epaddrs;

    /* Core AM handler wrappers set this for gasnetc_exit */
    int handler_running;

    int should_exit;
    int exit_code;
    double exit_timeout;

#if GASNET_PSHM
    gasneti_atomic_t pshm_lock;
#endif
} gasnetc_psm_state_t;

extern gasnetc_psm_state_t gasnetc_psm_state;

typedef struct _gasnetc_token {
    psm_am_token_t token;
    psm_epaddr_t source;
} gasnetc_token_t;


/* -------------------------------------------------------------------------- */
/* Internal progress routines */

/* Periodic polling:
   PSM should be called to process incoming messages, but not too frequently
   nor too infrequently.  If PSM is polled too frequently, the poll calls just
   become overhead when there are rarely messages waiting to be processed.  On
   the other hand, not polling often enough can result in long delays for
   waiting peers.  Calling PSM only once for every so many RMA operations
   strikes a balance between these two tradeoffs.  Informal testing resulted in
   the choice of once every 32 calls.
*/
GASNETI_HOT
GASNETI_INLINE(gasnetc_psm_poll_periodic)
void gasnetc_psm_poll_periodic(void)
{
    gasnetc_psm_state.periodic_poll += 1;
    if(gasnetc_psm_state.periodic_poll == 32) {
        gasnetc_psm_state.periodic_poll = 0;
        gasnetc_AMPoll();
    }
}


int gasnetc_progress_thread_init(void);

GASNETI_COLD GASNETI_NORETURN
void gasnetc_do_exit(void);

int gasnete_long_msg_init(void);

void gasnete_post_pending_mq_ops(void);

/* This routine assumes that psm_poll() has recently been called. */
void gasnete_finish_mq_reqs(void);

#endif
