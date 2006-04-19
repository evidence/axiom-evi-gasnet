/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_syncops.h,v $
 *     $Date: 2006/04/19 22:47:27 $
 * $Revision: 1.9 $
 * Description: GASNet header for synchronization operations used in GASNet implementation
 * Copyright 2006, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_INTERNAL_H)
  #error This file is not meant to be included directly- internal code should include gasnet_internal.h
#endif

#ifndef _GASNET_SYNCOPS_H
#define _GASNET_SYNCOPS_H

GASNETI_BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */

/* 
 * The gasnet mutex and spinlock code are in gasnet_help.h.
 */

/* ------------------------------------------------------------------------------------ */
/*
 * gasneti_semaphore_t
 *
 * This is a simple busy-waiting semaphore used, for instance, to control access to
 * some resource of known multiplicity.
 *
 * Unless GASNETI_SEMAPHORES_NOT_SIGNALSAFE is defined, the operations "up" and "up_n"
 * are safe from signal context and the "trydown", "trydown_n" and "trydown_partial"
 * are non-blocking from signal context.  However, calls to the trydown* functions from
 * signal context might never succeed if the thead interrupted by the signal was to
 * execute a matching "up".
 *
 * When debugging a non-zero 'limit' (given in the static or dynamic initializer) will
 * be checked to detect usage errors.  A 'limit' of zero disables this checking.
 *
 *	GASNETI_SEMAPHORE_MAX
 *				Maximum value supported by the implementation.
 *	GASNETI_SEMAPHORE_INITIALIZER(value, limit)
 *				Static initializer macro.
 *	gasneti_semaphore_init(&sema, value, limit)
 *				Dynamic initializer
 *	gasneti_semaphore_destroy(&sema)
 *				Destructor for dynamicaly initialized semaphores.
 *	gasneti_semaphore_read(&sema)
 *				Returns current value - mainly useful for debugging.
 *	gasneti_semaphore_up(&sema)
 *				Increments value atomically.
 *	gasneti_semaphore_up_n(&sema, n)
 *				Adds 'n' to value atomically.
 *	gasneti_semaphore_trydown(&sema)
 *				Atomically reduce value by 1 if new value would be non-negative.
 *				Returns 1 if value was reduced, or 0 otherwise.
 *	gasneti_semaphore_trydown_n(&sema, n)
 *				Atomically reduce value by 'n' if new value would be non-negative.
 *				Returns 'n' if value was reduced, or 0 otherwise.
 *	gasneti_semaphore_trydown_partial(&sema, n)
 *				Atomically reduce value by up to 'n', such that value is non-negative.
 *				Returns value in range 0 to 'n', indicating amount value was reduced.
 */

#if defined(GASNETI_FORCE_GENERIC_SEMAPHORES) || /* for debugging */ \
    defined(GASNETI_USING_GENERIC_ATOMICOPS)  || /* avoids double locking */ \
    !GASNETI_THREADS                          || /* avoids complexity of atomic algorithms */ \
    !(defined(GASNETI_HAVE_ATOMIC_CAS) || defined(GASNETI_HAVE_ATOMIC_ADD_SUB)) /* lack needed ops */
  #define GASNETI_USING_GENERIC_SEMAPHORES 1
#endif

#if defined(GASNETI_USING_GENERIC_SEMAPHORES) 
  /* Generic mutex-based implementation */
  typedef struct {
    gasneti_mutex_t		lock;
    gasneti_atomic_val_t	count;	 /* keeps size consistent beween SEQ and PAR builds */
  } _gasneti_semaphore_t;
  #define _GASNETI_SEMAPHORE_INITIALIZER(N) {GASNETI_MUTEX_INITIALIZER, (N)}
  #define GASNETI_SEMAPHORE_MAX GASNETI_ATOMIC_MAX
  GASNETI_INLINE(_gasneti_semaphore_init)
  void _gasneti_semaphore_init(_gasneti_semaphore_t *s, int n) {
    gasneti_mutex_init(&(s->lock));
    s->count = n;
  }
  GASNETI_INLINE(_gasneti_semaphore_destroy)
  void _gasneti_semaphore_destroy(_gasneti_semaphore_t *s) {
    gasneti_mutex_destroy(&(s->lock));
  }
  GASNETI_INLINE(_gasneti_semaphore_read)
  gasneti_atomic_val_t _gasneti_semaphore_read(_gasneti_semaphore_t *s) {
    return s->count;	/* no lock required */
  }
  GASNETI_INLINE(_gasneti_semaphore_up)
  void _gasneti_semaphore_up(_gasneti_semaphore_t *s) {
    gasneti_mutex_lock(&(s->lock));
    s->count += 1;
    gasneti_mutex_unlock(&(s->lock));
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown)
  int _gasneti_semaphore_trydown(_gasneti_semaphore_t *s) {
    int retval;
    gasneti_mutex_lock(&(s->lock));
    retval = s->count;
    if_pt (retval != 0)
      s->count -= 1;
    gasneti_mutex_unlock(&(s->lock));
    return retval;
  }
  GASNETI_INLINE(_gasneti_semaphore_up_n)
  void _gasneti_semaphore_up_n(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    gasneti_mutex_lock(&(s->lock));
    s->count += n;
    gasneti_mutex_unlock(&(s->lock));
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown_partial)
  gasneti_atomic_val_t _gasneti_semaphore_trydown_partial(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    gasneti_atomic_val_t retval, old;
    gasneti_mutex_lock(&(s->lock));
    old = s->count;
    retval = MIN(old, n);
    s->count -= retval;
    gasneti_mutex_unlock(&(s->lock));
    return retval;
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown_n)
  gasneti_atomic_val_t _gasneti_semaphore_trydown_n(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    gasneti_mutex_lock(&(s->lock));
    if_pt (s->count >= n)
      s->count -= n;
    else
      n = 0;
    gasneti_mutex_unlock(&(s->lock));
    return n;
  }
