/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testtools.c,v $
 *     $Date: 2005/02/24 19:01:20 $
 * $Revision: 1.20 $
 * Description: helpers for GASNet tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_tools.h>

/* specifically omit gasnet.h/test.h to test independence */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>

#ifdef HAVE_PTHREAD_H
  #include <pthread.h>
  #define NUM_THREADS 10
  gasnett_atomic_t thread_flag[NUM_THREADS];
  int valX[NUM_THREADS];
  int valY[NUM_THREADS];
#endif

int iters = 0;

int errs = 0;
#define ERR(msg) do {                                        \
  printf("ERROR: %s (at %s:%i)\n", msg, __FILE__, __LINE__); \
  fflush(stdout);                                            \
  errs++;                                                    \
  } while (0)

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
#define TIME() mygetMicrosecondTimeStamp()

void * thread_fn(void *arg);

int main(int argc, char **argv) {

  if (argc > 1) iters = atoi(argv[1]);
  if (iters < 1) iters = 100;

  printf("Running testtools with %i iterations\n", iters);

  #if defined(GASNETT_PAGESIZE) && defined(GASNETT_PAGESHIFT)
    if (0x1 << GASNETT_PAGESHIFT != GASNETT_PAGESIZE)
      printf("ERROR: bad pagesizes: GASNETT_PAGESHIFT=%i GASNETT_PAGESIZE=%i\n",
              GASNETT_PAGESHIFT, GASNETT_PAGESIZE);
    else 
      printf("System page size is 2^%i == %i\n", GASNETT_PAGESHIFT, GASNETT_PAGESIZE);
  #endif
   
  printf("Testing high-performance timers...\n");
  { /* high performance timers */
    int i;
    gasnett_tick_t start, end;
    int64_t startref, endref;
    gasnett_tick_t ticktimemin = GASNETT_TICK_MIN;
    gasnett_tick_t ticktimemax = GASNETT_TICK_MAX;

    if (!(ticktimemin < ticktimemax)) ERR("!(min < max)");
    if (!(gasnett_ticks_now() > ticktimemin)) ERR("!(now > min)");
    if (!(gasnett_ticks_now() < ticktimemax)) ERR("!(now < max)");

    for (i=0; i < 3; i++) {
      int time, timeref;
      start = gasnett_ticks_now();
      startref = TIME();
        sleep(1);
      end = gasnett_ticks_now();
      endref = TIME();

      time = gasnett_ticks_to_us(end) - gasnett_ticks_to_us(start);
      timeref = endref - startref;

      if (abs(timeref - time) > 100000)
        printf("ERROR: timer and reference differ by more than 0.1sec:\n"
               "\ttime=%i  timeref=%i\n",time,timeref);

      if (abs( (gasnett_ticks_to_us(end) - gasnett_ticks_to_us(start)) - 
               gasnett_ticks_to_us(end - start) ) > 1)
        ERR("ticks_to_us(A) - ticks_to_us(B) != ticks_to_us(A-B)");

    }
    {
      double overhead = gasnett_timer_overheadus();
      double granularity = gasnett_timer_granularityus();
      if (granularity <= 0.0 || overhead <= 0.0 ||
          (granularity+0.1) < 0.5*overhead) 
          /* allow some leeway for noise at granularities approaching cycle speed */
          /*granularity < 0.5*overhead)*/
        printf("ERROR: nonsensical timer overhead/granularity measurements:\n"
               "  overhead: %.3fus  granularity: %.3fus\n",overhead, granularity);
    }
  }

  printf("Testing local membar...\n");
  { /* local membar */
    int i;
    for (i=0;i<iters;i++) {
      gasnett_local_mb();
    }
  }

  printf("Testing local write membar...\n");
  { /* local membar */
    int i;
    for (i=0;i<iters;i++) {
      gasnett_local_wmb();
    }
  }

  printf("Testing local read membar...\n");
  { /* local membar */
    int i;
    for (i=0;i<iters;i++) {
      gasnett_local_rmb();
    }
  }

  printf("Testing atomic ops (sequential)...\n");
  { /* we can't really test atomicity without spinning threads, 
       but we can at least test simple operations  */
    int i = 0;
    gasnett_atomic_t var = gasnett_atomic_init(iters);

    if (gasnett_atomic_read(&var) != iters)
      ERR("gasnett_atomic_init/gasnett_atomic_read got wrong value");

    gasnett_atomic_set(&var, 2*iters);
    if (gasnett_atomic_read(&var) != 2*iters)
      ERR("gasnett_atomic_set/gasnett_atomic_read got wrong value");

    for (i=1;i<=iters;i++) {
      gasnett_atomic_increment(&var);
      if (gasnett_atomic_read(&var) != 2*iters + i)
        ERR("gasnett_atomic_increment got wrong value");
    }

    for (i=iters-1;i>=0;i--) {
      gasnett_atomic_decrement(&var);
      if (gasnett_atomic_read(&var) != 2*iters + i)
        ERR("gasnett_atomic_decrement got wrong value");
    }

    for (i=1;i<=iters;i++) {
      gasnett_atomic_set(&var, i);
      gasnett_atomic_increment(&var);
      if (gasnett_atomic_read(&var) != i+1)
        ERR("gasnett_atomic_set/gasnett_atomic_increment got wrong value");
    }

    for (i=1;i<=iters;i++) {
      gasnett_atomic_set(&var, i);
      gasnett_atomic_decrement(&var);
      if (gasnett_atomic_read(&var) != i-1)
        ERR("gasnett_atomic_set/gasnett_atomic_decrement got wrong value");
    }

    gasnett_atomic_set(&var,iters);
    for (i=iters-1;i>=1;i--) {
      if (gasnett_atomic_decrement_and_test(&var))
        ERR("gasnett_atomic_decrement_and_test got wrong value");
      if (gasnett_atomic_read(&var) != i)
        ERR("gasnett_atomic_decrement_and_test set wrong value");
    }
    if (!gasnett_atomic_decrement_and_test(&var))
      ERR("gasnett_atomic_decrement_and_test got wrong value at zero");
    if (gasnett_atomic_read(&var) != 0)
      ERR("gasnett_atomic_decrement_and_test set wrong value at zero");
  }

#ifdef HAVE_PTHREAD_H
  printf("Testing atomic ops (parallel)...\n");
  { 
    int i;
    pthread_t threadid[NUM_THREADS];

    for(i=0;i<NUM_THREADS;i++) gasnett_atomic_set(thread_flag+i,1);
    gasnett_local_mb();
    #ifdef HAVE_PTHREAD_SETCONCURRENCY
        pthread_setconcurrency(NUM_THREADS);
    #endif

    for(i=0;i<NUM_THREADS;i++) {
      pthread_attr_t attr;   
      pthread_attr_init(&attr);   
      pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); 
      if (pthread_create(&threadid[i], &attr, &thread_fn, (void *)(uintptr_t)i)) 
        perror("pthread_create");
    }

    for(i=0;i<NUM_THREADS;i++) {
      if (pthread_join(threadid[i], NULL))
        perror("pthread_join");
    }
  }
