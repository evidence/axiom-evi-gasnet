#include "portable_inttypes.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef GASNET_NDEBUG
  #define NDEBUG 1
#endif

#include <assert.h>

#ifdef BLOCKING_WAIT
  #define mpi_testwait_some MPI_Waitsome
  #define mpi_testwait_desc "blocking wait (MPI_Waitsome)"
#else
  #define mpi_testwait_some MPI_Testsome
  #define mpi_testwait_desc "nonblocking poll (MPI_Testsome)"
#endif

#define   SEND_BUFFER_SZ   1048576

#ifdef DEBUG
  #define DEBUGMSG(s) do { \
    printf("P%i: %s\n", rank, s); fflush(stdout); \
  } while(0)
#else
  #define DEBUGMSG(s)
#endif

#ifdef UNIQUE_BUFFERS
  #define BUFFER_CALC(base, offset)  ((base) + (offset))
#else
  #define BUFFER_CALC(base, offset)  (base)
#endif

#define MPI_SAFE(fncall) do {     \
   int retcode = (fncall);        \
   if (retcode != MPI_SUCCESS) {  \
     fprintf(stderr, "MPI Error: %s\n  returned error code %i\n", #fncall, retcode); \
     abort();                     \
   }                              \
 } while (0)

int64_t getMicrosecondTimeStamp() {
  int64_t retval;
  struct timeval tv;
  if (gettimeofday(&tv, NULL)) {
    perror("gettimeofday");
    abort();
  }
  retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
  return retval;
}

int rank = -1;
int nproc = 0;
int peerid = -1;
int queuedepth = -1;
int mympitag;
int peermpitag;

void startupMPI(int* argc, char ***argv) {

  MPI_SAFE(MPI_Init(argc, argv));
  MPI_SAFE(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
  MPI_SAFE(MPI_Comm_size(MPI_COMM_WORLD, &nproc));

  { /* attach a send buffer (only used for buffered sends) */
    char *buffer = (char *)malloc(SEND_BUFFER_SZ);
    MPI_SAFE(MPI_Buffer_attach(buffer, SEND_BUFFER_SZ));
  }

  printf("P%i/%i starting...\n", rank, nproc); fflush(stdout);

  /* pair up the processors (if nproc is 1, do a loopback test) */
  if (nproc == 1) peerid = 0;
  else {
    if (nproc % 2 != 0) { printf("ERROR: nproc must be 1 or even\n"); exit(1);}
    else peerid = ( rank % 2 == 0 ? rank + 1 : rank - 1);
  }

  { /* compute tags -
     * base MPI tag on pid to prevent receiving cross-talk messages 
     * sent to dead processes (yes, this actually happens with some less robust MPI implementations)
     */
    int pid = getpid();
    int *allmpitags = (int *)malloc(nproc * sizeof(int));
    mympitag = abs(pid) % (MPI_TAG_UB+1);
    if (mympitag == MPI_ANY_TAG) mympitag = (mympitag + 1) % (MPI_TAG_UB+1);

    /* exchange tags */
    MPI_SAFE(MPI_Allgather(&mympitag, 1, MPI_INT, 
      allmpitags, 1, MPI_INT, MPI_COMM_WORLD));
    assert(allmpitags[rank] == mympitag);
    peermpitag = allmpitags[peerid]; /* save peer's tag */
    free(allmpitags);
    DEBUGMSG("tag computation complete");
  } 
}

void shutdownMPI() {
  char *buffer= NULL;
  int sz = 0;
  DEBUGMSG("shutting down");
  MPI_SAFE(MPI_Buffer_detach(&buffer, &sz));
  free(buffer);
  MPI_SAFE(MPI_Finalize());

  printf("P%i exiting...\n", rank); fflush(stdout);
}

void barrier() {
  DEBUGMSG("entering barrier");
  MPI_SAFE(MPI_Barrier(MPI_COMM_WORLD));
  DEBUGMSG("leaving barrier");
}

/*------------------------------------------------------------------*/
#define KB 1024
#define MB (KB*KB)
#define FIRSTSZ() (1)
#define NEXTSZ(x) (x*2)
#define DONESZ(x) (x > 4*KB*KB)

#define CHECKTAG(x)     do { if (x != mympitag) {                         \
        printf("ERROR: Recieved a stray message with incorrect tag!!!\n");\
        fflush(stdout);                                                   \
        exit(1);                                                          \
      }} while(0)

