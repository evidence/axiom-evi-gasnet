/*  $Archive:: /Ti/GASNet/tests/testbarrier.c                             $
 *     $Date: 2004/03/05 23:48:02 $
 * $Revision: 1.8 $
 * Description: GASNet barrier performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#define TEST_DELAY 1
#include <test.h>

int main(int argc, char **argv) {
  int mynode, nodes, iters=0;
  int64_t start,total,delay_us;
  int64_t min_time, max_time, avg_time;
  int delay_loops;
  int result;
  int j, i = 0;

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

  MSG("running...");

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 10000;

  if (mynode == 0) {
      printf("Running barrier test with %i iterations...\n",iters);
      fflush(stdout);
  }
  BARRIER();

  start = TIME();
  for (i=0; i < iters; i++) {
    gasnet_barrier_notify(i, 0);            
    GASNET_Safe(gasnet_barrier_wait(i, 0)); 
  }
  total = TIME() - start;

  BARRIER();

  if (mynode == 0) {
      printf("Total time: %8.3f sec  Avg Named Barrier latency: %8.3f us\n",
        ((float)total)/1000000, ((float)total)/iters);
      fflush(stdout);
  }
  BARRIER();

  start = TIME();
  for (i=0; i < iters; i++) {
    gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);            
    GASNET_Safe(gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS)); 
  }
  total = TIME() - start;

  BARRIER();

  if (mynode == 0) {
      printf("Total time: %8.3f sec  Avg Anon. Barrier latency: %8.3f us\n",
        ((float)total)/1000000, ((float)total)/iters);
      fflush(stdout);
  }

  delay_us = (total * 5) / 4;
  delay_loops = test_calibrate_delay(iters, &delay_us);
  *(int *)TEST_MYSEG() = (int)delay_us;

  /* Take turns being late to notify
   * We insert a delay before the _notify() on one node.
   * This simulates a load imbalance between barriers.
   * The time reported is how much the barrier costs excluding the delay.
   */
  avg_time = 0;
  max_time = 0;
  min_time = (int64_t)1 << 62;	/* good enough */
  for (j=0; j < nodes; j++) {
    BARRIER();

    start = TIME();
    for (i=0; i < iters; i++) {
      if (j == mynode) {
        test_delay(delay_loops);
      }
      gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
      GASNET_Safe(gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS)); 
    }
    total = TIME() - start;

    delay_us = total - (int)gasnet_get_val(j, TEST_SEG(j), sizeof(int));

    avg_time += delay_us;
    min_time = MIN(min_time, delay_us);
    max_time = MAX(max_time, delay_us);
  }
  avg_time /= nodes;

  if (mynode == 0) {
    printf("Late notify() Anon. Barrier latency, minimum: %8.3f us\n", ((float)min_time)/iters);
    printf("Late notify() Anon. Barrier latency, maximum: %8.3f us\n", ((float)max_time)/iters);
    printf("Late notify() Anon. Barrier latency, average: %8.3f us\n", ((float)avg_time)/iters);
    fflush(stdout);
  }

  /* Take turns being late to wait
   * We insert a delay between the _notify() and _wait() on one node.
   * This simulates a load imbalance between barrier notify and wait.
   * The time reported is how much the barrier costs excluding the delay.
   */
  avg_time = 0;
  max_time = 0;
  min_time = (int64_t)1 << 62;	/* good enough */
  for (j=0; j < nodes; j++) {
    BARRIER();

    start = TIME();
    for (i=0; i < iters; i++) {
      gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
      if (j == mynode) {
        test_delay(delay_loops);
      }
      GASNET_Safe(gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS)); 
    }
    total = TIME() - start;

    delay_us = total - (int)gasnet_get_val(j, TEST_SEG(j), sizeof(int));

    avg_time += delay_us;
    min_time = MIN(min_time, delay_us);
    max_time = MAX(max_time, delay_us);
  }
  avg_time /= nodes;

  if (mynode == 0) {
    printf("Late wait() Anon. Barrier latency, minimum: %8.3f us\n", ((float)min_time)/iters);
    printf("Late wait() Anon. Barrier latency, maximum: %8.3f us\n", ((float)max_time)/iters);
    printf("Late wait() Anon. Barrier latency, average: %8.3f us\n", ((float)avg_time)/iters);
    fflush(stdout);
  }

  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
