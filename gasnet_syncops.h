/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_syncops.h,v $
 *     $Date: 2006/04/26 03:03:50 $
 * $Revision: 1.30 $
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
    defined(GASNETI_USE_GENERIC_ATOMICOPS)    || /* avoids double locking */ \
    !GASNETI_THREADS                          || /* avoids complexity of atomic algorithms */ \
    !(defined(GASNETI_HAVE_ATOMIC_CAS) || defined(GASNETI_HAVE_ATOMIC_ADD_SUB)) /* lack needed ops */
  #define GASNETI_USE_GENERIC_SEMAPHORES 1
#endif

#if defined(GASNETI_USE_GENERIC_SEMAPHORES) 
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

#if defined(GASNETI_USE_GENERIC_SEMAPHORES) || defined(GASNETI_ATOMICOPS_NOT_SIGNALSAFE)
  #define GASNETI_SEMAPHORES_NOT_SIGNALSAFE 1
#endif

#if 0
  /* This version can yield 0-byte padding, which upsets some compilers */
  /* Read as GASNETI_ALIGNUP(SZ,GASNETI_CACHE_LINE_BYTES) - SZ */
  #define GASNETI_CACHE_PAD(SZ) (((SZ+GASNETI_CACHE_LINE_BYTES-1)&~(GASNETI_CACHE_LINE_BYTES-1))-(SZ))
#else
  /* Read as GASNETI_ALIGNUP(SZ+1,GASNETI_CACHE_LINE_BYTES) - SZ */
  #define GASNETI_CACHE_PAD(SZ) (((SZ+GASNETI_CACHE_LINE_BYTES)&~(GASNETI_CACHE_LINE_BYTES-1))-(SZ))
#endif

typedef struct {
  #if GASNET_DEBUG
    _gasneti_semaphore_t	S;
    gasneti_atomic_val_t	limit;
    char			_pad[GASNETI_CACHE_PAD(sizeof(gasneti_atomic_val_t)+sizeof(_gasneti_semaphore_t))];
  #else
    _gasneti_semaphore_t	S;
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
/* Optional atomic operations for pointer-sized data.
 * FOR USE IN THIS FILE ONLY, UNTIL THIS IS MORE STABLE/COMPLETE.
 *
 * If GASNETI_HAVE_ATOMIC_PTR_CAS is defined:
 *	gasneti_atomic_ptr_t
 *	gasneti_atomic_ptr_init(val)
 *	gasneti_atomic_ptr_set(ptr, val)
 *	gasneti_atomic_ptr_read(ptr)
 *	gasneti_atomic_ptr_cas(ptr, oldval, newval, flags)
 *
 * If GASNETI_HAVE_ATOMIC_DBLPTR_CAS is defined:
 *	gasneti_atomic_dblptr_t
 *	gasneti_atomic_dblptr_init(hi, lo)
 *	gasneti_atomic_dblptr_set(ptr, hi, lo)
 *	gasneti_atomic_dblptr_read_lo(ptr)
 *	gasneti_atomic_dblptr_read_hi(ptr)
 *	gasneti_atomic_dblptr_cas2(ptr, oldhi, oldlo, newhi, newlo, flags)
 *	gasneti_atomic_dblptr_cas_lo(ptr, oldhi, oldlo, flags)
 *	gasneti_atomic_dblptr_cas_hi(ptr, newhi, newlo, flags)
 *
 * All values are uintptr_t, not (void*).
 *
 * NOTE: The set/read operations all currently lack a "flags" argument.
 * It can be added later if ever really needed.
 *
 * NOTE: These are for internal use only.  So, there are no "slow atomic" versions
 * for use with C++ compilers that lack the asm support present in the corresponding
 * C compiler.  However, it is possible to implement "special atomic" versions for
 * compilers with limited asm support (PGI < 6.1 and SunPro)
 */
#if defined(GASNETI_USE_GENERIC_ATOMICOPS) || defined(GASNETI_FORCE_OS_ATOMICOPS) || \
	(defined(GASNETI_USE_OS_ATOMICOPS) && !defined(_SGI_COMPILER_VERSION))
  /* If not using inline asm in gasnet_atomicops.h, then don't try to here either. */
  /* However, we make an exception for the MIPSPro compiler unless forced. */
#elif defined(__i386__) || defined(__i386) || defined(i386) || \
      defined(__i486__) || defined(__i486) || defined(i486) || \
      defined(__i586__) || defined(__i586) || defined(i586) || \
      defined(__i686__) || defined(__i686) || defined(i686)
  /* Default is suitable on all x86 platforms w/ native atomics */
  #define GASNETI_ATOMIC_PTR_DEFAULT

  #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__PATHCC__) || defined(PGI_WITH_REAL_ASM)
    typedef union {
      struct { volatile uintptr_t lo_ptr, hi_ptr; } ctr;	/* must be first for initializer */
      uint64_t u64;	/* For alignment */
    } gasneti_atomic_dblptr_t;

    #define gasneti_atomic_dblptr_init(hi,lo)     { { (uintptr_t)(lo), (uintptr_t)(hi) } }
    #define gasneti_atomic_dblptr_set(p,hi,lo)    do { (p)->ctr.lo_ptr = (uintptr_t)(lo); \
                                                       (p)->ctr.hi_ptr = (uintptr_t)(hi); \
                                                  } while (0)
    #define gasneti_atomic_dblptr_read_lo(p)      ((p)->ctr.lo_ptr)
    #define gasneti_atomic_dblptr_read_hi(p)      ((p)->ctr.hi_ptr)
    GASNETI_INLINE(_gasneti_atomic_dblptr_cas2)
    int _gasneti_atomic_dblptr_cas2(gasneti_atomic_dblptr_t *v, uintptr_t oldhi, uintptr_t oldlo, uintptr_t newhi, uintptr_t newlo) {
       __asm__ __volatile__ (
		GASNETI_X86_LOCK_PREFIX
		"cmpxchg8b	%0	\n\t"
		"sete		%b1	\n\t"
		"movzbl		%b1,%1"
		: "=m" (*v), "+a" (oldlo), "+d" (oldhi)
		: "b" (newlo), "c" (newhi), "m" (*v)
		: "cc" GASNETI_ATOMIC_MEM_CLOBBER);
       /* result in %eax (oldlo), for lack of available registers */
       return (int)oldlo;
    }
    #define GASNETI_HAVE_ATOMIC_DBLPTR_CAS 1
  #elif defined(__SUNPRO_C) || defined(__PGI)
    /* TO DO: "special" atomics versions for SunPro and PGI < 6.1 */
  #endif