#ifdef SIMULATE_READWRITE
  /* READMSG and WRITEMSG simulate the cache behavior of a real MPI application
   * some MPI implementations may have a non-zero cost for transferring messages
   * back and forth between NIC hardware and the host CPU cache - 
   * make sure we include this cost because it impacts the performance of any real application 
   * (assuming it doesn't send nonsense or ignore the contents of the messages it receives)
   */
  char _read_tmp;
  #define READMSG(buf, sz) do {     \
    char *p = buf;                  \
    char *pend = ((char*)buf) + sz; \
    while (p != pend) {             \
      _read_tmp += *(p++);          \
    } } while(0)

  #define WRITEMSG(buf, sz) memset(buf, 0, sz);
#else
  #define READMSG(buf, sz) 
  #define WRITEMSG(buf, sz) 
#endif

/*------------------------------------------------------------------*/
/* run a barrier performance test of iters iterations, 
 * returns the total number of microseconds consumed during the test
 */
double barriertest(int iters) {
  int i;
  int64_t starttime, endtime;

  barrier();

  starttime = getMicrosecondTimeStamp();

  for (i=0; i < iters; i++) {
    MPI_Barrier(MPI_COMM_WORLD);
  }

  endtime = getMicrosecondTimeStamp();

  return (double)(endtime - starttime);
}

/*------------------------------------------------------------------*/
/* run a pairwise pingpong test of iters iterations, where each iteration consists 
 * of a message of size msgsz bytes and an acknowledgement of size 1 byte
 * uses nonblocking recvs and blocking sends
 *  (these could be changed to synchronous, buffered or ready-mode sends, 
 *   or even to some form of non-blocking send)
 * returns the total number of microseconds consumed during the test
 */
double pingpongtest(int iters, int msgsz) {
  int i;
  int64_t starttime, endtime;
  int iamsender = (rank % 2 == 0);
  int iamreceiver = !iamsender || peerid == rank; /* handle loopback */
  char *sendMsgbuffer = (char*)malloc(msgsz);
  char *sendAckbuffer = (char*)malloc(1);
  char *recvMsgbuffer = (char*)malloc(msgsz);
  char *recvAckbuffer = (char*)malloc(1);
  MPI_Request recvMsgHandle = MPI_REQUEST_NULL;
  MPI_Request recvAckHandle = MPI_REQUEST_NULL;
  MPI_Status status;

  if (iamreceiver) {
    /* prepost a recv */
    MPI_SAFE(MPI_Irecv(recvMsgbuffer, msgsz, MPI_BYTE, 
              peerid, MPI_ANY_TAG, MPI_COMM_WORLD, 
              &recvMsgHandle));
  }

  barrier();

  starttime = getMicrosecondTimeStamp();

  for (i=0; i < iters; i++) {

    if (iamsender) {
      /* prepost a recv for acknowledgement */
      MPI_SAFE(MPI_Irecv(recvAckbuffer, 1, MPI_BYTE, 
                peerid, MPI_ANY_TAG, MPI_COMM_WORLD, 
                &recvAckHandle));

      /* send message */
      WRITEMSG(sendMsgbuffer, msgsz);
      MPI_SAFE(MPI_Send(sendMsgbuffer, msgsz, MPI_BYTE, peerid, peermpitag, MPI_COMM_WORLD));
    }

    if (iamreceiver) {
      /* wait for message */
      MPI_SAFE(MPI_Wait(&recvMsgHandle, &status));
      CHECKTAG(status.MPI_TAG);

      READMSG(recvMsgbuffer, msgsz);

      /* pre-post recv for next message */
      MPI_SAFE(MPI_Irecv(recvMsgbuffer, msgsz, MPI_BYTE, 
                peerid, MPI_ANY_TAG, MPI_COMM_WORLD, 
                &recvMsgHandle));

      /* send acknowledgement */
      WRITEMSG(sendAckbuffer, 1);
      MPI_SAFE(MPI_Send(sendAckbuffer, 1, MPI_BYTE, peerid, peermpitag, MPI_COMM_WORLD));
    }

    if (iamsender) {
      /* wait for acknowledgement */
      MPI_SAFE(MPI_Wait(&recvAckHandle, &status));
      CHECKTAG(status.MPI_TAG);
      READMSG(recvAckbuffer, 1);
    }

  }

  endtime = getMicrosecondTimeStamp();

  if (iamreceiver) { /* last recv must be cancelled (not included in timing) */
    #if 0
      MPI_SAFE(MPI_Cancel(&recvMsgHandle));
    #else
      /* apparently some MPI impls don't implement cancel at all.. (grr..) */
      /* use loopback instead to get the same effect */
      MPI_SAFE(MPI_Send(sendMsgbuffer, msgsz, MPI_BYTE, rank, mympitag, MPI_COMM_WORLD));
    #endif

    MPI_SAFE(MPI_Wait(&recvMsgHandle, &status));
  }

  free(sendMsgbuffer);
  free(sendAckbuffer);
  free(recvMsgbuffer);
  free(recvAckbuffer);

  return (double)(endtime - starttime);
}
/*------------------------------------------------------------------*/
/* run a pairwise flood test sending iters messages 
 * of a message of size msgsz bytes and no acknowledgements -
 * messages are shoveled into a send queue of size queuesz,
 * as quickly as MPI will take them
 * uses nonblocking recvs and nonblocking sends
 * returns the total number of microseconds consumed during the test
 */
