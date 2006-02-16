/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_diagnostic.c,v $
 *     $Date: 2006/02/16 17:45:56 $
 * $Revision: 1.8 $
 * Description: GASNet internal diagnostics
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>

/* filthy hack to allow simultaneous use of gasnet-internal and test.h facilities */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef strdup
#undef strndup
#undef assert         

#define TEST_OMIT_CONFIGSTRINGS 1
#include <../tests/test.h>
#include <gasnet_handler.h>

/* this file should *only* contain symbols used for internal diagnostics,
   so that we can avoid needlessly linking it into production executables 
   everything except the main entry point should be static, to prevent namespace pollution
*/

GASNETT_IDENT(gasneti_IdentString_diagnostics, 
 "$GASNetDiagnostics: (<link>) INCLUDES gasnet_diagnostic.o $");

#if GASNET_PAR
  static void * thread_fn(void *arg);
#endif
static int num_threads = 1;
static int peer = -1;
static void * myseg = NULL;
static void * peerseg = NULL;
static int iters = 0;
static int iters2 = 0;

#define gasneti_diag_hidx_base 128
#define gasnetc_diag_hidx_base 160
#define gasnete_diag_hidx_base 200
static int gasneti_diag_havehandlers = 0; /* true iff caller has registered our handler table */

#ifdef GASNETC_DIAGNOSTICS_SETUP
  GASNETC_DIAGNOSTICS_SETUP /* can include helper source files, etc */
#endif
#ifdef GASNETE_DIAGNOSTICS_SETUP
  GASNETE_DIAGNOSTICS_SETUP /* can include helper source files, etc */
#endif

#ifndef GASNETC_RUN_DIAGNOSTICS_SEQ
#define GASNETC_RUN_DIAGNOSTICS_SEQ(iters) (0)
#endif
#ifndef GASNETC_RUN_DIAGNOSTICS_PAR
#define GASNETC_RUN_DIAGNOSTICS_PAR(iters,threadid,numthreads) (0)
#endif

#ifndef GASNETE_RUN_DIAGNOSTICS_SEQ
#define GASNETE_RUN_DIAGNOSTICS_SEQ(iters) (0)
#endif
#ifndef GASNETE_RUN_DIAGNOSTICS_PAR
#define GASNETE_RUN_DIAGNOSTICS_PAR(iters,threadid,numthreads) (0)
#endif


/* ------------------------------------------------------------------------------------ */
/*  misc sequential tests */

#if GASNET_DEBUG
  extern gasneti_auxseg_request_t gasneti_auxseg_dummy(gasnet_seginfo_t *auxseg_info);
  static void auxseg_test() {
    BARRIER();
    MSG0("auxseg test...");
    gasneti_auxseg_dummy((void *)(uintptr_t)-1); /* call self-test */
  }
#else
  #define auxseg_test() ((void)0)
#endif

static void mutex_test(int id);
static void spinlock_test(int id);
static void cond_test(int id);
static void malloc_test(int id);
static void progressfns_test(int id);

/* ------------------------------------------------------------------------------------ */
/* run iters iterations of diagnostics and return zero on success 
   must be called collectively by exactly one thread on each node
   in par mode, the test may internally spawn up to threadcnt threads
 */