#elif defined(__x86_64__) || defined(__amd64)
  #if (SIZEOF_VOID_P == 4)
    /* This should not happen */
  #elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__PATHCC__) || defined(PGI_WITH_REAL_ASM)
    typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
    #define gasneti_atomic_ptr_init(_v)		{ (_v) }
    #define gasneti_atomic_ptr_set(_p,_v)	do { (_p)->ctr = (_v); } while(0)
    #define gasneti_atomic_ptr_read(_p)		((_p)->ctr)
    GASNETI_INLINE(_gasneti_atomic_ptr_cas)
    int _gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *p, uintptr_t oldval, uintptr_t newval) {
      register unsigned char retval;
      register uintptr_t readval;
      __asm__ __volatile__ (
		GASNETI_X86_LOCK_PREFIX
		"cmpxchgq %3, %1	\n\t"
		"sete %0"
		: "=mq" (retval), "=m" (p->ctr), "=a" (readval)
		: "r" (newval), "m" (p->ctr), "a" (oldval)
		: "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      return (int)retval;
    }
    #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
  #elif defined(__SUNPRO_C) || defined(__PGI)
    /* TO DO: "special" atomics versions for SunPro and PGI < 6.1 */
  #endif
#elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
  #if (SIZEOF_VOID_P == 4)
    /* Default is suitable on all ILP32 MIPS platforms w/ native atomics */
    #define GASNETI_ATOMIC_PTR_DEFAULT

    #if defined(_MIPS_ISA) && (_MIPS_ISA >= 3) /* MIPS_ISA >= 3 => 64-bit capable CPU */
      #if defined(__GNUC__)
        typedef union {
          struct { volatile uintptr_t hi_ptr, lo_ptr; } ctr;	/* must be first for initializer */
          uint64_t u64;	/* For alignment */
        } gasneti_atomic_dblptr_t;

        #define gasneti_atomic_dblptr_init(hi,lo)     { { (uintptr_t)(hi), (uintptr_t)(lo) } }
        #define gasneti_atomic_dblptr_set(p,hi,lo)    do { (p)->ctr.hi_ptr = (uintptr_t)(hi); \
                                                           (p)->ctr.lo_ptr = (uintptr_t)(lo); \
                                                      } while (0)
        #define gasneti_atomic_dblptr_read_lo(p)      ((p)->ctr.lo_ptr)
        #define gasneti_atomic_dblptr_read_hi(p)      ((p)->ctr.hi_ptr)
        GASNETI_INLINE(_gasneti_atomic_dblptr_cas2)
        int _gasneti_atomic_dblptr_cas2(gasneti_atomic_dblptr_t *v, uintptr_t oldhi, uintptr_t oldlo, uintptr_t newhi, uintptr_t newlo) {
          uint64_t temp;
          uint64_t oldval = ((uint64_t)oldhi << 32) | oldlo;
          uint64_t newval = ((uint64_t)newhi << 32) | newlo;
          int retval = 0;
          __asm__ __volatile__ (
		  "1:			\n\t"
		  "lld	%1,%5		\n\t"	/* Load from *v */
		  "bne	%1,%3,2f	\n\t"	/* Break loop on mismatch */
		  "move	%0,%4		\n\t"	/* Copy newval to retval */
		  "scd	%0,%2		\n\t"	/* Try SC to store retval */
		  "beqz	%0,1b		\n"	/* Retry on contention */
		  "2:			"
		  : "+r" (retval), "=&r" (temp), "=m" (*v)
		  : "r" (oldval), "r" (newval), "R" (*v) );
          return retval;
	}
	#define GASNETI_HAVE_ATOMIC_DBLPTR_CAS 1
      #elif defined(_SGI_COMPILER_VERSION)
	/* NOTE: The compiler intrinsic is polymorphic */
        typedef union {
          struct { volatile uintptr_t hi_ptr, lo_ptr; } ctr;	/* must be first for initializer */
          uint64_t u64;	/* For alignment */
        } gasneti_atomic_dblptr_t;

        #define gasneti_atomic_dblptr_init(hi,lo)     { { (uintptr_t)(hi), (uintptr_t)(lo) } }
        #define gasneti_atomic_dblptr_set(p,hi,lo)    do { (p)->ctr.hi_ptr = (uintptr_t)(hi); \
                                                           (p)->ctr.lo_ptr = (uintptr_t)(lo); \
                                                      } while (0)
        #define gasneti_atomic_dblptr_read_lo(p)      ((p)->ctr.lo_ptr)
        #define gasneti_atomic_dblptr_read_hi(p)      ((p)->ctr.hi_ptr)
	
        GASNETI_INLINE(_gasneti_atomic_dblptr_cas2)
        int _gasneti_atomic_dblptr_cas2(gasneti_atomic_dblptr_t *v, uintptr_t oldhi, uintptr_t oldlo, uintptr_t newhi, uintptr_t newlo) {
          uint64_t oldval = ((uint64_t)oldhi << 32) | oldlo;
          uint64_t newval = ((uint64_t)newhi << 32) | newlo;
	  return __compare_and_swap(&v->u64, oldval, newval);
	}
	#define GASNETI_HAVE_ATOMIC_DBLPTR_CAS 1
      #endif /* __GNUC__ vs _SGI_COMPILER_VERSION*/
    #endif /* 64-bit CPU */
  #elif (SIZEOF_VOID_P == 8)
    #if defined(__GNUC__)
      typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
      #define _gasneti_atomic_ptr_read(p)      ((p)->ctr)
      #define _gasneti_atomic_ptr_set(p,v)     do { (p)->ctr = (v); } while(0)
      #define _gasneti_atomic_ptr_init(v)      { (v) }

      GASNETI_INLINE(_gasneti_atomic_ptr_cas)
      int _gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *v, uintptr_t oldval, uintptr_t newval) {
	uint64_t temp;
	int retval = 0;
        __asm__ __volatile__ (
		  "1:			\n\t"
		  "lld	%1,%5		\n\t"	/* Load from *v */
		  "bne	%1,%3,2f	\n\t"	/* Break loop on mismatch */
		  "move	%0,%4		\n\t"	/* Copy newval to retval */
		  "scd	%0,%2		\n\t"	/* Try SC to store retval */
		  "beqz	%0,1b		\n"	/* Retry on contention */
		  "2:			"
		  : "+r" (retval), "=&r" (temp), "=m" (*v)
		  : "r" (oldval), "r" (newval), "R" (*v) );
	return retval;
      }
    #elif defined(_SGI_COMPILER_VERSION)
      /* NOTE: The compiler intrinsic is polymorphic */
      typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
      #define _gasneti_atomic_ptr_read(p)      ((p)->ctr)
      #define _gasneti_atomic_ptr_set(p,v)     do { (p)->ctr = (v); } while(0)
      #define _gasneti_atomic_ptr_init(v)      { (v) }

      GASNETI_INLINE(_gasneti_atomic_ptr_cas)
      int _gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *v, uintptr_t oldval, uintptr_t newval) {
        return __compare_and_swap(&v->ctr, oldval, newval);
      }
      #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
    #endif /* __GNUC__ vs _SGI_COMPILER_VERSION*/
  #endif /* ILP32 vs LP64 */
