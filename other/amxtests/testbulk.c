#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ammpi.h>
#include <ammpi_spmd.h>

#include "apputils.h"



#define VERBOSE 0

#define BULK_REQ_HANDLER 1
#define BULK_REP_HANDLER 2

int myproc;
int numprocs;

int size = -1;
int msg_size = 0;

int nummsgs = 0;

int done = 0;
uint32_t *VMseg;

static void bulk_request_handler(void *token, void *buf, int nbytes, int arg) {
  uint32_t *recvdbuf = (uint32_t *)buf;
  #if VERBOSE
    printf("%i: bulk_request_handler(). starting...", myproc); fflush(stdout);
  #endif

  assert(arg == 666);
  assert(nbytes == size % AM_MaxLong() || nbytes == AM_MaxLong());
  assert(buf == ((uint8_t *)VMseg) + 100);
  /* assert(done < 2*nummsgs); */

  #ifdef AMMPI_DEBUG
    /*  verify the result */
    { int i;
      for (i = 0; i < nbytes/4; i++) {
        if (recvdbuf[i] != (uint32_t)i) {
          printf("%i: ERROR: mismatched data recvdbuf[%i]=%i\n", myproc, i, recvdbuf[i]);
          fflush(stdout);
          abort();
          }
        }
      }
  #endif

  #if VERBOSE
    printf("%i: bulk_request_handler(). sending reply...", myproc); fflush(stdout);
  #endif


  AM_Safe(AM_Reply0(token, BULK_REP_HANDLER));
  done++;
  }

static void bulk_reply_handler(void *token, int ctr, int dest, int val) {
  /* assert(done < 2*nummsgs); */

  #if VERBOSE
    printf("%i: bulk_reply_handler()\n", myproc); fflush(stdout);
  #endif
  done++;
  }

int main(int argc, char **argv) {
  eb_t eb;
  ep_t ep;
  uint64_t networkpid;
  int64_t begin, end, total;
  int polling = 1;
  int fullduplex = 0;
  int depth = 0;
  int rightguy;
  uint32_t *srcmem;
  int iters = 0;

  AMMPI_VerboseErrors = 1;

  if (argc < 2) {
    printf("Usage: %s iters (bulkmsgsize) (Poll/Block) (netdepth) (Half/Full)\n", argv[0]);
    exit(1);
    }

  if (argc > 4) depth = atoi(argv[4]);
  if (!depth) depth = 4;

  /* call startup */
  AM_Safe(AMMPI_SPMDStartup(&argc, &argv, 
                            depth, &networkpid, &eb, &ep));

  /* setup handlers */
  AM_Safe(AM_SetHandler(ep, BULK_REQ_HANDLER, bulk_request_handler));
  AM_Safe(AM_SetHandler(ep, BULK_REP_HANDLER, bulk_reply_handler));

  setupUtilHandlers(ep, eb);

  /* get SPMD info */
  myproc = AMMPI_SPMDMyProc();
  numprocs = AMMPI_SPMDNumProcs();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 1;
  if (argc > 2) size = atoi(argv[2]);
  if (size == -1) size = 512;
  if (argc > 3) {
    switch(argv[3][0]) {
      case 'p': case 'P': polling = 1; break;
      case 'b': case 'B': polling = 0; break;
      default: printf("polling must be 'P' or 'B'..\n"); AMMPI_SPMDExit(1);
      }
    }
  if (argc > 5) {
    switch(argv[5][0]) {
      case 'h': case 'H': fullduplex = 0; break;
      case 'f': case 'F': fullduplex = 1; break;
      default: printf("duplex must be H or F..\n"); AMMPI_SPMDExit(1);
      }
    }
  if (!fullduplex && numprocs % 2 != 0) {
     printf("half duplex requires an even number of processors\n"); AMMPI_SPMDExit(1);
    }
  msg_size = (size > AM_MaxLong() ? AM_MaxLong() : size);
  nummsgs = (size % AM_MaxLong() == 0 ? size / AM_MaxLong() : (size / AM_MaxLong())+1);
  srcmem = (uint32_t *)malloc(msg_size);
  memset(srcmem, 0, msg_size);
  VMseg = (uint32_t *)malloc(msg_size+100);
  memset(VMseg, 0, msg_size+100);
  AM_Safe(AM_SetSeg(ep, VMseg, msg_size+100));

  rightguy = (myproc + 1) % numprocs;

  { /*  init my source mem */
    int i;
    int numints = msg_size/4;
    for (i=0; i < numints; i++) srcmem[i] = i;
    }

  AM_Safe(AMMPI_SPMDBarrier());


  if (myproc == 0) printf("Running %s bulk test sz=%i...\n", (fullduplex?"full-duplex":"half-duplex"), size);

  begin = getCurrentTimeMicrosec();

  if (fullduplex || myproc % 2 == 1) {
    int q;
    for (q=0; q<iters; q++) {
      int j;
      msg_size = AM_MaxLong();
      for (j = 0; j < nummsgs; j++) {
	      if (j == nummsgs-1 && size % AM_MaxLong() != 0) msg_size = size % AM_MaxLong(); /*  last one */
        #if VERBOSE_PING
	        printf("%i: sending request...", myproc); fflush(stdout);
        #endif
	      AM_Safe(AM_RequestXfer1(ep, rightguy, 100, BULK_REQ_HANDLER, srcmem, msg_size, 666));
        }
      }
    }

  if (polling) { /* poll until everyone done */
    int expectedmsgs = fullduplex ? 2*nummsgs*iters:nummsgs*iters;
    while (done<expectedmsgs) {
      AM_Safe(AM_Poll(eb));
      }
    }
  else {
    int expectedmsgs = fullduplex ? 2*nummsgs*iters:nummsgs*iters;
    while (done<expectedmsgs) {
      AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY)); 
      AM_Safe(AM_WaitSema(eb));
      AM_Safe(AM_Poll(eb));
      }
    }

  end = getCurrentTimeMicrosec();

  total = end - begin;
  if (fullduplex || myproc % 2 == 1) 
    printf("Slave %i: %i microseconds total, throughput: %8.3f KB/sec\n", 
      myproc, (int)total, (float)(((float)1000000)*size*iters/((int)total))/1024.0);
  fflush(stdout);

  /* dump stats */
  AM_Safe(AMMPI_SPMDBarrier());
  printGlobalStats();
  AM_Safe(AMMPI_SPMDBarrier());

  /* exit */
  AM_Safe(AMMPI_SPMDExit(0));

  return 0;
  }
/* ------------------------------------------------------------------------------------ */