extern int gasneti_run_diagnostics(int iter_cnt, int threadcnt, gasnet_seginfo_t const *seginfo) { 
  int i;
  int partner = (gasnet_mynode() ^ 1);
  if (partner == gasnet_nodes()) partner = gasnet_mynode();
  test_errs = 0;
  iters = iter_cnt;
  iters2 = iters*100;
  peer = gasnet_mynode()^1;
  if (peer == gasnet_nodes()) peer = gasnet_mynode();
  assert_always(seginfo);
  _test_seginfo = (gasnet_seginfo_t *)seginfo;
  for (i=0; i < (int)gasnet_nodes(); i++) {
    assert_always(_test_seginfo[i].size >= TEST_SEGSZ);
    assert_always((((uintptr_t)_test_seginfo[i].addr) % PAGESZ) == 0);
  }
  myseg = TEST_MYSEG();
  peerseg = TEST_SEG(peer);

  TEST_GENERICS_WARNING();

  auxseg_test();

  BARRIER();
  MSG0("sequential malloc test...");
  malloc_test(0);


  BARRIER();
  MSG0("gasneti_getPhysMemSz test...");
  { uint64_t val = gasneti_getPhysMemSz(0);
    if (val == 0) MSG("WARNING: gasneti_getPhysMemSz() failed to discover physical memory size.");
    else if (val > (1ULL<<50) || val < (1ULL<<20)) 
      ERR("gasneti_getPhysMemSz() got a ridiculous result: %llu bytes", (unsigned long long)val);
  }

  BARRIER();
  MSG0("sequential gasneti_mutex_t test...");
  mutex_test(0);

  BARRIER();
  MSG0("sequential gasneti_cond_t test...");
  cond_test(0);

  BARRIER();
  spinlock_test(0);

  BARRIER();
  progressfns_test(0);

  BARRIER();
  MSG0("sequential conduit tests...");
  BARRIER();
  test_errs += GASNETC_RUN_DIAGNOSTICS_SEQ(iters);
  BARRIER();
  test_errs += GASNETE_RUN_DIAGNOSTICS_SEQ(iters);
  BARRIER();

  #if GASNET_PAR
    num_threads = threadcnt;
    MSG0("spawning %i threads...", num_threads);
    test_createandjoin_pthreads(num_threads, &thread_fn, NULL, 0);
  #endif

  BARRIER();
  MSG0("GASNet internal diagnostics complete.");
  return test_errs;
}

#undef MSG0
#undef ERR
#define MSG0 THREAD_MSG0(id)
#define ERR  THREAD_ERR(id)

/* ------------------------------------------------------------------------------------ */
/*  mixed parallel / sequential tests */
/* ------------------------------------------------------------------------------------ */