double floodtest(int iters, int msgsz) {
  int numsent = 0, numrecvd = 0, numrecvposted = 0;
  int64_t starttime, endtime;
  int iamsender = (rank % 2 == 0);
  int iamreceiver = !iamsender || peerid == rank; /* handle loopback */
  MPI_Request *recvHandle = NULL;
  MPI_Request *sendHandle = NULL;
  char *sendbuffer = NULL;
  char *recvbuffer = NULL;
  int *indextmp = malloc(sizeof(int)*queuedepth);
  MPI_Status *statustmp = malloc(sizeof(MPI_Status)*queuedepth);

  if (iamsender) {
    int i;
    sendbuffer = (char*)malloc(msgsz*queuedepth);
    sendHandle = (MPI_Request*)malloc(sizeof(MPI_Request)*queuedepth);
    assert(sendbuffer && sendHandle);
    for (i=0; i < queuedepth; i++) {
      sendHandle[i] = MPI_REQUEST_NULL;
    }
  }
  if (iamreceiver) {
    recvbuffer = (char*)malloc(msgsz*queuedepth);
    recvHandle = (MPI_Request*)malloc(sizeof(MPI_Request)*queuedepth);
    assert(recvbuffer && recvHandle);
    while(numrecvposted < queuedepth && numrecvposted < iters) {
      recvHandle[numrecvposted] = MPI_REQUEST_NULL;
      /* prepost recvs */
      MPI_SAFE(MPI_Irecv(BUFFER_CALC(recvbuffer,msgsz*numrecvposted), msgsz, MPI_BYTE, 
                peerid, MPI_ANY_TAG, MPI_COMM_WORLD, 
                &recvHandle[numrecvposted]));
      assert(recvHandle[numrecvposted] != MPI_REQUEST_NULL);
      numrecvposted++;
    }
  }

  barrier();

  starttime = getMicrosecondTimeStamp();

  if (iamsender) { /* fill the outgoing pipe */
    while (numsent < iters && numsent < queuedepth) {
      char *buf = BUFFER_CALC(sendbuffer,msgsz*numsent);
      WRITEMSG(buf, msgsz);
      MPI_SAFE(MPI_Isend(buf, msgsz, MPI_BYTE, peerid, peermpitag, MPI_COMM_WORLD, &sendHandle[numsent]));
      assert(sendHandle[numsent] != MPI_REQUEST_NULL);
      numsent++;
    }
  }

  while ( (iamsender && numsent < iters) || 
          (iamreceiver && numrecvd < iters)) {

    if (iamreceiver) {
      int numcomplete = 0;
      /* reap any completions and do more recvs */
      MPI_SAFE(mpi_testwait_some(queuedepth, recvHandle, &numcomplete, indextmp, statustmp));
      while (numcomplete != MPI_UNDEFINED && numcomplete > 0) {
        int idx = indextmp[--numcomplete];
        char *buf = BUFFER_CALC(recvbuffer,msgsz*idx);
        CHECKTAG(statustmp[numcomplete].MPI_TAG);
        READMSG(buf, msgsz);
        numrecvd++;
        assert(recvHandle[idx] == MPI_REQUEST_NULL);
        if (numrecvposted < iters) { /* not done yet - recv another */
          MPI_SAFE(MPI_Irecv(buf, msgsz, MPI_BYTE, 
                    peerid, MPI_ANY_TAG, MPI_COMM_WORLD, 
                    &recvHandle[idx]));
          assert(recvHandle[idx] != MPI_REQUEST_NULL);
          numrecvposted++;
        }
      }
    }

    if (iamsender) {
      int numcomplete = 0;
      /* reap any completions and do more sends */
      MPI_SAFE(mpi_testwait_some(queuedepth, sendHandle, &numcomplete, indextmp, statustmp));
      while (numcomplete != MPI_UNDEFINED && numcomplete > 0) {
        int idx = indextmp[--numcomplete];
        char *buf = BUFFER_CALC(sendbuffer,msgsz*idx);
        assert(sendHandle[idx] == MPI_REQUEST_NULL);
        if (numsent < iters) { /* not done yet - send another */
          WRITEMSG(buf, msgsz);
          MPI_SAFE(MPI_Isend(buf, msgsz, MPI_BYTE, peerid, peermpitag, MPI_COMM_WORLD, &sendHandle[idx]));
          assert(sendHandle[idx] != MPI_REQUEST_NULL);
          numsent++;
        }
      }
    }

  }

  if (iamsender) { /* pause for all sends to complete locally */
    MPI_SAFE(MPI_Waitall(queuedepth, sendHandle, statustmp));
  }

  endtime = getMicrosecondTimeStamp();

  if (recvHandle) free(recvHandle);
  if (sendHandle) free(sendHandle);
  if (sendbuffer) free(sendbuffer);
  if (recvbuffer) free(recvbuffer);
  free(indextmp);
  free(statustmp);

  return (double)(endtime - starttime);
}
/*------------------------------------------------------------------*/
/* run an exchange test with msgsz bytes per proc with bytes transferred
 * actually nproc*msgsz per exchange (all-to-all).
 */