#elif defined(GASNETI_HAVE_ATOMIC_CAS)
  /* Semi-generic implementation for CAS-capable systems */
  typedef gasneti_weakatomic_t _gasneti_semaphore_t;
  #define _GASNETI_SEMAPHORE_INITIALIZER gasneti_weakatomic_init
  #define GASNETI_SEMAPHORE_MAX GASNETI_ATOMIC_MAX
  GASNETI_INLINE(_gasneti_semaphore_init)
  void _gasneti_semaphore_init(_gasneti_semaphore_t *s, int n) {
    gasneti_weakatomic_set(s, n, GASNETI_ATOMIC_REL);
  }
  GASNETI_INLINE(_gasneti_semaphore_destroy)
  void _gasneti_semaphore_destroy(_gasneti_semaphore_t *s) {
    /* Nothing */
  }
  GASNETI_INLINE(_gasneti_semaphore_read)
  gasneti_atomic_val_t _gasneti_semaphore_read(_gasneti_semaphore_t *s) {
    return gasneti_weakatomic_read(s, 0);
  }
  GASNETI_INLINE(_gasneti_semaphore_up)
  void _gasneti_semaphore_up(_gasneti_semaphore_t *s) {
    gasneti_weakatomic_increment(s, GASNETI_ATOMIC_REL);
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown)
  int _gasneti_semaphore_trydown(_gasneti_semaphore_t *s) {
    int retval = 0;
    do {
      const gasneti_atomic_val_t old = gasneti_weakatomic_read(s, 0);
      if_pf (old == 0)
        break;
      retval = gasneti_weakatomic_compare_and_swap(s, old, old - 1, GASNETI_ATOMIC_ACQ_IF_TRUE);
    } while (PREDICT_FALSE(!retval));
    return retval;
  }
  GASNETI_INLINE(_gasneti_semaphore_up_n)
  void _gasneti_semaphore_up_n(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    #if GASNETI_HAVE_ATOMIC_ADD_SUB
      (void)gasneti_weakatomic_add(s, n, GASNETI_ATOMIC_REL);
    #else
      int swap;
      do {
	const gasneti_atomic_val_t old = gasneti_weakatomic_read(s, 0);
	swap = gasneti_weakatomic_compare_and_swap(s, old, old + n, GASNETI_ATOMIC_REL);
      } while (PREDICT_FALSE(!swap));
    #endif
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown_partial)
  gasneti_atomic_val_t _gasneti_semaphore_trydown_partial(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    gasneti_atomic_val_t retval = 0;
    int swap;
    do {
      const gasneti_atomic_val_t old = gasneti_weakatomic_read(s, 0);
      if_pf (old == 0)
        break;
      retval = MIN(old, n);
      swap = gasneti_weakatomic_compare_and_swap(s, old, old - retval, GASNETI_ATOMIC_ACQ_IF_TRUE);
    } while (PREDICT_FALSE(!swap));
    return retval;
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown_n)
  gasneti_atomic_val_t _gasneti_semaphore_trydown_n(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    int swap;
    do {
      const gasneti_atomic_val_t old = gasneti_weakatomic_read(s, 0);
      if_pf (old < n) {
        n = 0;
        break;
      }
      swap = gasneti_weakatomic_compare_and_swap(s, old, old - n, GASNETI_ATOMIC_ACQ_IF_TRUE);
    } while (PREDICT_FALSE(!swap));
    return n;
  }
#elif defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
  /* Semi-generic implementation for and-add-fetch capable systems */
  typedef gasneti_weakatomic_t _gasneti_semaphore_t;
  #define _GASNETI_SEMAPHORE_INITIALIZER gasneti_weakatomic_init
  #define GASNETI_SEMAPHORE_MAX GASNETI_ATOMIC_SIGNED_MAX
  GASNETI_INLINE(_gasneti_semaphore_init)
  void _gasneti_semaphore_init(_gasneti_semaphore_t *s, int n) {
    gasneti_weakatomic_set(s, n, GASNETI_ATOMIC_REL);
  }
  GASNETI_INLINE(_gasneti_semaphore_destroy)
  void _gasneti_semaphore_destroy(_gasneti_semaphore_t *s) {
    /* Nothing */
  }
  GASNETI_INLINE(_gasneti_semaphore_read)
  gasneti_atomic_val_t _gasneti_semaphore_read(_gasneti_semaphore_t *s) {
    const gasneti_atomic_sval_t tmp = gasneti_atomic_signed(gasneti_weakatomic_read(s, 0));
    return (tmp >= 0) ? tmp : 0;
  }
  GASNETI_INLINE(_gasneti_semaphore_up)
  void _gasneti_semaphore_up(_gasneti_semaphore_t *s) {
    gasneti_weakatomic_increment(s, GASNETI_ATOMIC_REL);
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown)
  int _gasneti_semaphore_trydown(_gasneti_semaphore_t *s) {
    int retval = 0;
    if_pt (gasneti_atomic_signed(gasneti_weakatomic_read(s, 0)) > 0) {
      if_pt (gasneti_atomic_signed(gasneti_weakatomic_subtract(s, 1, 0)) >= 0) {
        gasneti_local_rmb(); /* Acquire */
        retval = 1;
      } else {
        gasneti_weakatomic_increment(s, 0);
      }
    }
    return retval;
  }
  GASNETI_INLINE(_gasneti_semaphore_up_n)
  void _gasneti_semaphore_up_n(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    (void)gasneti_weakatomic_add(s, n, GASNETI_ATOMIC_REL);
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown_partial)
  gasneti_atomic_val_t _gasneti_semaphore_trydown_partial(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    int retval = 0;
    if_pt (gasneti_atomic_signed(gasneti_weakatomic_subtract(s, 1, 0)) >= 0) {
      gasneti_atomic_sval_t tmp = gasneti_atomic_signed(gasneti_weakatomic_subtract(s, n, 0));
      if_pt (tmp >= 0) {
        gasneti_local_rmb(); /* Acquire */
        retval = n;
      } else if (tmp >= -((gasneti_atomic_sval_t)n)) {
	retval = n + tmp;
        (void)gasneti_weakatomic_add(s, -tmp, 0);
      } else {
        /* retval = 0 already */
      }
    }
    return retval;
  }
  GASNETI_INLINE(_gasneti_semaphore_trydown_n)
  gasneti_atomic_val_t _gasneti_semaphore_trydown_n(_gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
    int retval = 0;
    if_pt (gasneti_atomic_signed(gasneti_weakatomic_read(s, 0)) >= n) {
      if_pt (gasneti_atomic_signed(gasneti_weakatomic_subtract(s, n, 0)) >= 0) {
        gasneti_local_rmb(); /* Acquire */
        retval = 1;
      } else {
        (void)gasneti_weakatomic_add(s, n, 0);
      }
    }
    return retval;
  }
