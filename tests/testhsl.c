/*  $Archive:: /Ti/GASNet/tests/testhsl.c                                 $
 *     $Date: 2002/12/19 18:31:54 $
 * $Revision: 1.3 $
 * Description: GASNet barrier performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

gasnet_hsl_t globallock = GASNET_HSL_INITIALIZER;
void badhandler1(gasnet_token_t token) {
  gasnet_hsl_lock(&globallock);
}
void badhandler2(gasnet_token_t token) {
  gasnet_hold_interrupts();
}
void badhandler3(gasnet_token_t token) {
  gasnet_hsl_lock(&globallock);
  gasnet_AMReplyShort0(token, 255);
}

void donothing(gasnet_token_t token) {
}

int main(int argc, char **argv) {
  int mynode, partner;
  gasnet_handlerentry_t htable[] = { 
    { 201, badhandler1 },
    { 202, badhandler2 },
    { 203, badhandler3 },

    { 255, donothing }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t), 
                            TEST_SEGSZ, TEST_MINHEAPOFFSET));

  MSG("running...");

  mynode = gasnet_mynode();
  partner = (gasnet_mynode() + 1) % gasnet_nodes();

  if (argc < 2) {
    printf("Usage: %s (errtestnum:1..15)\n", argv[0]);fflush(stdout);
    gasnet_exit(1);
  }
  {
    int errtest = atoi(argv[1]);
    gasnet_hsl_t lock1 = GASNET_HSL_INITIALIZER;
    gasnet_hsl_t lock2;
    gasnet_hsl_init(&lock2);

    switch(errtest) {
      case 1:
        gasnet_hold_interrupts();
        gasnet_hold_interrupts();
      break;
      case 2:
        gasnet_resume_interrupts();
      break;
      case 3:
        gasnet_hsl_lock(&lock1);
        gasnet_resume_interrupts();
      break;
      case 4:
        gasnet_hsl_init(&lock1);
      break;
      case 5:
        gasnet_hsl_unlock(&lock1);
      break;
      case 6:
        gasnet_hsl_lock(&lock1);
        gasnet_hsl_lock(&lock2);
        gasnet_hsl_unlock(&lock1);
      break;
      case 7:
        gasnet_hsl_lock(&lock1);
        gasnet_hsl_lock(&lock1);
      break;
      case 8:
        gasnet_hsl_lock(&lock1);
        gasnet_hold_interrupts();
      break;
      case 9:
        gasnet_hsl_lock(&lock1);
        gasnet_AMPoll();
      break;
      case 10:
        gasnet_hold_interrupts();
        gasnet_AMPoll();
      break;
      case 11:
        gasnet_AMRequestShort0(gasnet_mynode(), 201);
        GASNET_BLOCKUNTIL(0);
      break;
      case 12:
        gasnet_AMRequestShort0(gasnet_mynode(), 202);
        GASNET_BLOCKUNTIL(0);
      break;
      case 13:
        gasnet_AMRequestShort0(gasnet_mynode(), 203);
        GASNET_BLOCKUNTIL(0);
      break;
      case 14:
        gasnet_hsl_lock(&lock1);
        sleep(2);
        gasnet_hsl_unlock(&lock1);
        goto done;
      break;
      case 15:
        gasnet_hold_interrupts();
        sleep(2);
        gasnet_resume_interrupts();
        goto done;
      break;
      default:
        MSG("bad err test num.");
        abort();
    }
  }

  MSG("FAILED: err test failed.");

done:
  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