#endif

  printf("Done.\n");
  return (errs > 0 ? 1 : 0);
}

#ifdef HAVE_PTHREAD_H

gasnett_atomic_t up = gasnett_atomic_init(0);
gasnett_atomic_t down = gasnett_atomic_init(2*NUM_THREADS);
gasnett_atomic_t x1 = gasnett_atomic_init(10000);
gasnett_atomic_t x2 = gasnett_atomic_init(10000);
gasnett_atomic_t x3 = gasnett_atomic_init(10000);
gasnett_atomic_t x4 = gasnett_atomic_init(10000);
gasnett_atomic_t x5 = gasnett_atomic_init(10000);

void * thread_fn(void *arg) {
  int id = (int)(uintptr_t)arg;
  int i;
  int iters2=100*iters;
 
  /* sanity check */
  if (!gasnett_atomic_decrement_and_test(thread_flag+id)) {
      printf("ERROR: thread %i failed sanity check\n", id);
  }

  if (id == 0) printf("parallel atomic-op barrier test...\n");

  for (i=0;i<iters;i++) {
    /* simple count-up barrier */
    gasnett_atomic_increment(&up);
    while (gasnett_atomic_read(&up) < NUM_THREADS) gasnett_sched_yield(); 

    gasnett_atomic_set(&down, 2*NUM_THREADS);

    gasnett_atomic_increment(&up);
    while (gasnett_atomic_read(&up) < 2*NUM_THREADS) gasnett_sched_yield(); 

    if (gasnett_atomic_read(&up) != 2*NUM_THREADS)
      ERR("count-up post-barrier read");

    /* simple count-down barrier */
    gasnett_atomic_decrement(&down);
    while (gasnett_atomic_read(&down) > NUM_THREADS) gasnett_sched_yield(); 

    gasnett_atomic_set(&up, 0);

    gasnett_atomic_decrement(&down);
    while (gasnett_atomic_read(&down) > 0) gasnett_sched_yield(); 

    if (gasnett_atomic_read(&down) != 0)
      ERR("count-down post-barrier read");
  }
  
  if (id == 0) printf("parallel atomic-op pounding test...\n");
  gasnett_atomic_set(&x1, 5);
  gasnett_atomic_set(&x2, 5+iters2*NUM_THREADS);

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < NUM_THREADS) gasnett_sched_yield(); 

  for (i=0;i<iters2;i++) {
    gasnett_atomic_increment(&x1);
    gasnett_atomic_decrement(&x2);
  }

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 2*NUM_THREADS) gasnett_sched_yield(); 

  if (gasnett_atomic_read(&x1) != 5+iters2*NUM_THREADS)
    ERR("pounding inc test mismatch");

  if (gasnett_atomic_read(&x2) != 5)
    ERR("pounding dec test mismatch");

  if (id == 0) printf("parallel dec-test pounding test...\n");

  gasnett_atomic_set(&x3, NUM_THREADS);
  gasnett_atomic_set(&x4, 0);
  gasnett_atomic_set(&x5, 0); /* count of "wins" */

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 3*NUM_THREADS) gasnett_sched_yield(); 

  for (i=0;i<iters;i++) {
    if (gasnett_atomic_decrement_and_test(&x3)) { /* I won */
      gasnett_atomic_increment(&x5); /* tally win */
      if (gasnett_atomic_read(&x3) != 0) ERR("pounding dec-test mismatch x3");
      if (gasnett_atomic_read(&x4) != 0) ERR("pounding dec-test mismatch x4");
      gasnett_atomic_set(&x4, NUM_THREADS); /* go */
    } else {
      while (gasnett_atomic_read(&x4) == 0) gasnett_sched_yield(); /* I lost - wait */
    }

    if (gasnett_atomic_decrement_and_test(&x4)) { /* I won */
      gasnett_atomic_increment(&x5); /* tally win */
      if (gasnett_atomic_read(&x3) != 0) ERR(" pounding dec-test mismatch x3");
      if (gasnett_atomic_read(&x4) != 0) ERR("pounding dec-test mismatch x4");
      gasnett_atomic_set(&x3, NUM_THREADS); /* go */
    } else {
      while (gasnett_atomic_read(&x3) == 0) gasnett_sched_yield(); /* I lost - wait */
    }
  }

  if (gasnett_atomic_read(&x5) != 2*iters)
    ERR("pounding dec-test mismatch");

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 4*NUM_THREADS) gasnett_sched_yield(); 

  if (id == 0) printf("parallel word-tearing test...\n");

  gasnett_atomic_set(&x3, 0);
  gasnett_atomic_set(&x4, 0);
  gasnett_atomic_set(&x5, 0); 

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 5*NUM_THREADS) gasnett_sched_yield(); 

  if (NUM_THREADS <= 100) {  /* need 2*NUM_THREADS + 1 < 255 to prevent byte overflow */
    uint32_t x = id + 1;
    uint32_t myval = (x << 24) | (x << 16) | (x << 8) | x;
    for (i=0;i<iters2;i++) {
      uint32_t v;
      gasnett_atomic_set(&x3, myval);
      gasnett_atomic_set(&x4, myval);
      gasnett_atomic_set(&x5, myval);
      gasnett_atomic_increment(&x4);
      gasnett_atomic_decrement(&x5);
      v = gasnett_atomic_read(&x3);
      if (((v >> 24) & 0xFF) != (v & 0xFF) ||
          ((v >> 16) & 0xFF) != (v & 0xFF) ||
          ((v >>  8) & 0xFF) != (v & 0xFF)) 
          ERR("observed word tearing on gasnett_atomic_set");
      v = gasnett_atomic_read(&x4); 
      /* bottom byte may have increased by up to NUM_THREADS, but high bytes must be same */
      if (((v >> 24) & 0xFF) != ((v >>  8) & 0xFF) ||
          ((v >> 16) & 0xFF) != ((v >>  8) & 0xFF)) 
          ERR("observed word tearing on gasnett_atomic_set/gasnett_atomic_increment");
      v = gasnett_atomic_read(&x5); 
      v += NUM_THREADS;
      /* bottom byte may have decreased by by  to NUM_THREADS, but high bytes must be same */
      if (((v >> 24) & 0xFF) != ((v >>  8) & 0xFF) ||
          ((v >> 16) & 0xFF) != ((v >>  8) & 0xFF)) 
          ERR("observed word tearing on gasnett_atomic_set/gasnett_atomic_decrement");
    }
  }

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 6*NUM_THREADS) gasnett_sched_yield(); 

  if (id == 0) printf("parallel membar test...\n");
  valX[id] = 0;
  valY[id] = 0;

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 7*NUM_THREADS) gasnett_sched_yield(); 

  { int partner = (id + 1) % NUM_THREADS;
    int lx, ly;
    for (i=0;i<iters2;i++) {
      valX[id] = i;
      gasnett_local_wmb();
      valY[id] = i;

      ly = valY[partner];
      gasnett_local_rmb();
      lx = valX[partner];
      if (lx < ly) ERR("mismatch in gasnett_local_wmb/gasnett_local_rmb test");
    }
  }

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 8*NUM_THREADS) gasnett_sched_yield(); 

  return NULL;
}