#endif

#if defined(GASNETI_USING_GENERIC_SEMAPHORES) || defined(GASNETI_ATOMICOPS_NOT_SIGNALSAFE)
  #define GASNETI_SEMAPHORES_NOT_SIGNALSAFE 1
#endif

#define GASNETI_CACHE_PAD(SZ) (((SZ+GASNETI_CACHE_LINE_BYTES-1)&~(GASNETI_CACHE_LINE_BYTES-1))-(SZ))

typedef struct {
  #if GASNET_DEBUG
    _gasneti_semaphore_t		S;
    gasneti_atomic_val_t	limit;
    char			_pad[GASNETI_CACHE_PAD(sizeof(gasneti_atomic_val_t)+sizeof(_gasneti_semaphore_t))];
  #else
    _gasneti_semaphore_t		S;
    char			_pad[GASNETI_CACHE_PAD(sizeof(_gasneti_semaphore_t))];
  #endif
} gasneti_semaphore_t;

#if GASNET_DEBUG
  #define GASNETI_SEMAPHORE_INITIALIZER(N,L) {_GASNETI_SEMAPHORE_INITIALIZER(N), (L),}
  #define GASNETI_SEMA_CHECK(_s)	do {                    \
      gasneti_atomic_val_t _tmp = _gasneti_semaphore_read(&(_s)->S); \
      gasneti_assert(_tmp <= GASNETI_SEMAPHORE_MAX);            \
      gasneti_assert((_tmp <= (_s)->limit) || !(_s)->limit);    \
    } while (0)
#else
  #define GASNETI_SEMAPHORE_INITIALIZER(N,L) {_GASNETI_SEMAPHORE_INITIALIZER(N),}
  #define GASNETI_SEMA_CHECK(_s)	do {} while(0)
#endif

/* gasneti_semaphore_init */
GASNETI_INLINE(gasneti_semaphore_init)
void gasneti_semaphore_init(gasneti_semaphore_t *s, int n, gasneti_atomic_val_t limit) {
  gasneti_assert(limit <= GASNETI_SEMAPHORE_MAX);
  _gasneti_semaphore_init(&(s->S), n);
  #if GASNET_DEBUG
    s->limit = limit;
  #endif
  GASNETI_SEMA_CHECK(s);
}

/* gasneti_semaphore_destroy */
GASNETI_INLINE(gasneti_semaphore_destroy)
void gasneti_semaphore_destroy(gasneti_semaphore_t *s) {
  GASNETI_SEMA_CHECK(s);
  _gasneti_semaphore_destroy(&(s->S));
}

/* gasneti_semaphore_read
 *
 * Returns current value of the semaphore
 */
GASNETI_INLINE(gasneti_semaphore_read)
gasneti_atomic_val_t gasneti_semaphore_read(gasneti_semaphore_t *s) {
  GASNETI_SEMA_CHECK(s);
  return _gasneti_semaphore_read(&(s->S));
}

/* gasneti_semaphore_up
 *
 * Atomically increments the value of the semaphore.
 * Since this just a busy-waiting semaphore, no waking operations are required.
 */
GASNETI_INLINE(gasneti_semaphore_up)
void gasneti_semaphore_up(gasneti_semaphore_t *s) {
  GASNETI_SEMA_CHECK(s);
  _gasneti_semaphore_up(&(s->S));
  GASNETI_SEMA_CHECK(s);
}

/* gasneti_semaphore_trydown
 *
 * If the value of the semaphore is non-zero, decrements it and returns non-zero.
 * If the value is zero, returns zero.  Otherwise returns 1;
 */
GASNETI_INLINE(gasneti_semaphore_trydown) GASNETI_WARN_UNUSED_RESULT
int gasneti_semaphore_trydown(gasneti_semaphore_t *s) {
  int retval;

  GASNETI_SEMA_CHECK(s);
  retval = _gasneti_semaphore_trydown(&(s->S));
  GASNETI_SEMA_CHECK(s);

  return retval;
}