static void malloc_test(int id) { 
  int i, cnt = 0;
  void **ptrs;
  gasneti_heapstats_t stats_before, stats_after;
  for (i=0; i < 100; i++) { /* try to trigger any warm-up allocations potentially caused by barrier */
    PTHREAD_BARRIER(num_threads);
  }

  if (!id) gasneti_getheapstats(&stats_before);
    
  PTHREAD_BARRIER(num_threads);

  gasneti_memcheck_all();
  ptrs = gasneti_malloc_allowfail(8);
  assert_always(ptrs);
  gasneti_free(ptrs);
  ptrs = gasneti_realloc(NULL,8);
  assert_always(ptrs);
  gasneti_free(ptrs);
  gasneti_free(NULL);

  PTHREAD_BARRIER(num_threads);

  ptrs = gasneti_calloc(iters,sizeof(void*));
  for (i = 0; i < iters; i++) assert_always(ptrs[i] == NULL);
  for (i = 0; i < iters2/num_threads; i++) {
    gasneti_memcheck_one();
    if (cnt == iters || (cnt > 0 && TEST_RAND_ONEIN(2))) {
      size_t idx = TEST_RAND(0,cnt-1);
      assert_always(ptrs[idx]);
      gasneti_memcheck(ptrs[idx]);
      if (TEST_RAND_ONEIN(2)) {
        gasneti_free(ptrs[idx]);
        cnt--;
        ptrs[idx] = ptrs[cnt];
        ptrs[cnt] = NULL;
      } else {
        ptrs[idx] = gasneti_realloc(ptrs[idx],TEST_RAND(1,16*1024));
      }
    } else {
      void *p;
      if (TEST_RAND_ONEIN(2)) {
        p = gasneti_malloc(TEST_RAND(1,1024));
      } else {
        p = gasneti_calloc(1,TEST_RAND(1,1024));
      }
      gasneti_memcheck(p);
      assert_always(p);
      assert_always(ptrs[cnt] == NULL);
      ptrs[cnt] = p;
      cnt++;
    }
  }
  gasneti_memcheck_all();
  for (i = 0; i < cnt; i++) {
    gasneti_free(ptrs[i]);
  }
  gasneti_free(ptrs);
  gasneti_memcheck_all();

  PTHREAD_BARRIER(num_threads);

  if (!id) {
    gasneti_getheapstats(&stats_after);
    #if GASNET_DEBUG
      if (stats_before.live_bytes != stats_after.live_bytes ||
          stats_before.live_objects != stats_after.live_objects) 
        MSG("WARNING: unexpected heap size change:\n"
        "  stats_before.live_bytes=%llu stats_after.live_bytes=%llu\n"
        "  stats_before.live_objects=%llu stats_after.live_objects=%llu",
        (unsigned long long)stats_before.live_bytes,   (unsigned long long)stats_after.live_bytes,
        (unsigned long long)stats_before.live_objects, (unsigned long long)stats_after.live_objects);
    #endif
  }
  PTHREAD_BARRIER(num_threads);
}
/* ------------------------------------------------------------------------------------ */
static void cond_test(int id) {
  static gasneti_cond_t cond1 = GASNETI_COND_INITIALIZER;
  static gasneti_cond_t cond2;
  static gasneti_mutex_t lock1 = GASNETI_MUTEX_INITIALIZER;
  static uint32_t done = 0;
  int i;

  PTHREAD_BARRIER(num_threads);

    if (!id) {
      gasneti_cond_init(&cond2);
      gasneti_cond_destroy(&cond2);
      gasneti_cond_init(&cond2);
      gasneti_mutex_lock(&lock1);
      gasneti_cond_signal(&cond1);
      gasneti_cond_signal(&cond2);
      gasneti_cond_broadcast(&cond1);
      gasneti_cond_broadcast(&cond2);
      gasneti_mutex_unlock(&lock1);
    }

  PTHREAD_BARRIER(num_threads);

    if (!id) { /* awake thread */
      for (i = 0; i < iters2; i++) {
        gasneti_mutex_lock(&lock1);
          if (i&1) {
            gasneti_cond_signal(&cond1);
          } else {
            gasneti_cond_broadcast(&cond1);
          }
        gasneti_mutex_unlock(&lock1);
        if (TEST_RAND_ONEIN(iters)) gasnett_sched_yield();
      }
      gasneti_mutex_lock(&lock1);
        done = 1;
        gasneti_cond_broadcast(&cond1);
      gasneti_mutex_unlock(&lock1);
    } else {
      gasneti_mutex_lock(&lock1);
      while (!done) {
        gasneti_mutex_assertlocked(&lock1);
        gasneti_cond_wait(&cond1, &lock1);
      }
      gasneti_mutex_unlock(&lock1);
      gasneti_mutex_assertunlocked(&lock1);
    }

  PTHREAD_BARRIER(num_threads);
}
/* ------------------------------------------------------------------------------------ */
static void mutex_test(int id) {
  static gasneti_mutex_t lock1 = GASNETI_MUTEX_INITIALIZER;
  static gasneti_mutex_t lock2;
  static unsigned int counter;
  int i;

  PTHREAD_BARRIER(num_threads);

    if (!id) {
      gasneti_mutex_assertunlocked(&lock1);
      gasneti_mutex_lock(&lock1);
      gasneti_mutex_assertlocked(&lock1);
      gasneti_mutex_unlock(&lock1);
      gasneti_mutex_assertunlocked(&lock1);

      assert_always(gasneti_mutex_trylock(&lock1) == GASNET_OK);
      gasneti_mutex_assertlocked(&lock1);
      gasneti_mutex_unlock(&lock1);

      gasneti_mutex_init(&lock2);
      gasneti_mutex_assertunlocked(&lock2);
      gasneti_mutex_lock(&lock2);
      gasneti_mutex_assertlocked(&lock2);
      gasneti_mutex_unlock(&lock2);
      gasneti_mutex_assertunlocked(&lock2);
      gasneti_mutex_destroy(&lock2);
      gasneti_mutex_init(&lock2);
      gasneti_mutex_assertunlocked(&lock2);

      counter = 0;
    }

  PTHREAD_BARRIER(num_threads);

    for (i=0;i<iters2;i++) {
      if (i&1) {
        gasneti_mutex_lock(&lock1);
      } else {
        int retval;
        while ((retval=gasneti_mutex_trylock(&lock1))) {
          assert_always(retval == EBUSY);
        }
      }
        counter++;
      gasneti_mutex_unlock(&lock1);
    }

  PTHREAD_BARRIER(num_threads);

    if (counter != (num_threads * iters2)) 
      ERR("failed mutex test: counter=%i expecting=%i", counter, (num_threads * iters2));

  PTHREAD_BARRIER(num_threads);
}
/* ------------------------------------------------------------------------------------ */
#if GASNETI_HAVE_SPINLOCK
static void spinlock_test(int id) {
  static gasneti_atomic_t lock1 = GASNETI_SPINLOCK_INITIALIZER;
  static gasneti_atomic_t lock2;
  static unsigned int counter;
  int i;

  MSG0("%s spinlock test...",(num_threads > 1?"parallel":"sequential"));
  PTHREAD_BARRIER(num_threads);

    if (!id) {
      gasneti_spinlock_lock(&lock1);
      gasneti_spinlock_unlock(&lock1);

      assert_always(gasneti_spinlock_trylock(&lock1) == GASNET_OK);
      gasneti_spinlock_unlock(&lock1);

      gasneti_spinlock_init(&lock2);
      gasneti_spinlock_lock(&lock2);
      gasneti_spinlock_unlock(&lock2);
      gasneti_spinlock_destroy(&lock2);
      gasneti_spinlock_init(&lock2);

      counter = 0;
    }

  PTHREAD_BARRIER(num_threads);

    for (i=0;i<iters2;i++) {
      if (i&1) {
        gasneti_spinlock_lock(&lock1);
      } else {
        int retval;
        while ((retval=gasneti_spinlock_trylock(&lock1))) {
          assert_always(retval == EBUSY);
        }
      }
        counter++;
      gasneti_spinlock_unlock(&lock1);
    }

  PTHREAD_BARRIER(num_threads);

    if (counter != (num_threads * iters2)) 
      ERR("failed spinlock test: counter=%i expecting=%i", counter, (num_threads * iters2));

  PTHREAD_BARRIER(num_threads);
}
#else
static void spinlock_test(int id) {
  MSG0("spinlocks not available - spinlock test skipped.");
}
#endif
/* ------------------------------------------------------------------------------------ */
static int pf_cnt_boolean, pf_cnt_counted;
static gasnet_hsl_t pf_lock = GASNET_HSL_INITIALIZER;
static void progressfn_reqh(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyMedium0(token, gasneti_diag_hidx_base + 1, buf, nbytes));
}
static void progressfn_reph(gasnet_token_t token, void *buf, size_t nbytes) {
}
static void progressfn_tester(int *counter) {
  static int active = 0; /* protocol provides mutual exclusion & recursion protection */
  int iamactive = 0;
  gasnet_hsl_lock(&pf_lock);
    (*counter)++;
    if (!active) { active = 1; iamactive = 1; }
  gasnet_hsl_unlock(&pf_lock);
  if (!iamactive) return;

  /* do some work that should be legal inside a progress fn */
  { static int tmp = 47;
    int sz;
    gasnet_put_nbi(peer, peerseg, &tmp, sizeof(tmp));
    gasnet_get_nbi(&tmp, peer, peerseg, sizeof(tmp));
    for (sz = 1; sz <= MIN(128*1024,TEST_SEGSZ); sz *= 2) {
      gasnet_put_nbi_bulk(peer, peerseg, myseg, sz);
      gasnet_get_nbi_bulk(myseg, peer, peerseg, sz);
    }
    gasnet_try_syncnbi_all();
    if (gasneti_diag_havehandlers) {
      for (sz = 1; sz <= MIN(gasnet_AMMaxMedium(),MIN(64*1024,TEST_SEGSZ)); sz *= 2) {
        gasnet_AMRequestMedium0(peer, gasneti_diag_hidx_base + 0, myseg, sz);
        gasnet_AMRequestLong0(peer, gasneti_diag_hidx_base + 0, myseg, sz, peerseg);
      }
    }
  }

  gasneti_local_mb();
  active = 0;
}
static void progressfn_bool() { progressfn_tester(&pf_cnt_boolean); }
static void progressfn_counted() { progressfn_tester(&pf_cnt_counted); }
static void progressfns_test(int id) {
#if !GASNET_DEBUG
  return;
#else
  { int i;
    int cnt_c = 0, cnt_b = 0;
    MSG0("%s progress functions test...",(num_threads>1?"parallel":"sequential"));

    PTHREAD_BARRIER(num_threads);
    gasneti_debug_progressfn_bool = progressfn_bool;
    gasneti_debug_progressfn_counted = progressfn_counted;
    pf_cnt_boolean = 0;
    pf_cnt_counted = 0;
    PTHREAD_BARRIER(num_threads);

    if (!id) GASNETI_PROGRESSFNS_ENABLE(debug_boolean,BOOLEAN);
    PTHREAD_BARRIER(num_threads);
    GASNETI_PROGRESSFNS_ENABLE(debug_counted,COUNTED);
    GASNETI_PROGRESSFNS_ENABLE(debug_counted,COUNTED);
    GASNETI_PROGRESSFNS_DISABLE(debug_counted,COUNTED);

    /* do some work that should cause progress fns to run */
    for (i=0; i < 10; i++) {
      int tmp;
      gasnet_put(peer, peerseg, &tmp, sizeof(tmp));
      gasnet_get(&tmp, peer, peerseg, sizeof(tmp));
      gasnet_put_bulk(peer, peerseg, myseg, 1024);
      gasnet_get_bulk(myseg, peer, peerseg, 1024);
      gasnet_AMPoll();
    }

    /* ensure they did run */
    cnt_c = pf_cnt_counted; cnt_b = pf_cnt_boolean;
    assert_always(cnt_c > 0); assert_always(cnt_b > 0);

    /* disable progress fns and quiesce the system */
    PTHREAD_BARRIER(num_threads);
    if (!id) GASNETI_PROGRESSFNS_DISABLE(debug_boolean,BOOLEAN);
    GASNETI_PROGRESSFNS_DISABLE(debug_counted,COUNTED);
    PTHREAD_BARRIER(num_threads);
    for (i=0; i < 1000; i++) { gasnet_AMPoll(); gasneti_sched_yield(); }
    PTHREAD_BARRIER(num_threads);
    cnt_c = pf_cnt_counted; cnt_b = pf_cnt_boolean;
    PTHREAD_BARRIER(num_threads);

    /* do some work that might cause progress fns to run */
    for (i=0; i < 10; i++) {
      int tmp;
      gasnet_put(peer, peerseg, &tmp, sizeof(tmp));
      gasnet_get(&tmp, peer, peerseg, sizeof(tmp));
      gasnet_put_bulk(peer, peerseg, myseg, 1024);
      gasnet_get_bulk(myseg, peer, peerseg, 1024);
      gasnet_AMPoll();
    }

    PTHREAD_BARRIER(num_threads);
    /* ensure they did not run */
    assert_always(cnt_c == pf_cnt_counted); assert_always(cnt_b == pf_cnt_boolean);
  }
#endif
}
/* ------------------------------------------------------------------------------------ */
#if GASNET_PAR

