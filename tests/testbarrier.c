/*  $Archive:: /Ti/GASNet/tests/testbarrier.c                             $
 *     $Date: 2004/03/03 17:58:41 $
 * $Revision: 1.6 $
 * Description: GASNet barrier performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

int main(int argc, char **argv) {
  int mynode, iters=0;
  int64_t start,total;
  int result;
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

  BARRIER();

  /* Test for required failures: */

  /* node 0 indicates mismatch on entry: */
  gasnet_barrier_notify(0, !mynode ? GASNET_BARRIERFLAG_MISMATCH : 0);
  result = gasnet_barrier_wait(0, !mynode ? GASNET_BARRIERFLAG_MISMATCH : 0);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect barrier mismatch indicated on notify.");
    gasnet_exit(1);
  }

  /* ids differ between notify and wait */
  gasnet_barrier_notify(0, 0);
  result = gasnet_barrier_wait(1, 0);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect mismatch between id at notify and wait.");
    gasnet_exit(1);
  }

  /* Flags differ between notify and wait: */
  gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
  result = gasnet_barrier_wait(0, 0);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect anonymous notify with named wait.");
    gasnet_exit(1);
  }
  gasnet_barrier_notify(0, 0);
  result = gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect named notify with anonymous wait.");
    gasnet_exit(1);
  }

  /* Mismatched id: */
  if (gasnet_nodes() > 1) {
    gasnet_barrier_notify(!mynode, 0);
    result = gasnet_barrier_wait(0, 0);
    if (result != GASNET_ERR_BARRIER_MISMATCH) {
      MSG("Failed to detect different id on node 0.");
      gasnet_exit(1);
    }
  }

  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