/* gasneti_semaphore_up_n
 *
 * Increases the value of the semaphore by the indicated count.
 * Since this just a busy-waiting semaphore, no waking operations are required.
 */
GASNETI_INLINE(gasneti_semaphore_up_n)
void gasneti_semaphore_up_n(gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
  GASNETI_SEMA_CHECK(s);
  _gasneti_semaphore_up_n(&(s->S), n);
  GASNETI_SEMA_CHECK(s);
}

/* gasneti_semaphore_trydown_n
 *
 * Decrements the semaphore by 'n' or fails.
 * If the "old" value is zero, returns zero.
 */
GASNETI_INLINE(gasneti_semaphore_trydown_n) GASNETI_WARN_UNUSED_RESULT
gasneti_atomic_val_t gasneti_semaphore_trydown_n(gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
  gasneti_atomic_val_t retval;

  GASNETI_SEMA_CHECK(s);
  retval = _gasneti_semaphore_trydown_n(&(s->S), n);
  GASNETI_SEMA_CHECK(s);

  return retval;
}

/* gasneti_semaphore_trydown_partial
 *
 * Decrements the semaphore by as much as 'n' and returns the number of "counts" thus
 * obtained.  The decrement is the smaller of 'n' and the "old" value of the semaphore,
 * and this value is returned.
 * If the "old" value is zero, returns zero.
 */
GASNETI_INLINE(gasneti_semaphore_trydown_partial) GASNETI_WARN_UNUSED_RESULT
gasneti_atomic_val_t gasneti_semaphore_trydown_partial(gasneti_semaphore_t *s, gasneti_atomic_val_t n) {
  gasneti_atomic_val_t retval;

  GASNETI_SEMA_CHECK(s);
  retval = _gasneti_semaphore_trydown_partial(&(s->S), n);
  GASNETI_SEMA_CHECK(s);

  return retval;
}

/* ------------------------------------------------------------------------------------ */
/* gasneti_cond_t Condition variables - 
   Provides pthread_cond-like functionality, with error checking
  GASNETI_COND_INITIALIZER - value to statically initialize a gasneti_cond_t
  gasneti_cond_init(gasneti_cond_t *pc) - dynamically initialize a gasneti_cond_t   
  gasneti_cond_destroy(gasneti_cond_t *pc) - reclaim a gasneti_cond_t
  gasneti_cond_signal(gasneti_cond_t *pc) - 
    signal at least one waiter on a gasneti_cond_t, while holding the associated mutex
  gasneti_cond_broadcast(gasneti_cond_t *pc) - 
    signal all current waiters on a gasneti_cond_t, while holding the associated mutex
  gasneti_cond_wait(gasneti_cond_t *pc, gasneti_mutex_t *pl) - 
    release gasneti_mutex_t pl (which must be held) and block WITHOUT POLLING 
    until gasneti_cond_t pc is signalled by another thread, or until the system
    decides to wake this thread for no good reason (which it may or may not do).
    Upon wakeup for any reason, the mutex will be reacquired before returning.

    It's an error to wait if there is only one thread, and can easily lead to 
    deadlock if the last thread goes to sleep. No thread may call wait unless it
    can guarantee that (A) some other thread is still polling and (B) some other
    thread will eventually signal it to wake up. The system may or may not also 
    randomly signal threads to wake up for no good reason, so upon awaking the thread
    MUST verify using its own means that the condition it was waiting for 
    has actually been signalled (ie that the client-level "outer" condition has been set).

    In order to prevent races leading to missed signals and deadlock, signaling
    threads must always hold the associated mutex while signaling, and ensure the
    outer condition is set *before* releasing the mutex. Additionally, all waiters
    must check the outer condition *after* acquiring the same mutex and *before*
    calling wait (which atomically releases the lock and puts the thread to sleep).
*/

#if GASNETI_USE_TRUE_MUTEXES
  typedef pthread_cond_t            gasneti_cond_t;

  #define GASNETI_COND_INITIALIZER    PTHREAD_COND_INITIALIZER
  #define gasneti_cond_init(pc) do {                       \
      GASNETI_MUTEX_INITCLEAR(pc);                         \
      gasneti_assert_zeroret(pthread_cond_init((pc), NULL)); \
  } while (0)
  #define gasneti_cond_destroy(pc)    gasneti_assert_zeroret(pthread_cond_destroy(pc))

  #if defined(__crayx1) /* bug 993 - workaround for buggy pthread library */
    static gasneti_cond_t const gasneti_cond_staticinitialized = GASNETI_COND_INITIALIZER;
    #define GASNETI_COND_INIT_CHECK(pc) \
      (!memcmp(&gasneti_cond_staticinitialized,(pc),sizeof(gasneti_cond_t)) ? \
        (void)pthread_cond_init((pc), NULL) : (void)0 )
  #else
    #define GASNETI_COND_INIT_CHECK(pc) ((void)0)
  #endif

  #define gasneti_cond_signal(pc) do {                 \
      GASNETI_COND_INIT_CHECK(pc);                     \
      gasneti_assert_zeroret(pthread_cond_signal(pc)); \
    } while (0)
  #define gasneti_cond_broadcast(pc) do {                 \
      GASNETI_COND_INIT_CHECK(pc);                        \
      gasneti_assert_zeroret(pthread_cond_broadcast(pc)); \
    } while (0)

  #if GASNET_DEBUG
    #define gasneti_cond_wait(pc,pl)  do {                          \
      gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY());       \
      (pl)->owner = GASNETI_MUTEX_NOOWNER;                          \
      GASNETI_COND_INIT_CHECK(pc);                                  \
      gasneti_assert_zeroret(pthread_cond_wait(pc, &((pl)->lock))); \
      gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER);         \
      (pl)->owner = GASNETI_THREADIDQUERY();                        \
    } while (0)
  #else
    #define gasneti_cond_wait(pc,pl)  do {               \
      GASNETI_COND_INIT_CHECK(pc);                       \
      gasneti_assert_zeroret(pthread_cond_wait(pc, pl)); \
    } while (0)
  #endif
