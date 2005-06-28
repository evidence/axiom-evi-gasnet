/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/amxtests/testlatency.c,v $
 *     $Date: 2005/06/28 08:40:54 $
 * $Revision: 1.8 $
 * Description: AMX test
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#include "apputils.h"

/* non-pipelined version of ping tester */

#define PING_REQ_HANDLER 1
#define PING_REP_HANDLER 2

static volatile int numleft;

int myproc;
int numprocs;
eb_t eb;
ep_t ep;

static void ping_request_handler(void *token) {
  numleft--;

  #if VERBOSE
    printf("%i: ping_request_handler(). sending reply...\n", myproc); fflush(stdout);
  #endif

  AM_Safe(AM_Reply0(token, PING_REP_HANDLER));
  }

static void ping_reply_handler(void *token) {

  #if VERBOSE
    printf("%i: ping_reply_handler()\n", myproc); fflush(stdout);
  #endif

  numleft--;
  }

void mywait(int polling) {
  if (polling) { /* poll until everyone done */
    while (numleft) {
      AM_Safe(AM_Poll(eb));
      }
    }
  else {
    while (numleft) {
      AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY)); 
      AM_Safe(AM_WaitSema(eb));
      AM_Safe(AM_Poll(eb));
      }
    }
}

/* usage: testlatency  numprocs  spawnfn  iters  P/B
 */
int main(int argc, char **argv) {
  uint64_t networkpid;
  int64_t begin, end, total;
  int polling = 1;
  int k;
  int iters = 0;

  CHECKARGS(argc, argv, 1, 2, "iters (Poll/Block)");

  AMX_VerboseErrors = 1;

  /* call startup */
  AM_Safe(AMX_SPMDStartup(&argc, &argv, 
                            0, &networkpid, &eb, &ep));

  /* setup handlers */
  AM_Safe(AM_SetHandler(ep, PING_REQ_HANDLER, ping_request_handler));
  AM_Safe(AM_SetHandler(ep, PING_REP_HANDLER, ping_reply_handler));

  setupUtilHandlers(ep, eb);

  /* get SPMD info */
  myproc = AMX_SPMDMyProc();
  numprocs = AMX_SPMDNumProcs();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 1;
  if (argc > 2) {
    switch(argv[2][0]) {
      case 'p': case 'P': polling = 1; break;
      case 'b': case 'B': polling = 0; break;
      default: printf("polling must be 'P' or 'B'..\n"); AMX_SPMDExit(1);
      }
    }

  outputTimerStats();

  AM_Safe(AMX_SPMDBarrier());

  if (myproc == 0) printf("Running %i iterations of latency test...\n", iters);

  begin = getCurrentTimeMicrosec();

  if (myproc == 0 && numprocs > 1) {
    numleft = (numprocs-1)*iters;
    mywait(polling);
  } else { /* everybody sends packets to 0 */
    int expect = (numprocs > 1 ? 1 : 2);
    for (k=0;k < iters; k++) {
      numleft = expect;
      #if VERBOSE
        printf("%i: sending request...", myproc); fflush(stdout);
      #endif
      AM_Safe(AM_Request0(ep, 0, PING_REQ_HANDLER));
      mywait(polling);
      }
    }
  
  end = getCurrentTimeMicrosec();

  total = end - begin;
  if (myproc != 0 || numprocs == 1) printf("Slave %i: %i microseconds total, throughput: %i requests/sec (%i us / request)\n", 
    myproc, (int)total, (int)(((float)1000000)*iters/((int)total)), ((int)total)/iters);
  else printf("Slave 0 done.\n");
  fflush(stdout);

  /* dump stats */
  AM_Safe(AMX_SPMDBarrier());
  printGlobalStats();
  AM_Safe(AMX_SPMDBarrier());

  /* exit */
  AM_Safe(AMX_SPMDExit(0));

  return 0;
  }
/* ------------------------------------------------------------------------------------ */