#elif defined(__sparc) || defined(__sparc__)
  #if (SIZEOF_VOID_P == 4)
    /* Default is suitable on all ILP32 platforms w/ native atomics */
    #define GASNETI_ATOMIC_PTR_DEFAULT

    #if defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_ULTRASPARC)
      /* SPARC v9 or V8plus ISA = ILP32 on a 64-bit capable CPU */
      #if defined(__GNUC__)
        typedef union {
          struct { volatile uintptr_t hi_ptr, lo_ptr; } ctr;	/* must be first for initializer */
          uint64_t u64;	/* For alignment */
        } gasneti_atomic_dblptr_t;

        #define gasneti_atomic_dblptr_init(hi,lo)     { { (uintptr_t)(hi), (uintptr_t)(lo) } }
        #define gasneti_atomic_dblptr_set(p,hi,lo)    do { (p)->ctr.hi_ptr = (uintptr_t)(hi); \
                                                           (p)->ctr.lo_ptr = (uintptr_t)(lo); \
                                                      } while (0)
        #define gasneti_atomic_dblptr_read_lo(p)      ((p)->ctr.lo_ptr)
        #define gasneti_atomic_dblptr_read_hi(p)      ((p)->ctr.hi_ptr)
        GASNETI_INLINE(_gasneti_atomic_dblptr_cas2)
        int _gasneti_atomic_dblptr_cas2(gasneti_atomic_dblptr_t *v, uintptr_t oldhi, uintptr_t oldlo, uintptr_t newhi, uintptr_t newlo) {
	  /* This is more complex than one might expect, because the ILP32 ABI puts 64-bit
 	   * types in 2 adjacent regs while the casx instruction needs them in a single register.
	   */
	  register int retval, tmp;
          __asm__ __volatile__ ( 
		"sllx	%0,32,%0	\n\t"	/* retval = newhi << 32 */
		"sllx	%1,32,%1	\n\t"	/* tmp = oldhi << 32 */
		"or	%0,%6,%0	\n\t"	/* retval |= newlo */
		"or	%1,%8,%1	\n\t"	/* tmp |= oldlo */
		"casx	[%3],%1,%0	\n\t"	/* atomic CAS, with read value -> retval */
		"xor	%1,%0,%1	\n\t"	/* tmp = 0 IFF retval == tmp */
		"cmp	%%g0,%1		\n\t"	/* set/clear carry bit */
		"subx	%%g0,-1,%0"		/* yield retval = 0 or 1 */
		: "=h"(retval), "=h"(tmp), "=m"(*v)
		: "r"(v), "m"(*v), "0"(newhi), "r"(newlo), "1"(oldhi), "r"(oldlo)
		: "cc" );
	  return retval;
        }
        #define GASNETI_HAVE_ATOMIC_DBLPTR_CAS 1
      #elif defined(__SUNPRO_C)
        /* TO DO: "special" atomics versions for SunPro compiler */
      #endif
    #endif /* V9 or V8plus */
  #elif (SIZEOF_VOID_P == 8)
    /* TO DO: No SPARC LP64 support yet */
  #endif /* ILP32 vs LP64 */