#else
  typedef char           gasneti_cond_t;
  #define GASNETI_COND_INITIALIZER  '\0'
  #define gasneti_cond_init(pc)       ((void)0)
  #define gasneti_cond_destroy(pc)    ((void)0)
  #define gasneti_cond_signal(pc)     ((void)0)
  #define gasneti_cond_broadcast(pc)  ((void)0)
  #define gasneti_cond_wait(pc,pl) \
      gasneti_fatalerror("There's only one thread: waiting on condition variable => deadlock")
#endif

/* ------------------------------------------------------------------------------------ */

/* Gasnet internal LIFO (stack) container.
 *
 * This data type provides a last in first out linked list implementation which
 * is suitable for use as a multiple-producer, multiple-consumer free list.
 * On architectires where possible, the synchronization is performed with
 * lock-free algorithms.  On the remaining platforms, gasneti_mutex's are used.
 *
 * This container type is independent of the type to be stored.  The only requirement
 * is that the first sizeof(void *) bytes of the object are used for the list linkage.
 *
 *
 * GASNETI_LIFO_INITIALIZER
 * 		Static initializer for empty LIFO
 * void gasneti_lifo_init(gasneti_lifo_head_t *lifo);
 *		Initializer for dynamically allocated LIFO
 * void gasneti_lifo_destroy(gasneti_lifo_head_t *lifo);
 *		Destructor for dynamically allocated LIFO
 * void *gasneti_lifo_pop(gasneti_lifo_head_t *lifo);
 *		Pop "top" element from the LIFO or NULL if it is empty
 * void gasneti_lifo_push(gasneti_lifo_head_t *lifo, void *elem);
 *		Push one element on the LIFO
 * void gasneti_lifo_push_many(gasneti_lifo_head_t *lifo, void *head, void *tail);
 *		Push a chain of linked elements on the LIFO
 * void gasneti_lifo_link(void *p, void *q);
 *		Build a chain (q follows p) for use with _lifo_push_many()
 * void *gasneti_lifo_next(void *elem);
 *		Get next element in a chain built with _lifo_link
 *
 * Unless GASNETI_LIFOS_NOT_SIGNALSAFE is defined, the operations "pop", "push", and
 * "push_many" are signal safe.  The operations "link" and "next" are always signal
 * safe since they don't involve access to a share data structure.
 */


/* Optional arch-specific code */
#if !GASNETI_THREADS
  /* No threads, so we use the mutex code that compiles away. */
#elif defined(GASNETI_USING_GENERIC_ATOMICOPS) || defined(GASNETI_USING_OS_ATOMICOPS)
  /* If not using inline asm in gasnet_atomicops.h, then don't try to here either. */
#elif defined(__i386__) /* x86 but NOT x86_64 */
  #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(PGI_WITH_REAL_ASM)
    typedef struct {
      volatile uintptr_t 	head;
      volatile uintptr_t 	ABA_tag;
      char			_pad[GASNETI_CACHE_PAD(2*sizeof(uintptr_t))];
    } gasneti_lifo_head_t;

    GASNETI_INLINE(_gasneti_lifo_push)
    void _gasneti_lifo_push(gasneti_lifo_head_t *p, void **head, void **tail) {
      /* RELEASE semantics: LOCK prefix is a full mb() */
      __asm__ __volatile__ ("1: movl	%0, %%eax	\n\t"	/* eax = p->head */
                            "movl	%%eax, %2	\n\t"	/* tail->next = eax */
    GASNETI_X86_LOCK_PREFIX "cmpxchgl	%1, %0		\n\t"	/* p->head = head */
                            "jne	1b"		/* retry on conflict */
                                : "=m" (p->head)
                                : "r" (head), "m" (*tail)
                                : "cc", "memory", "eax");
    }
    GASNETI_INLINE(_gasneti_lifo_pop)
    void *_gasneti_lifo_pop(gasneti_lifo_head_t *p) {
      /* ACQUIRE semantics: LOCK prefix is a full mb() */
      register uintptr_t retval = p->head;
      __asm__ __volatile__ ("1: test	%0,%0		\n\t"	/* terminate loop ... */
                            "jz		2f		\n\t"	/*        ... on NULL */
                            "mov	(%0), %%ebx	\n\t"	/* ebx = p->head->next */
                            "lea	1(%3), %%ecx	\n\t"	/* ecx = ABA_tag + 1 */ 
    GASNETI_X86_LOCK_PREFIX "cmpxchg8b	%1		\n\t"	/* p->(head,ABA_tag) = (ebx,ecx) */
                            "jne	1b		\n\t"	/* retry w/ updated (eax,edx) */
                            "2:"
                                : "=a" (retval)
                                : "m" (p->head), "a" (retval), "d" (p->ABA_tag)
                                : "cc", "memory", "ebx", "ecx");
      return (void *)retval;
    }
    GASNETI_INLINE(_gasneti_lifo_init)
    void _gasneti_lifo_init(gasneti_lifo_head_t *p) {
      p->head = 0;
    }
    GASNETI_INLINE(_gasneti_lifo_destroy)
    void _gasneti_lifo_destroy(gasneti_lifo_head_t *p) {
      /* NOTHING */
    }
    #define GASNETI_LIFO_INITIALIZER	{0,}
    #define GASNETI_HAVE_ARCH_LIFO	1
  #endif
