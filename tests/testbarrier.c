/*  $Archive:: /Ti/GASNet/tests/testbarrier.c                             $
 *     $Date: 2002/07/04 03:01:49 $
 * $Revision: 1.2 $
 * Description: GASNet barrier performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>

#include <test.h>

int main(int argc, char **argv) {
  int mynode, iters=0;
  int64_t start,total;
  int i = 0;

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

  MSG("running...");

  mynode = gasnet_mynode();
  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 10000;

  if (mynode == 0) {
      printf("Running barrier test with %i iterations...\n",iters);
      fflush(stdout);
  }
  BARRIER();

  start = TIME();
  for (i=0; i < iters; i++) {
    gasnete_barrier_notify(i, 0);            
    GASNET_Safe(gasnete_barrier_wait(i, 0)); 
  }
  total = TIME() - start;

  BARRIER();

  if (mynode == 0) {
      printf("Total time: %8.3f sec  Avg Barrier latency: %8.3f us\n",
        ((float)total)/1000000, ((float)total)/iters);
      fflush(stdout);
  }

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