#elif defined(__alpha__) || defined(__alpha) /* DEC Alpha */
  #if (SIZEOF_VOID_P == 4)
    /* No ILP32 support */
  #elif (SIZEOF_VOID_P == 8)
    #if defined(__GNUC__)
      typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
      #define _gasneti_atomic_ptr_read(p)      ((p)->ctr)
      #define _gasneti_atomic_ptr_set(p,v)     do { (p)->ctr = (v); } while(0)
      #define _gasneti_atomic_ptr_init(v)      { (v) }

      GASNETI_INLINE(_gasneti_atomic_ptr_cas)
      int _gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *p, uintptr_t oldval, uintptr_t newval) {
        unsigned long ret;
        __asm__ __volatile__ (
		"1:	ldl_l	%0,%1\n"	/* Load-linked of current value */
		"	cmpeq	%0,%2,%0\n"	/* compare to oldval */
		"	beq	%0,2f\n"	/* done/fail on mismatch (success/fail in ret) */
		"	mov	%3,%0\n"	/* copy newval to ret */
		"	stl_c	%0,%1\n"	/* Store-conditional of newval (success/fail in ret) */
		"	beq	%0,1b\n"	/* Retry on stl_c failure */
		"2:	"
       		: "=&r"(ret), "=m"(*p)
		: "r"(oldval), "r"(newval)
		: "cc");
        return ret;
      }
      #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
    #elif defined(__DECC) && defined(__osf__)
      typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
      #define _gasneti_atomic_ptr_read(p)      ((p)->ctr)
      #define _gasneti_atomic_ptr_set(p,v)     do { (p)->ctr = (v); } while(0)
      #define _gasneti_atomic_ptr_init(v)      { (v) }

      GASNETI_INLINE(_gasneti_atomic_ptr_cas)
      int _gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *p, uintptr_t oldval, uintptr_t newval) {
        return asm("1:	ldd_l	%v0,(%a0);"	/* Load-linked of current value to %v0 */
		   "	cmpeq	%v0,%a1,%v0;"	/* compare %v0 to oldval w/ result to %v0 */
		   "	beq	%v0,2f;"	/* done/fail on mismatch (success/fail in %v0) */
		   "	mov	%a2,%v0;"	/* copy newval to %v0 */
		   "	std_c	%v0,(%a0);"	/* Store-conditional of newval (success/fail in %v0) */
		   "	beq	%v0,1b;"	/* Retry on std_c failure */
		   "2:	", p, oldval, newval);  /* Returns value from %v0 */
      }
      #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
    #endif /* Switch(Compiler) */
  #endif /* ILP32 vs LP64 */