#elif defined(_POWER) || defined(__PPC__) || defined(__ppc__) || defined(__ppc64__)
  /* PowerPPC ids:
   * AIX: _POWER
   * Darwin: __ppc__ or __ppc64__
   * Linux: __PPC__
   */
  #if defined(__GNUC__)
    typedef struct {
      /* Ensure list head pointer is the only item on its cache line.
       * This prevents a live-lock which would result if a list element fell
       * on the same cache line.
       */
      char		_pad0[GASNETI_CACHE_LINE_BYTES];
      volatile void	**head;
      char		_pad1[GASNETI_CACHE_PAD(sizeof(void **))];
    } gasneti_lifo_head_t;

    GASNETI_INLINE(_gasneti_lifo_push)
    void _gasneti_lifo_push(gasneti_lifo_head_t *p, void **head, void **tail) {
      /* Roughly based on Appendix D of IBM's "Programming Environments Manual for 64-bit Microprocessors."
       * The key is moving the store to tail->next outside the loop and rechecking tmp1==tmp2 inside.
       * This is needed because a store in the l[wd]arx/st[wd]cx interval can lead to livelock.
       */
      /* RELEASE semantics: 'sync' is wmb after the write to tail->next */
      register uintptr_t addr = (uintptr_t)(&p->head);
      register uintptr_t tmp1, tmp2;
      #if (SIZEOF_VOID_P == 4)
        __asm__ __volatile__ ("lwz	%3,0(%0)   \n\t" /* tmp1 = p->head */
			      "1: mr	%4,%3      \n\t" /* tmp2 = tmp1 */
			      "stw	%3,0(%2)   \n\t" /* tail->next = tmp1 */
			      GASNETI_PPC_WMB_ASM "\n\t" /* wmb */
			      "2: lwarx	%3,0,%0    \n\t" /* reload tmp1 = p->head */
			      "cmpw	%3,%4      \n\t" /* check tmp1 still == tmp2 */
			      "bne-	1b         \n\t" /* retry if p->head changed since starting */
			      "stwcx.	%1,0,%0    \n\t" /* p->head = head */
			      "bne-	2b         \n\t" /* retry on conflict */
			      GASNETI_PPC_RMB_ASM
				: "=b" (addr), "=r" (head), "=b" (tail), "=r" (tmp1), "=r" (tmp2)
				: "0" (addr), "1" (head), "2" (tail) 
				: "memory", "cc");
      #elif (SIZEOF_VOID_P == 8)
        __asm__ __volatile__ ("ld	%3,0(%0)   \n\t" /* tmp1 = p->head */
			      "1: mr	%4,%3      \n\t" /* tmp2 = tmp1 */
			      "std	%3,0(%2)   \n\t" /* tail->next = tmp1 */
			      GASNETI_PPC_WMB_ASM "\n\t" /* wmb */
			      "2: ldarx	%3,0,%0    \n\t" /* reload tmp1 = p->head */
			      "cmpd	%3,%4      \n\t" /* check tmp1 still == tmp2 */
			      "bne-	1b         \n\t" /* retry if p->head changed since starting */
			      "stdcx.	%1,0,%0    \n\t" /* p->head = head */
			      "bne-	2b"		 /* retry on conflict */
			      GASNETI_PPC_RMB_ASM
				: "=b" (addr), "=r" (head), "=b" (tail), "=r" (tmp1), "=r" (tmp2)
				: "0" (addr), "1" (head), "2" (tail) 
				: "memory", "cc");
      #else
        #error "PPC w/ unknown word size"
      #endif
    }
    GASNETI_INLINE(_gasneti_lifo_pop)
    void *_gasneti_lifo_pop(gasneti_lifo_head_t *p) {
      /* ACQUIRE semantics: 'isync' between read of head and head->next */
      register uintptr_t addr = (uintptr_t)(&p->head);
      register uintptr_t head, next;
      if_pf (p->head == NULL) {
	/* One expects the empty list case to be the most prone to contention because
	 * many threads may be continuously polling for it become non-empty.  The l[wd]arx
	 * involves obtaining the cache line in an Exclusive state, while this normal
	 * load does not.  Thus this redundant check is IBM's recommended practice.
	 */
	return NULL;
      }
      #if (SIZEOF_VOID_P == 4)
        __asm__ __volatile__ ("1: lwarx	%1,0,%0    \n\t" /* head = p->head */
			      "cmpwi	0,%1,0     \n\t" /* head == NULL? */
			      "beq-	2f         \n\t" /* end on NULL */
			      GASNETI_PPC_RMB_ASM "\n\t" /* rmb */
			      "lwz	%2,0(%1)   \n\t" /* next = head->next */
			      "stwcx.	%2,0,%0    \n\t" /* p->head = next */
			      "bne-	1b         \n\t" /* retry on conflict */
			      "2: "
				: "=b" (addr), "=b" (head), "=r" (next)
				: "0" (addr)
				: "memory", "cc");
      #elif (SIZEOF_VOID_P == 8)
        __asm__ __volatile__ ("1: ldarx	%1,0,%0    \n\t" /* head = p->head */
			      "cmpdi	0,%1,0     \n\t" /* head == NULL? */
			      "beq-	2f         \n\t" /* end on NULL */
			      GASNETI_PPC_RMB_ASM "\n\t" /* rmb */
			      "ld	%2,0(%1)   \n\t" /* next = head->next */
			      "stdcx.	%2,0,%0    \n\t" /* p->head = next */
			      "bne-	1b         \n\t" /* retry on conflict */
			      "2: "
				: "=b" (addr), "=b" (head), "=r" (next)
				: "0" (addr)
				: "memory", "cc");
      #else
        #error "PPC w/ unknown word size"
      #endif
      return (void *)head;
    }
    GASNETI_INLINE(_gasneti_lifo_init)
    void _gasneti_lifo_init(gasneti_lifo_head_t *p) {
      p->head = NULL;
    }
    GASNETI_INLINE(_gasneti_lifo_destroy)
    void _gasneti_lifo_destroy(gasneti_lifo_head_t *p) {
      /* NOTHING */
    }
    #define GASNETI_LIFO_INITIALIZER	{{0,}, NULL,}
    #define GASNETI_HAVE_ARCH_LIFO	1
  #elif defined(__xlC__)
    typedef struct {
      /* Ensure list head pointer is the only item on its cache line.
       * This prevents a live-lock which would result if a list element fell
       * on the same cache line.
       * XXX: Can't use GASNETI_CACHE_LINE_BYTES w/o some extra indirection.
       */
      char		_pad0[128];
      volatile void	**head;
      char		_pad1[128 - sizeof(void **)];
    } gasneti_lifo_head_t;

    /* See the GCC versions above for explanation */

    static void _gasneti_lifo_push(gasneti_lifo_head_t *p, void **head, void **tail);
    #if (SIZEOF_VOID_P == 4)
      #pragma mc_func _gasneti_lifo_push {\
        /* ARGS: r3 = p, r4 = head, r5 = tail  LOCAL: r0 = tmp2, r2 = tmp1, r6 = addr */ \
	"80430080"	/* lwz		r2,128(r3)	*/ \
	"38c30080"	/* addi		r6,r3,128	*/ \
	"7c401378"	/* 1: mr	r0,r2		*/ \
	"90450000"	/* stw		r2,0(r5)	*/ \
	GASNETI_PPC_WMB_ASM				\
	"7c403028"	/* 2: lwarx	r2,0,r6		*/ \
	"7c020000"	/* cmpw		r2,r0		*/ \
	"40a2ffec"	/* bne-		1b		*/ \
	"7c80312d"	/* stwcx.	r4,0,r6		*/ \
	"40a2fff0"	/* bne-		2b		*/ \
	GASNETI_PPC_RMB_ASM				\
      }
      #pragma reg_killed_by _gasneti_lifo_push cr0, gr0, gr2, gr6
    #elif (SIZEOF_VOID_P == 8)
      #pragma mc_func _gasneti_lifo_push {\
        /* ARGS: r3 = p, r4 = head, r5 = tail  LOCAL: r0 = tmp2, r9 = tmp1, r6 = addr */ \
	"e9230080"	/* ld		r9,128(r3)	*/ \
	"38c30080"	/* addi		r6,r3,128	*/ \
	"7d204b78"	/* 1: mr	r0,r9		*/ \
	"f9250000"	/* std		r9,0(r5)	*/ \
	GASNETI_PPC_WMB_ASM				\
	"7d2030a8"	/* 2: ldarx	r9,r0,r6	*/ \
	"7c290000"	/* cmpd		r9,r0		*/ \
	"40a2ffec"	/* bne-		1b		*/ \
	"7c8031ad"	/* stdcx.	r4,r0,r6	*/ \
	"40a2fff0"	/* bne-		2b		*/ \
	GASNETI_PPC_RMB_ASM				\
      }
      #pragma reg_killed_by _gasneti_lifo_push cr0, gr0, gr6, gr9
    #else
      #error "PPC w/ unknown word size"
    #endif

    static void *_gasneti_lifo_pop(gasneti_lifo_head_t *p);
    #if (SIZEOF_VOID_P == 4)
      #pragma mc_func _gasneti_lifo_pop {\
        /* ARGS: r3 = p  LOCAL: r0 = next, r2 = head */ \
	"80430080"	/* lwz		r2,128(r3)	*/ \
	"38630080"	/* addi		r3,r3,128	*/ \
	"2c020000"	/* cmpwi	r2,0		*/ \
	"38400000"	/* li		r2,0		*/ \
	"41820020"	/* beq-		2f		*/ \
	"7c401828"	/* 1: lwarx	r2,0,r3		*/ \
	"2c020000"	/* cmpwi	r2,0		*/ \
	"41820014"	/* beq-		2f		*/ \
	GASNETI_PPC_RMB_ASM				\
	"80020000"	/* lwz		r0,0(r2)	*/ \
	"7c00192d"	/* stwcx.	r0,0,r3		*/ \
	"40a2ffe8"	/* bne-		1b		*/ \
	"7c431378"	/* 2: mr	r3,r2		*/ \
      }
      #pragma reg_killed_by _gasneti_lifo_pop cr0, gr0, gr2
    #elif (SIZEOF_VOID_P == 8)
      #pragma mc_func _gasneti_lifo_pop {\
        /* ARGS: r3 = p  LOCAL: r0 = next, r4 = head */ \
	"e8830080"	/* ld		r4,128(r3)	*/ \
	"38630080"	/* addi		r3,r3,128	*/ \
	"2c240000"	/* cmpdi	r4,0		*/ \
	"38800000"	/* li		r4,0		*/ \
	"41820020"	/* beq-		2f		*/ \
	"7c8018a8"	/* 1: ldarx	r4,0,r3		*/ \
	"2c240000"	/* cmpdi	r4,0		*/ \
	"41820014"	/* beq-		2f		*/ \
	GASNETI_PPC_RMB_ASM				\
	"e8040000"	/* ld		r0,0(r4)	*/ \
	"7c0019ad"	/* stdcx.	r0,0,r3		*/ \
	"40a2ffe8"	/* bne-		1b		*/ \
	"7c832378"	/* mr		r3,r4		*/ \
      }
      #pragma reg_killed_by _gasneti_lifo_pop cr0, gr0, gr4
    #else
      #error "PPC w/ unknown word size"
    #endif
    GASNETI_INLINE(_gasneti_lifo_init)
    void _gasneti_lifo_init(gasneti_lifo_head_t *p) {
      p->head = NULL;
    }
    GASNETI_INLINE(_gasneti_lifo_destroy)
    void _gasneti_lifo_destroy(gasneti_lifo_head_t *p) {
      /* NOTHING */
    }
    #define GASNETI_LIFO_INITIALIZER	{{0,}, NULL,}
    #define GASNETI_HAVE_ARCH_LIFO	1
  #endif
