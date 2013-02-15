/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testinternal.c,v $
 *     $Date: 2006/02/10 23:34:36 $
 * $Revision: 1.1 $
 * Description: GASNet internal diagnostic tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_tools.h>

#include <test.h>

/* ------------------------------------------------------------------------------------ */
int main(int argc, char **argv) {
  int iters = 0, threads=0;

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(NULL, 0, 
                            TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  #if GASNET_PAR
    test_init("testinternal",0,"(iters) (threadcnt)");
  #else
    test_init("testinternal",0,"(iters)");
  #endif
  TEST_PRINT_CONDUITINFO();

  if (argc > 1) iters = atoi(argv[1]);
  if (iters < 1) iters = 1000;
  if (argc > 2) threads = atoi(argv[2]);
  if (threads < 1) threads = 4;

  #if GASNET_PAR
    MSG0("Running GASNet internal diagnostics with iters=%i and threads=%i", iters, threads);
  #else
    MSG0("Running GASNet internal diagnostics with iters=%i", iters);
  #endif

  BARRIER();
  test_errs = gasnett_run_diagnostics(iters, threads);
  BARRIER();

  if (test_errs) ERR("gasnett_run_diagnostics(%i) failed.", iters);

  gasnet_exit(test_errs);
  return 0;
}
