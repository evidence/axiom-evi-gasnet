/*  $Archive:: /Ti/GASNet/tests/testgasnet.c                              $
 *     $Date: 2002/07/04 03:01:49 $
 * $Revision: 1.3 $
 * Description: General GASNet correctness tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>

#include <test.h>

int main(int argc, char **argv) {
  int *partnerseg = NULL;
  int mynode, partner;
  

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

  MSG("running...");

  { int i;
    printf("my args: argc=%i argv=[", argc);
    for (i=0; i < argc; i++) {
      printf("%s'%s'",(i>0?" ":""),argv[i]);
    }
    printf("]\n"); fflush(stdout);
  }

  mynode = gasnet_mynode();
  partner = (gasnet_mynode() + 1) % gasnet_nodes();
  { gasnet_seginfo_t si[GASNET_MAXNODES];
    gasnet_getSegmentInfo((gasnet_seginfo_t*)&si,GASNET_MAXNODES);
    assert(si[mynode].size == TEST_SEGSZ && si[partner].size == TEST_SEGSZ);
    partnerseg = si[partner].addr;
  }

  /*  blocking test */
  { int val1=0, val2=0;
    val1 = mynode + 100;

    gasnet_put(partner, partnerseg, &val1, sizeof(int));
    gasnet_get(&val2, partner, partnerseg, sizeof(int));

    if (val2 == (mynode + 100)) MSG("*** passed blocking test!!");
    else MSG("*** ERROR - FAILED BLOCKING TEST!!!!!");
  }

  BARRIER();
  /*  blocking list test */
  #define iters 100
  { GASNET_BEGIN_FUNCTION();
    gasnet_handle_t handles[iters];
    int val1;
    int vals[iters];
    int success = 1;
    int i;
    for (i = 0; i < iters; i++) {
      val1 = 100 + i + mynode;
      handles[i] = gasnet_put_nb(partner, partnerseg+i, &val1, sizeof(int));
    }
    gasnet_wait_syncnb_all(handles, iters); 
    for (i = 0; i < iters; i++) {
      handles[i] = gasnet_get_nb(&vals[i], partner, partnerseg+i, sizeof(int));
    }
    gasnet_wait_syncnb_all(handles, iters); 
    for (i=0; i < iters; i++) {
      if (vals[i] != 100 + mynode + i) {
        MSG("*** ERROR - FAILED NBI TEST!!!");
        success = 0;
      }
    }
    if (success) MSG("*** passed blocking list test!!");
  }

  BARRIER();

  { /*  implicit test */
    GASNET_BEGIN_FUNCTION();
    int vals[100];
    int i, success=1;
    for (i=0; i < 100; i++) {
      int tmp = mynode + i;
      gasnet_put_nbi(partner, partnerseg+i, &tmp, sizeof(int));
    }
    gasnet_wait_syncnbi_puts();
    for (i=0; i < 100; i++) {
      gasnet_get_nbi(&vals[i], partner, partnerseg+i, sizeof(int));
    }
    gasnet_wait_syncnbi_gets();
    for (i=0; i < 100; i++) {
      if (vals[i] != mynode + i) {
        MSG("*** ERROR - FAILED NBI TEST!!!");
        success = 0;
      }
    }
    if (success) MSG("*** passed nbi test!!");
  }

  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
