/*  $Archive:: /Ti/GASNet/tests/testbarrier.c                             $
 *     $Date: 2004/03/08 19:27:05 $
 * $Revision: 1.9 $
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

  /* Calibrate a delay loop.  Given "iters" and "delay_us", we determine the argument
   * we need when calling test_delay() iters times, to get a _total_ delay no less than
   * delay_us.  The value of delay_us is overwritten with the achieved delay.
   * Take turns calibrating the delay to avoid spoiling timings on overcommitted CPUs */
  for (j=0; j < nodes; j++) {
    BARRIER();

    if (j == mynode) {
      delay_us = (total * 5) / 4;	/* delay about 120% of the barrier time */
      delay_loops = test_calibrate_delay(iters, &delay_us);
      *(int64_t *)TEST_MYSEG() = delay_us;
    } else {
      sleep(1 + (total + 499999)/500000); /* sleep while other node(s) spin */
    }
  }

  /* Take turns being late to notify
   * We insert a delay before the _notify() on one node.
   * This simulates a load imbalance between barriers.
   * The time reported is how much the barrier costs excluding the delay.
   * This reported time will often be less than the full barrier because
   * some progress was made by the other nodes.
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

    gasnet_get(&delay_us, j, TEST_SEG(j), sizeof(delay_us));
    delay_us = total - delay_us;

    avg_time += delay_us;
    min_time = MIN(min_time, delay_us);
    max_time = MAX(max_time, delay_us);
  }
  avg_time /= nodes;

  if (mynode == 0) {
    printf("Total difference: %8.3f sec  Late notify() Anon. Barrier net latency, minimum: %8.3f us\n", ((float)min_time)/1000000, ((float)min_time)/iters);
    printf("Total difference: %8.3f sec  Late notify() Anon. Barrier net latency, maximum: %8.3f us\n", ((float)max_time)/1000000, ((float)max_time)/iters);
    printf("Total difference: %8.3f sec  Late notify() Anon. Barrier net latency, average: %8.3f us\n", ((float)avg_time)/1000000, ((float)avg_time)/iters);
    fflush(stdout);
  }

  /* Take turns being late to wait
   * We insert a delay between the _notify() and _wait() on one node.
   * This simulates a load imbalance between barrier notify and wait.
   * The time reported is how much the barrier costs excluding the delay.
   * This reported time will often be less than the full barrier because
   * some progress was made by the other nodes.
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

    gasnet_get(&delay_us, j, TEST_SEG(j), sizeof(delay_us));
    delay_us = total - delay_us;

    avg_time += delay_us;
    min_time = MIN(min_time, delay_us);
    max_time = MAX(max_time, delay_us);
  }
  avg_time /= nodes;

  if (mynode == 0) {
    printf("Total difference: %8.3f sec  Late wait() Anon. Barrier net latency, minimum: %8.3f us\n", ((float)min_time)/1000000, ((float)min_time)/iters);
    printf("Total difference: %8.3f sec  Late wait() Anon. Barrier net latency, maximum: %8.3f us\n", ((float)max_time)/1000000, ((float)max_time)/iters);
    printf("Total difference: %8.3f sec  Late wait() Anon. Barrier net latency, average: %8.3f us\n", ((float)avg_time)/1000000, ((float)avg_time)/iters);
    fflush(stdout);
  }

  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