/* Mimic Berkeley UPC build config strings, to allow running GASNet tests using upcrun */
#define GASNET_CONFIG_STRING \
  "RELEASE=x,SPEC=x,CONDUIT=SMP-x/REFERENCE-x,THREADMODEL=PAR,SEGMENT=FAST,PTR=x,align,nodebug,notrace,nostats"
GASNETT_IDENT(GASNetT_IdentString_link_GASNetConfig, 
 "$GASNetConfig: (<link>) " GASNET_CONFIG_STRING " $");
GASNETT_IDENT(GASNetT_IdentString_link_UPCRConfig,
 "$UPCRConfig: (<link>) " GASNET_CONFIG_STRING ",SHMEM=pthreads,dynamicthreads $");
GASNETT_IDENT(GASNetT_IdentString_link_upcver, 
 "$UPCVersion: (<link>) *** GASNet test *** $");
GASNETT_IDENT(GASNetT_IdentString_link_compileline, 
 "$UPCCompileLine: (<link>) *** GASNet test *** $");
GASNETT_IDENT(GASNetT_IdentString_link_compiletime, 
 "$UPCCompileTime: (<link>) " __DATE__ " " __TIME__ " $");
GASNETT_IDENT(GASNetT_IdentString_HeapSz, 
 "$UPCRDefaultHeapSizes: UPC_SHARED_HEAP_OFFSET=0 UPC_SHARED_HEAP_SIZE=0 $");
GASNETT_IDENT(GASNetT_IdentString_PthCnt, "$UPCRDefaultPthreadCount: 1 $");
#ifdef GASNETI_PTR32
  GASNETT_IDENT(GASNetT_IdentString_PtrSz, "$UPCRSizeof: void_ptr=( $");
#else
  GASNETT_IDENT(GASNetT_IdentString_PtrSz, "$UPCRSizeof: void_ptr=, $");
#endif
/* Ditto for Titanium tcrun */
GASNETT_IDENT(GASNetT_TiBackend_IdentString,
 "$TitaniumBackend: sequential $");
GASNETT_IDENT(GASNetT_TiCompiler_IdentString,          
 "$TitaniumCompilerFlags: *** GASNet test *** -g $");

#endif