double exchangetest(int iters, int msgsz) {
  int64_t starttime, endtime;
  int i;
  char *sendbuf, *recvbuf;

  sendbuf = malloc(msgsz*nproc);
  recvbuf = malloc(msgsz*nproc);

  if (sendbuf == NULL || recvbuf == NULL) {
    fprintf(stderr, "malloc");
    exit(-1);
  }

  barrier();

  starttime = getMicrosecondTimeStamp();
  for (i=0; i<iters; i++) {
    MPI_Alltoall(sendbuf, msgsz, MPI_CHAR, 
		 recvbuf, msgsz, MPI_CHAR, MPI_COMM_WORLD);
  }
  endtime = getMicrosecondTimeStamp();

  free(sendbuf);
  free(recvbuf);

  return (endtime-starttime);
}

/*------------------------------------------------------------------*/
void Usage(char *argvzero) {
  fprintf(stderr, "Usage:\n"
    "  %s B/P/F/E (numiterationsPerSize) (queuedepth)\n"
    "  B = Barrier latency test\n"
    "  P = Ping/pong latency test (no communication overlap)\n"
    "  F = Flood bandwidth test (overlap messages)\n"
    "  E = Exchange test (All-to-All)\n",
    argvzero);
  exit(1);
}
int main(int argc, char **argv) {
  int dopingpongtest = 0;
  int dofloodtest = 0;
  int dobarriertest = 0;
  int doexchangetest = 0;
  int iters = -1;

  /* init */
  startupMPI(&argc, &argv);

  /* parse arguments */
  if (argc < 2) Usage(argv[0]);
  { char *p;
    for (p = argv[1]; *p; p++) {
      switch(*p) {
        case 'E': case 'e': doexchangetest = 1; break;
        case 'P': case 'p': dopingpongtest = 1; break;
        case 'F': case 'f': dofloodtest = 1; break;
        case 'B': case 'b': dobarriertest = 1; break;
        default: Usage(argv[0]);
      }
    }
  }
  if (argc > 2) iters = atoi(argv[2]);
  else iters = 10000;
  if (iters <= 0) Usage(argv[0]);

  if (argc > 3) queuedepth = atoi(argv[3]);
  else queuedepth = 64;
  if (queuedepth <= 0) Usage(argv[0]);


  if (dobarriertest) { /* barrier test */
    double totaltime;
    if (rank == 0) {
      printf("=====> testmpiperf-barrier nprocs=%d config=MPI\n", nproc);
      printf("running %i iterations of barrier test...\n", iters);
      fflush(stdout);
    }
    
    barriertest(1); /* "warm-up" run */

    totaltime = barriertest(iters);
    if (rank == 0) {
      printf("barrier latency= %9.3f us, totaltime= %9.3f sec\n",
        totaltime/iters, totaltime/1000000);
      fflush(stdout);
    }
    barrier();
  }
  barrier();
  if (dopingpongtest) { /* ping-pong test */
    if (rank == 0) {
      printf("=====> testmpiperf-pingpong nprocs=%d config=MPI\n", nproc);
      printf("running %i iterations of ping-pong test per size...\n", iters);
      fflush(stdout);
    }
    barrier();

    { int msgsz;
      for (msgsz = FIRSTSZ(); !DONESZ(msgsz); msgsz = NEXTSZ(msgsz)) {
        double totaltime;

        pingpongtest(1, msgsz); /* "warm-up" run */
        barrier();
        totaltime = pingpongtest(iters, msgsz);
        barrier();

        if (rank % 2 == 0) {
          printf("P%i-P%i: size=%8i bytes, latency= %9.3f us, bandwidth= %11.3f KB/sec\n",
            rank, peerid, msgsz, 
            totaltime/iters, 
            (((double)msgsz)*iters/KB)/(totaltime/1000000));
          fflush(stdout);
        }
      }
    }
  }
  barrier();
  if (dofloodtest) { /* flood test */
    if (rank == 0) {
      printf("=====> testmpiperf-flood nprocs=%d config=MPI\n", nproc);
      printf("running %i iterations of flood test per size, with queuedepth=%i...\n", 
        iters, queuedepth);
      printf("Flood test using %s method\n", mpi_testwait_desc);
      fflush(stdout);
    }
    barrier();


    { int msgsz;
      for (msgsz = FIRSTSZ(); !DONESZ(msgsz); msgsz = NEXTSZ(msgsz)) {
        double totaltime;

        floodtest(queuedepth, msgsz); /* "warm-up" run */
        barrier();
        totaltime = floodtest(iters, msgsz);
        barrier();

        if ((rank % 2 == 1) || peerid == rank) { /* report reciever times on flood test */
          printf("P%i-P%i: size=%8i bytes, inv. throughput= %9.3f us, bandwidth= %11.3f KB/sec\n",
            rank, peerid, msgsz, 
            totaltime/iters, 
            (((double)msgsz)*iters/KB)/(totaltime/1000000));
          fflush(stdout);
        }
      }
    }
  }
  barrier();
  if (doexchangetest) { /* Exchange (all-to-all) test */
    if (rank == 0) {
      printf("=====> testmpiperf-exchange nprocs=%d config=MPI\n", nproc);
      printf("running %i iterations of exchange test per size\n", iters);
      fflush(stdout);
    }
    barrier();

    { int msgsz;
      for (msgsz = FIRSTSZ(); !DONESZ(msgsz/nproc); msgsz = NEXTSZ(msgsz)) {
        double totaltime;

	exchangetest(1, msgsz); /* "warm-up" run */
        barrier();
        totaltime = exchangetest(iters, msgsz);
        barrier();

        printf("P%i-P%i: size=%8i bytes, inv. throughput= %9.3f us, bandwidth= %11.3f KB/sec\n",
            rank, peerid, msgsz, 
            totaltime/iters, 
            (((double)msgsz*nproc)*iters/KB)/(totaltime/1000000));
          fflush(stdout);
      }
    }
  }

  barrier();


  shutdownMPI();
  return 0;
}