#elif defined(_POWER) || defined(__PPC__) || defined(__ppc__) || defined(__ppc64__)
  #if (SIZEOF_VOID_P == 4)
    /* Default is suitable on all ILP32 PPC platforms w/ native atomics */
    #define GASNETI_ATOMIC_PTR_DEFAULT
  #elif (SIZEOF_VOID_P == 8)
    #if defined(__GNUC__)
      typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
      #define gasneti_atomic_ptr_init(_v)	{ (_v) }
      #define gasneti_atomic_ptr_set(_p,_v)	do { (_p)->ctr = (_v); } while(0)
      #define gasneti_atomic_ptr_read(_p)	((_p)->ctr)
      GASNETI_INLINE(_gasneti_atomic_ptr_cas)
      int _gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *p, uintptr_t oldval, uintptr_t newval) {
        register uintptr_t result;
        __asm__ __volatile__ (
          "Lga.0.%=:\t"                   /* AIX assembler doesn't grok "0:"-type local labels */
	  "ldarx    %0,0,%1 \n\t"         /* load to result */
	  "xor.     %0,%0,%2 \n\t"        /* compare result w/ oldval */
	  "bne      Lga.1.%= \n\t"        /* branch on mismatch */
	  "stdcx.   %3,0,%1 \n\t"         /* store newval */
	  "bne-     Lga.0.%= \n\t"        /* retry on conflict */
	  "Lga.1.%=:	"
	  : "=&r"(result)
	  : "r" (p), "r"(oldval), "r"(newval)
	  : "cr0");
        return (result == 0);
      } 
      #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
    #elif defined(__xlC__)
      typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
      #define gasneti_atomic_ptr_init(_v)	{ (_v) }
      #define gasneti_atomic_ptr_set(_p,_v)	do { (_p)->ctr = (_v); } while(0)
      #define gasneti_atomic_ptr_read(_p)	((_p)->ctr)
      static int gasneti_atomic_ptr_cas_not(gasneti_atomic_ptr_t *p, uintptr_t oldval, uintptr_t newval);
      #pragma mc_func gasneti_atomic_ptr_cas_not {\
	/* ARGS: r3 = p, r4=oldval, r5=newval   LOCAL: r0 = tmp */ \
	"7c0018a8"	/* 0: ldarx   r0,0,r3	*/ \
	"7c002279"	/*    xor.    r0,r0,r4	*/ \
	"4082000c"	/*    bne-    1f	*/ \
	"7ca019ad"	/*    stdcx.  r5,0,r3	*/ \
	"40a2fff0"	/*    bne-    0b	*/ \
	"7c030378"	/* 1: mr      r3,r0	*/ \
	/* RETURN in r3 = 0 iff swap took place */ \
      }
      #pragma reg_killed_by gasneti_atomic_ptr_cas_not cr0, gr0
      #define _gasneti_atomic_ptr_cas(p, oldval, newval) \
					(gasneti_atomic_ptr_cas_not(p, oldval, newval) == 0)
      #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
    #endif /* __GNUC__ vs _xlC_ */
  #endif /* ILP32 vs LP64 */
#elif defined(__ia64__) || defined(__ia64) /* Itanium */
  #if (SIZEOF_VOID_P == 4)
    /* This should not happen */
  #elif defined(__INTEL_COMPILER)
    /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
    #include <ia64intrin.h>
    typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
    #define _gasneti_atomic_ptr_read(p)      ((p)->ctr)
    #define _gasneti_atomic_ptr_set(p,v)     do { (p)->ctr = (v); } while (0)
    #define _gasneti_atomic_ptr_init(v)      { (v) }

    #define _gasneti_atomic_ptr_cas(p,oval,nval) \
		(_InterlockedCompareExchange64_acq(&((p)->ctr),nval,oval) == (oval))
    #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
  #elif defined(__GNUC__)
    typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
    #define _gasneti_atomic_ptr_read(p)      ((p)->ctr)
    #define _gasneti_atomic_ptr_set(p,v)     do { (p)->ctr = (v); } while (0)
    #define _gasneti_atomic_ptr_init(v)      { (v) }

    GASNETI_INLINE(_gasneti_atomic_ptr_cas)
    int _gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *p, uintptr_t oldval, uintptr_t newval) {
      uintptr_t tmp;
      __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(oldval));
      __asm__ __volatile__ ("cmpxchg8.acq %0=[%1],%2,ar.ccv"
                                : "=r"(tmp) : "r"(p), "r"(newval) );
      return (tmp == oldval);
    }
    #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
  #elif defined(__HP_cc) /* HP C Itanium intrinsics */
    #include <machine/sys/inline.h>
    typedef struct { volatile uintptr_t ctr; } gasneti_atomic_ptr_t;
    #define _gasneti_atomic_ptr_read(p)      ((p)->ctr)
    #define _gasneti_atomic_ptr_set(p,v)     do { (p)->ctr = (v); } while (0)
    #define _gasneti_atomic_ptr_init(v)      { (v) }

    GASNETI_INLINE(_gasneti_atomic_ptr_cas)
    int _gasneti_atomic_ptr_cas(volatile uintptr_t *ptr, uintptr_t oldval, uintptr_t newval) {
      register uintptr_t tmp;
      _Asm_mov_to_ar(_AREG_CCV, oldval);
      tmp = _Asm_cmpxchg(_SZ_D, _SEM_ACQ, ptr, newval, 
                         _LDHINT_NONE, (_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE));
      return (tmp == oldval);
    }
    #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
  #endif /* Switch(Compiler) */
#endif

/* Fences and default implementations: */
#if defined(GASNETI_ATOMIC_PTR_DEFAULT)
  /* gasneti_atomic_t is suitable for gasneti_atomic_ptr_t (just need some casts) */
  #define GASNETI_HAVE_ATOMIC_PTR_CAS 1
  typedef gasneti_atomic_t gasneti_atomic_ptr_t;
  #define gasneti_atomic_ptr_init(_v)		gasneti_atomic_init((gasneti_atomic_val_t)(_v))
  #define gasneti_atomic_ptr_set(_p,_v)		gasneti_atomic_set(_p,(gasneti_atomic_val_t)(_v),0)
  #define gasneti_atomic_ptr_read(_p)		((uintptr_t)gasneti_atomic_read(_p,0))
  #define gasneti_atomic_ptr_cas(_p,_o,_n,_f)	gasneti_atomic_compare_and_swap(_p,(gasneti_atomic_val_t)(_o),(gasneti_atomic_val_t)(_n),_f)