static void * thread_fn(void *arg) {
  int test_errs = 0;
  int id = (int)(uintptr_t)arg;

  PTHREAD_BARRIER(num_threads);

  MSG0("parallel gasneti_mutex_t test...");
  mutex_test(id);

  PTHREAD_BARRIER(num_threads);

  MSG0("parallel gasneti_cond_t test...");
  cond_test(id);

  PTHREAD_BARRIER(num_threads);

  spinlock_test(id);

  PTHREAD_BARRIER(num_threads);

  MSG0("parallel malloc test...");
  malloc_test(id);
  
  PTHREAD_BARRIER(num_threads);

  progressfns_test(id);

  PTHREAD_BARRIER(num_threads);
  MSG0("parallel conduit tests...");
  PTHREAD_BARRIER(num_threads);
  test_errs += GASNETC_RUN_DIAGNOSTICS_PAR(iters,id,num_threads);
  PTHREAD_BARRIER(num_threads);
  test_errs += GASNETE_RUN_DIAGNOSTICS_PAR(iters,id,num_threads);
  PTHREAD_BARRIER(num_threads);

  return (void *)(uintptr_t)test_errs;
}
#endif

static gasnet_handlerentry_t gasneti_diag_handlers[] = {
  #ifdef GASNETC_DIAG_HANDLERS
    GASNETC_DIAG_HANDLERS(), /* should start at gasnetc_diag_hidx_base */
  #endif
  #ifdef GASNETE_DIAG_HANDLERS
    GASNETE_DIAG_HANDLERS(), /* should start at gasnete_diag_hidx_base */
  #endif

  { gasneti_diag_hidx_base + 0, (gasneti_handler_fn_t)progressfn_reqh },
  { gasneti_diag_hidx_base + 1, (gasneti_handler_fn_t)progressfn_reph }
};


void gasneti_diagnostic_gethandlers(gasnet_handlerentry_t **htable, int *htable_cnt) {
  assert(htable && htable_cnt);
  *htable = gasneti_diag_handlers;
  *htable_cnt = (int)(sizeof(gasneti_diag_handlers)/sizeof(gasnet_handlerentry_t));
  gasneti_diag_havehandlers = 1;
}