#else
  /* All the LL/SC platforms should be easy targets for porting the PPC asm as time allows.
   *
   * No Opteron or Itanium support yet because there is no CAS2 or DCSS (double-compare single-swap)
   * support for 8-byte pointers.  While the x86_64 architecture includes an optional cmpxchg16b (CAS2),
   * no current CPU implements it.  For ia64, we lack even an optional CAS2 or DCSS.
   * The CS literature offers many ways to simulate CAS2 or DCSS using just CAS (cmpxchg8b), but
   * they all are either very complex and/or require thread-specific data to help resolve the ABA
   * problem.  I'll continue to look into this.  -PHH 2006.01.19
   */
#endif

/* Generic mutex-based default implementation */
#ifndef GASNETI_HAVE_ARCH_LIFO
    typedef struct {
      gasneti_mutex_t		lock;
      void			**head;
      char			_pad[GASNETI_CACHE_PAD(sizeof(gasneti_mutex_t)+sizeof(void **))];
    } gasneti_lifo_head_t;

    GASNETI_INLINE(_gasneti_lifo_push)
    void _gasneti_lifo_push(gasneti_lifo_head_t *p, void **head, void **tail) {
      gasneti_mutex_lock(&(p->lock));
      *tail = p->head;
      p->head = head;
      gasneti_mutex_unlock(&(p->lock));
    }
    GASNETI_INLINE(_gasneti_lifo_pop)
    void *_gasneti_lifo_pop(gasneti_lifo_head_t *p) {
      void **elem;
      gasneti_mutex_lock(&(p->lock));
      elem = p->head;
      if_pt (elem != NULL) {
        p->head = *elem;
      }
      gasneti_mutex_unlock(&(p->lock));
      return (void *)elem;
    }
    GASNETI_INLINE(_gasneti_lifo_init)
    void _gasneti_lifo_init(gasneti_lifo_head_t *p) {
      gasneti_mutex_init(&(p->lock));
      p->head = NULL;
    }
    GASNETI_INLINE(_gasneti_lifo_destroy)
    void _gasneti_lifo_destroy(gasneti_lifo_head_t *p) {
      gasneti_mutex_destroy(&(p->lock));
    }
    #define GASNETI_LIFO_INITIALIZER	{ GASNETI_MUTEX_INITIALIZER, NULL }
    #define GASNETI_HAVE_ARCH_LIFO	0
    #define GASNETI_LIFOS_NOT_SIGNALSAFE 1