#elif defined(GASNETI_HAVE_ATOMIC_PTR_CAS) && !defined(gasneti_atomic_ptr_cas)
  GASNETI_INLINE(gasneti_atomic_ptr_cas)
  int gasneti_atomic_ptr_cas(gasneti_atomic_ptr_t *p, uintptr_t oldval, uintptr_t newval, int flags) {
    _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
    { const int retval = _gasneti_atomic_ptr_cas(p,oldval,newval);
      _gasneti_atomic_fence_after_bool(flags, retval) /* no semi */
      return retval;
    }
  }
#endif
#if defined(GASNETI_HAVE_ATOMIC_DBLPTR_CAS)
  #ifndef GASNETI_HAVE_ATOMIC_PTR_CAS
    #error "GASNETI_HAVE_ATOMIC_DBLPTR_CAS defined w/o GASNETI_HAVE_ATOMIC_PTR_CAS"
  #endif
  #ifndef gasneti_atomic_dblptr_cas2
    GASNETI_INLINE(gasneti_atomic_dblptr_cas2)
    int gasneti_atomic_dblptr_cas2(gasneti_atomic_dblptr_t *p, uintptr_t oldhi, uintptr_t oldlo,
		                   uintptr_t newhi, uintptr_t newlo, int flags) {
      _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
      { const int retval = _gasneti_atomic_dblptr_cas2(p,oldhi,oldlo,newhi,newlo);
        _gasneti_atomic_fence_after_bool(flags, retval) /* no semi */
        return retval;
      }
    }
  #endif
  #if defined(gasneti_atomic_dblptr_cas_lo) && defined(gasneti_atomic_dblptr_cas_hi)
    /* Use platform-specific versions */
  #elif defined(gasneti_atomic_dblptr_cas_lo) || defined(gasneti_atomic_dblptr_cas_hi)
    #error "Define either both or neither of gasneti_atomic_dblptr_cas_{lo,hi}"
  #elif defined(GASNETI_HAVE_ATOMIC_PTR_CAS)
    /* Default is to build from normal single ptr cas when available,
     * NOTE: This default assumes standard naming of fields. */
    #define gasneti_atomic_dblptr_cas_lo(v,oldlo,newlo,flags) \
			gasneti_atomic_ptr_cas((gasneti_atomic_ptr_t*)(&(v)->ctr.lo_ptr),oldlo,newlo,flags)
    #define gasneti_atomic_dblptr_cas_hi(v,oldhi,newhi,flags) \
			gasneti_atomic_ptr_cas((gasneti_atomic_ptr_t*)(&(v)->ctr.hi_ptr),oldhi,newhi,flags)
  #else
    /* Apply flags wrappers to platform-specific _-prefixed versions */
    GASNETI_INLINE(gasneti_atomic_dblptr_cas_lo)
    int gasneti_atomic_dblptr_cas_lo(gasneti_atomic_dblptr_t *p, uintptr_t oldval, uintptr_t newval, int flags) {
      _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
      { const int retval = _gasneti_atomic_dblptr_cas_lo(p,oldval,newval);
        _gasneti_atomic_fence_after_bool(flags, retval) /* no semi */
        return retval;
      }
    }
    GASNETI_INLINE(gasneti_atomic_dblptr_cas_hi)
    int gasneti_atomic_dblptr_cas_hi(gasneti_atomic_dblptr_t *p, uintptr_t oldval, uintptr_t newval, int flags) {
      _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
      { const int retval = _gasneti_atomic_dblptr_cas_hi(p,oldval,newval);
        _gasneti_atomic_fence_after_bool(flags, retval) /* no semi */
        return retval;
      }
    }
  #endif
#endif


/* ------------------------------------------------------------------------------------ */

/* Gasnet internal LIFO (stack) container.
 *
 * This data type provides a last in first out linked list implementation which
 * is suitable for use as a multiple-producer, multiple-consumer free list.
 * On architectires where possible, the synchronization is performed with
 * lock-free/wait-free algorithms.  Elsewhere, gasneti_mutex's are used.
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
#elif defined(GASNETI_USE_GENERIC_ATOMICOPS) || defined(GASNETI_USE_OS_ATOMICOPS)
  /* If not using inline asm in gasnet_atomicops.h, then don't try to here either. */
