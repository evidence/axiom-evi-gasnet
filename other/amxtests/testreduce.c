#include <stdio.h>
#include <stdlib.h>

#include <ammpi.h>
#include <ammpi_spmd.h>

#include "apputils.h"

#define REDUCE_HANDLER  1

int total = 0;
int numcalls = 0;
void reduce_request_handler(void *token, int val) {
  printf("reduce_request_handler got: %i\n", val);
  total += val;
  numcalls++;
  }

int main(int argc, char **argv) {
  eb_t eb;
  ep_t ep;
  uint64_t networkpid;
  int myproc;
  int numprocs;

  AMMPI_VerboseErrors = 1;

  if (argc > 1) {
    printf("Usage: %s\n", argv[0]);
    exit(1);
    }

  /* call startup */
  AM_Safe(AMMPI_SPMDStartup(&argc, &argv, 
                            0, &networkpid, &eb, &ep));

  /* setup handlers */
  AM_Safe(AM_SetHandler(ep, REDUCE_HANDLER, reduce_request_handler));
  setupUtilHandlers(ep, eb);
  
  /* barrier */
  AM_Safe(AMMPI_SPMDBarrier());

  /* get SPMD info */
  myproc = AMMPI_SPMDMyProc();
  numprocs = AMMPI_SPMDNumProcs();

  /* compute */
  AM_Safe(AM_Request1(ep, 0, REDUCE_HANDLER, myproc));

  if (myproc == 0) {
    while (numcalls < numprocs) {
      #if 0 /* poll-sleep */
        AM_Safe(AM_Poll(eb));
        sleep(1);
      #else /* poll-block */
        AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY));
        AM_Safe(AM_WaitSema(eb));
        AM_Safe(AM_Poll(eb));
      #endif
      printf(".");
      }
    printf("Reduction result: %i\n",total);
    { /* verify result */
      int i, correcttotal=0;
      for (i = 0; i < numprocs; i++) correcttotal += i;
      if (total == correcttotal) printf("Result verified!\n");
      else printf("ERROR!!! Result incorrect! total=%i  correcttotal=%i\n", total, correcttotal);
      }
    }


  /* barrier */
  AM_Safe(AMMPI_SPMDBarrier());

  printGlobalStats();

  AM_Safe(AMMPI_SPMDBarrier());

  /* exit */
  AM_Safe(AMMPI_SPMDExit(0));

  return 0;
  }
/* ------------------------------------------------------------------------------------ */