#endif
    

/* Initializer for dynamically allocated lifo heads */
GASNETI_INLINE(gasneti_lifo_init)
void gasneti_lifo_init(gasneti_lifo_head_t *lifo) {
  gasneti_assert(lifo != NULL);
  _gasneti_lifo_init(lifo);
}

/* Destructor for dynamically allocated lifo heads */
GASNETI_INLINE(gasneti_lifo_destroy)
void gasneti_lifo_destroy(gasneti_lifo_head_t *lifo) {
  gasneti_assert(lifo != NULL);
  _gasneti_lifo_destroy(lifo);
}

/* Get one element from the LIFO or NULL if it is empty */
GASNETI_INLINE(gasneti_lifo_pop) GASNETI_MALLOC
void *gasneti_lifo_pop(gasneti_lifo_head_t *lifo) {
  gasneti_assert(lifo != NULL);
  return _gasneti_lifo_pop(lifo);
}

/* Push element on the LIFO */
GASNETI_INLINE(gasneti_lifo_push)
void gasneti_lifo_push(gasneti_lifo_head_t *lifo, void *elem) {
  gasneti_assert(lifo != NULL);
  gasneti_assert(elem != NULL);
  _gasneti_lifo_push(lifo, elem, elem);
}

/* Push a chain of linked elements on the LIFO */
GASNETI_INLINE(gasneti_lifo_push_many)
void gasneti_lifo_push_many(gasneti_lifo_head_t *lifo, void *head, void *tail) {
  gasneti_assert(lifo != NULL);
  gasneti_assert(head != NULL);
  gasneti_assert(tail != NULL);
  _gasneti_lifo_push(lifo, head, tail);
}

/* Build a chain (q follows p) for use with _lifo_push_many() */
GASNETI_INLINE(gasneti_lifo_link)
void gasneti_lifo_link(void *p, void *q) {
  gasneti_assert(p != NULL);
  gasneti_assert(q != NULL);
  *((void **)p) = q;
}

/* Get next element in a chain built with _lifo_link */
GASNETI_INLINE(gasneti_lifo_next)
void *gasneti_lifo_next(void *elem) {
  gasneti_assert(elem != NULL);
  return *((void **)elem);
}

/* ------------------------------------------------------------------------------------ */
GASNETI_END_EXTERNC

#endif