#elif defined(_POWER) || defined(__PPC__) || defined(__ppc__) || defined(__ppc64__)
  /* PowerPPC ids:
   * AIX: _POWER
   * Darwin: __ppc__ or __ppc64__
   * Linux: __PPC__
   *
   * Among the platforms we currently support, PPC is unique in having an LL/SC
   * construct which allows a load between the LL and the SC.
   */
  #if defined(__GNUC__)
    /* Note use of "Lga.0.%=" for labels works around the AIX assembler, which doesn't like "1:" */
    typedef struct {
      /* Ensure list head pointer is the only item on its cache line.
       * This prevents a live-lock which would result if a list element fell
       * on the same cache line.
       */
      char			_pad0[GASNETI_CACHE_LINE_BYTES];
      gasneti_atomic_ptr_t	head;
      char			_pad1[GASNETI_CACHE_LINE_BYTES];
    } gasneti_lifo_head_t;

    GASNETI_INLINE(_gasneti_lifo_push)
    void _gasneti_lifo_push(gasneti_lifo_head_t *p, void **head, void **tail) {
      /* RELEASE semantics */
      uintptr_t oldhead;
      do {
	oldhead = gasneti_atomic_ptr_read(&p->head);
	*tail = (void *)oldhead;
      } while (!gasneti_atomic_ptr_cas(&p->head, oldhead, (uintptr_t)head, GASNETI_ATOMIC_REL));
    }

    GASNETI_INLINE(_gasneti_lifo_pop)
    void *_gasneti_lifo_pop(gasneti_lifo_head_t *p) {
      /* ACQUIRE semantics: 'isync' between read of head and head->next */
      register uintptr_t addr = (uintptr_t)(&p->head);
      register uintptr_t head, next;
      if_pf (gasneti_atomic_ptr_read(&p->head) == 0) {
	/* One expects the empty list case to be the most prone to contention because
	 * many threads may be continuously polling for it become non-empty.  The l[wd]arx
	 * involves obtaining the cache line in an Exclusive state, while this normal
	 * load does not.  Thus this redundant check is IBM's recommended practice.
	 */
	return NULL;
      }
      #if (SIZEOF_VOID_P == 4)
        __asm__ __volatile__ ("Lga.1.%=:	   \n\t"
			      "lwarx	%1,0,%0    \n\t" /* head = p->head */
			      "cmpwi	0,%1,0     \n\t" /* head == NULL? */
			      "beq-	Lga.2.%=   \n\t" /* end on NULL */
			      GASNETI_PPC_RMB_ASM "\n\t" /* rmb */
			      "lwz	%2,0(%1)   \n\t" /* next = head->next */
			      "stwcx.	%2,0,%0    \n\t" /* p->head = next */
			      "bne-	Lga.1.%=   \n"   /* retry on conflict */
			      "Lga.2.%=: "
				: "=b" (addr), "=b" (head), "=r" (next)
				: "0" (addr)
				: "memory", "cc");
      #elif (SIZEOF_VOID_P == 8)
        __asm__ __volatile__ ("Lga.1.%=:	   \n\t"
			      "ldarx	%1,0,%0    \n\t" /* head = p->head */
			      "cmpdi	0,%1,0     \n\t" /* head == NULL? */
			      "beq-	Lga.2.%=   \n\t" /* end on NULL */
			      GASNETI_PPC_RMB_ASM "\n\t" /* rmb */
			      "ld	%2,0(%1)   \n\t" /* next = head->next */
			      "stdcx.	%2,0,%0    \n\t" /* p->head = next */
			      "bne-	Lga.1.%=   \n"   /* retry on conflict */
			      "Lga.2.%=: "
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
      gasneti_atomic_ptr_set(&p->head, 0);
    }
    GASNETI_INLINE(_gasneti_lifo_destroy)
    void _gasneti_lifo_destroy(gasneti_lifo_head_t *p) {
      /* NOTHING */
    }
    #define GASNETI_LIFO_INITIALIZER	{{0,}, gasneti_atomic_ptr_init(0),}
    #define GASNETI_HAVE_ARCH_LIFO	1
  #elif defined(__xlC__)
    typedef struct {
      /* Ensure list head pointer is the only item on its cache line.
       * This prevents a live-lock which would result if a list element fell
       * on the same cache line.
       * XXX: Can't use GASNETI_CACHE_LINE_BYTES w/o some extra indirection.
       */
      char			_pad0[128];
      gasneti_atomic_ptr_t	head;
      char			_pad1[128 - sizeof(void **)];
    } gasneti_lifo_head_t;

    GASNETI_INLINE(_gasneti_lifo_push)
    void _gasneti_lifo_push(gasneti_lifo_head_t *p, void **head, void **tail) {
      /* RELEASE semantics */
      uintptr_t oldhead;
      do {
	oldhead = gasneti_atomic_ptr_read(&p->head);
	*tail = (void *)oldhead;
      } while (!gasneti_atomic_ptr_cas(&p->head, oldhead, (uintptr_t)head, GASNETI_ATOMIC_REL));
    }

    static void *_gasneti_lifo_pop(gasneti_lifo_head_t *p);
    /* ARGS: r3 = p  LOCAL: r0 = next, r4 = head */
    #if (SIZEOF_VOID_P == 4)
      #pragma mc_func _gasneti_lifo_pop {\
	"80830080"	/* lwz		r4,128(r3)	*/ \
	"38630080"	/* addi		r3,r3,128	*/ \
	"2c040000"	/* cmpwi	r4,0		*/ \
	"38800000"	/* li		r4,0		*/ \
	"41820020"	/* beq-		2f		*/ \
	"7c801828"	/* 1: lwarx	r4,0,r3		*/ \
	"2c040000"	/* cmpwi	r4,0		*/ \
	"41820014"	/* beq-		2f		*/ \
	GASNETI_PPC_RMB_ASM				\
	"80040000"	/* lwz		r0,0(r4)	*/ \
	"7c00192d"	/* stwcx.	r0,0,r3		*/ \
	"40a2ffe8"	/* bne-		1b		*/ \
	"7c832378"	/* 2: mr	r3,r4		*/ \
      }
    #elif (SIZEOF_VOID_P == 8)
      #pragma mc_func _gasneti_lifo_pop {\
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
    #else
      #error "PPC w/ unknown word size"
    #endif
    #pragma reg_killed_by _gasneti_lifo_pop cr0, gr0, gr4

    GASNETI_INLINE(_gasneti_lifo_init)
    void _gasneti_lifo_init(gasneti_lifo_head_t *p) {
      gasneti_atomic_ptr_set(&p->head, 0);
    }
    GASNETI_INLINE(_gasneti_lifo_destroy)
    void _gasneti_lifo_destroy(gasneti_lifo_head_t *p) {
      /* NOTHING */
    }
    #define GASNETI_LIFO_INITIALIZER	{{0,}, gasneti_atomic_ptr_init(0),}
    #define GASNETI_HAVE_ARCH_LIFO	1
  #endif
#elif defined(GASNETI_HAVE_ATOMIC_DBLPTR_CAS)
    /* Algorithm if we have a compare-and-swap for a type as wide as two pointers.
     * The lower half holds the head pointer, which the upper half hold a "tag"
     * which is advanced by one on each Pop to avoid the "classic ABA problem".
     */
    typedef struct {
      char		_pad0[GASNETI_CACHE_LINE_BYTES];
      gasneti_atomic_dblptr_t 	head_and_tag;
      char		_pad1[GASNETI_CACHE_LINE_BYTES];
    } gasneti_lifo_head_t;

    GASNETI_INLINE(_gasneti_lifo_push)
    void _gasneti_lifo_push(gasneti_lifo_head_t *p, void **head, void **tail) {
      uintptr_t oldhead;
      do {
	oldhead = gasneti_atomic_dblptr_read_lo(&p->head_and_tag);
	*tail = (void *)oldhead;
      } while (!gasneti_atomic_dblptr_cas_lo(&p->head_and_tag, oldhead, (uintptr_t)head, GASNETI_ATOMIC_REL));
    }
    GASNETI_INLINE(_gasneti_lifo_pop)
    void *_gasneti_lifo_pop(gasneti_lifo_head_t *p) {
      uintptr_t tag, oldhead, newhead;
      do {
	oldhead = gasneti_atomic_dblptr_read_lo(&p->head_and_tag);
	tag = gasneti_atomic_dblptr_read_hi(&p->head_and_tag);
	if_pf (!oldhead) break;
	newhead = (uintptr_t)(*(void **)oldhead);
      } while (!gasneti_atomic_dblptr_cas2(&p->head_and_tag, tag, oldhead, tag+1, newhead, GASNETI_ATOMIC_ACQ_IF_TRUE));
      return (void *)oldhead;
    }
    GASNETI_INLINE(_gasneti_lifo_init)
    void _gasneti_lifo_init(gasneti_lifo_head_t *p) {
      gasneti_atomic_dblptr_set(&p->head_and_tag, 0, 0);
    }
    GASNETI_INLINE(_gasneti_lifo_destroy)
    void _gasneti_lifo_destroy(gasneti_lifo_head_t *p) {
      /* NOTHING */
    }
    #define GASNETI_LIFO_INITIALIZER	{{0,}, gasneti_atomic_dblptr_init(0,0),}
    #define GASNETI_HAVE_ARCH_LIFO	1
#else
  /* The LL/SC algorithm used on the PPC will not work on the Alpha or MIPS, which don't
   * allow for the load we perform between the ll and the sc.  More complex algorithms are
   * probably possible.  I'll continue to look into this.  -PHH 2006.04.19
   *
   * No Opteron or Itanium support yet because there is no CAS2 or DCSS (double-compare single-swap)
   * support for 8-byte pointers.  While the x86_64 architecture includes an optional cmpxchg16b
   * (CAS2), no current CPU implements it.  For ia64, we lack even an optional CAS2 or DCSS.
   * The CS literature offers many ways to simulate CAS2 or DCSS using just CAS (cmpxchg8b), but
   * they all are either very complex and/or require thread-specific data to help resolve the ABA
   * problem.  I'll continue to look into this.  -PHH 2006.01.19
   *
   * One possible solution for all remaining platforms is "software ll/sc".  Using just pointer
   * CAS, one can implement an ideal LL/SC which allows for arbitrary loads and stores between
   * the LL and the SC.  This would require a compare-and-swap-pointer atomic operation.
   * In the contention-free case such an algorithm needs a thread-local data lookup, an rmb()
   * and two CAS operations, which may or may not make it competative with mutexes.  Because
   * such an algorithm is "wait free", it is expected to perform better under contention than
   * mutexes.
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
