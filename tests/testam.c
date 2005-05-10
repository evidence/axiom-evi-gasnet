/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testam.c,v $
 *     $Date: 2005/05/10 17:33:09 $
 * $Revision: 1.18 $
 * Description: GASNet Active Messages performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

int mynode = 0;
void *myseg = NULL;
int sender;
int peer;
void *peerseg = NULL;

void report(const char *desc, int64_t totaltime, int iters) {
  if (mynode == 0) {
      printf("%-50s: %8.3f sec  %8.3f us\n",
        desc, ((float)totaltime)/1000000, ((float)totaltime)/iters);
      fflush(stdout);
  }
}

gasnet_hsl_t inchsl = GASNET_HSL_INITIALIZER;
#define INC(var) do {           \
    gasnet_hsl_lock(&inchsl);   \
    var++;                      \
    gasnet_hsl_unlock(&inchsl); \
  } while (0)

/* ------------------------------------------------------------------------------------ */
#define hidx_ping_shorthandler   201
#define hidx_pong_shorthandler   202

#define hidx_ping_medhandler     203
#define hidx_pong_medhandler     204

#define hidx_ping_longhandler    205
#define hidx_pong_longhandler    206

#define hidx_ping_shorthandler_flood   207
#define hidx_pong_shorthandler_flood   208

#define hidx_ping_medhandler_flood     209
#define hidx_pong_medhandler_flood     210

#define hidx_ping_longhandler_flood    211
#define hidx_pong_longhandler_flood    212

volatile int flag = 0;

void ping_shorthandler(gasnet_token_t token) {
  GASNET_Safe(gasnet_AMReplyShort0(token, hidx_pong_shorthandler));
}
void pong_shorthandler(gasnet_token_t token) {
  flag++;
}


void ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyMedium0(token, hidx_pong_medhandler, buf, nbytes));
}
void pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  flag++;
}


void ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyLong0(token, hidx_pong_longhandler, buf, nbytes, peerseg));
}

void pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  flag++;
}
/* ------------------------------------------------------------------------------------ */
void ping_shorthandler_flood(gasnet_token_t token) {
  GASNET_Safe(gasnet_AMReplyShort0(token, hidx_pong_shorthandler_flood));
}
void pong_shorthandler_flood(gasnet_token_t token) {
  INC(flag);
}


void ping_medhandler_flood(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyMedium0(token, hidx_pong_medhandler_flood, buf, nbytes));
}
void pong_medhandler_flood(gasnet_token_t token, void *buf, size_t nbytes) {
  INC(flag);
}


void ping_longhandler_flood(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyLong0(token, hidx_pong_longhandler_flood, buf, nbytes, peerseg));
}

void pong_longhandler_flood(gasnet_token_t token, void *buf, size_t nbytes) {
  INC(flag);
}

/* ------------------------------------------------------------------------------------ */
int main(int argc, char **argv) {
  int iters=0;
  int i = 0;
  int maxsz=64*1024;
  int maxmed, maxlong;
  gasnet_handlerentry_t htable[] = { 
    { hidx_ping_shorthandler,  ping_shorthandler  },
    { hidx_pong_shorthandler,  pong_shorthandler  },
    { hidx_ping_medhandler,    ping_medhandler    },
    { hidx_pong_medhandler,    pong_medhandler    },
    { hidx_ping_longhandler,   ping_longhandler   },
    { hidx_pong_longhandler,   pong_longhandler   },

    { hidx_ping_shorthandler_flood,  ping_shorthandler_flood  },
    { hidx_pong_shorthandler_flood,  pong_shorthandler_flood  },
    { hidx_ping_medhandler_flood,    ping_medhandler_flood    },
    { hidx_pong_medhandler_flood,    pong_medhandler_flood    },
    { hidx_ping_longhandler_flood,   ping_longhandler_flood   },
    { hidx_pong_longhandler_flood,   pong_longhandler_flood   }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t),
                            TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  mynode = gasnet_mynode();
  if (!mynode)
	print_testname("testam", gasnet_nodes());
  TEST_DEBUGPERFORMANCE_WARNING();
  TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */

  MSG("running...");

  myseg = TEST_MYSEG();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 1000;

  maxmed = MIN(maxsz, gasnet_AMMaxMedium());
  maxlong = MIN(maxsz, MIN(gasnet_AMMaxLongRequest(),gasnet_AMMaxLongReply()));
  peer = mynode ^ 1;
  if (peer == gasnet_nodes()) {
    /* w/ odd # of nodes, last one talks to self */
    peer = mynode;
  }
  sender = mynode % 2 == 0;

  peerseg = TEST_SEG(peer);

  if (mynode == 0) {
      printf("Running AM performance test with %i iterations...\n",iters);
      printf("%-50s    Total time    Avg. time\n"
             "%-50s    ----------    ---------\n", "", "");
      fflush(stdout);
  }

  /* ------------------------------------------------------------------------------------ */
  { GASNET_BEGIN_FUNCTION();

    if (sender) { /* warm-up */
      flag = 0;                                                                                  
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_ping_shorthandler_flood));
      }
      GASNET_BLOCKUNTIL(flag == iters);
      GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_ping_shorthandler));
      GASNET_BLOCKUNTIL(flag == iters+1);
    }
    BARRIER();
    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = -1;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_ping_shorthandler));
        GASNET_BLOCKUNTIL(flag == i);
      }
      report("AMShort ping-pong, roundtrip latency",TIME() - start, iters);
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();
    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = 0;
      BARRIER();
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_pong_shorthandler_flood));
      }
      if (peer == mynode) GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
      report("AMShort one-way flood, inv. throughput",TIME() - start, iters);
    } else {
      flag = 0;
      BARRIER();
      GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();
    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = 0;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_ping_shorthandler_flood));
      }
      GASNET_BLOCKUNTIL(flag == iters);
      report("AMShort roundtrip flood, inv. throughput",TIME() - start, iters);
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();
    /* ------------------------------------------------------------------------------------ */
#define TESTAM_PERF(DESC_STR, AMREQUEST, PING_HIDX, PONG_HIDX, MAXSZ, DEST) do { \
    if (sender) { /* warm-up */                                                  \
      flag = 0;                                                                  \
      for (i=0; i < iters; i++) {                                                \
        GASNET_Safe(AMREQUEST(peer, PING_HIDX##_flood, myseg, MAXSZ DEST));      \
      }                                                                          \
      GASNET_BLOCKUNTIL(flag == iters);                                          \
      GASNET_Safe(AMREQUEST(peer, PING_HIDX, myseg, MAXSZ DEST));                \
      GASNET_BLOCKUNTIL(flag == iters+1);                                        \
    }                                                                            \
    BARRIER();                                                                   \
    /* ---------------------------------------------------------- */             \
    { int sz;                                                                    \
      char msg[255];                                                             \
      for (sz = 0; sz <= MAXSZ; sz = (sz?sz*2:1)) {                              \
        sprintf(msg, DESC_STR"(sz=%5i) ping-pong, roundtrip latency", sz);       \
        BARRIER();                                                               \
        if (sender) {                                                            \
          int64_t start = TIME();                                                \
          flag = -1;                                                             \
          for (i=0; i < iters; i++) {                                            \
            GASNET_Safe(AMREQUEST(peer, PING_HIDX, myseg, sz DEST));             \
            GASNET_BLOCKUNTIL(flag == i);                                        \
          }                                                                      \
          report(msg,TIME() - start, iters);                                     \
        }                                                                        \
        BARRIER();                                                               \
      }                                                                          \
    }                                                                            \
    if (mynode == 0) { printf("\n"); fflush(stdout); }                           \
    BARRIER();                                                                   \
    /* ---------------------------------------------------------- */             \
    { int sz;                                                                    \
      char msg[255];                                                             \
      for (sz = 0; sz <= MAXSZ; sz = (sz?sz*2:1)) {                              \
        flag = 0;                                                                \
        sprintf(msg, DESC_STR"(sz=%5i) one-way flood, inv. throughput", sz);     \
        BARRIER();                                                               \
        if (sender) {                                                            \
          int64_t start = TIME();                                                \
          for (i=0; i < iters; i++) {                                            \
            GASNET_Safe(AMREQUEST(peer, PONG_HIDX##_flood, myseg, sz DEST));     \
          }                                                                      \
          if (peer == mynode) GASNET_BLOCKUNTIL(flag == iters);                  \
          BARRIER();                                                             \
          report(msg,TIME() - start, iters);                                     \
        } else {                                                                 \
          GASNET_BLOCKUNTIL(flag == iters);                                      \
          BARRIER();                                                             \
        }                                                                        \
        BARRIER();                                                               \
      }                                                                          \
    }                                                                            \
    if (mynode == 0) { printf("\n"); fflush(stdout); }                           \
    BARRIER();                                                                   \
    /* ---------------------------------------------------------- */             \
    { int sz;                                                                    \
      char msg[255];                                                             \
      for (sz = 0; sz <= MAXSZ; sz = (sz?sz*2:1)) {                              \
        sprintf(msg, DESC_STR"(sz=%5i) roundtrip flood, inv. throughput", sz);   \
        BARRIER();                                                               \
        if (sender) {                                                            \
          int64_t start = TIME();                                                \
          flag = 0;                                                              \
          for (i=0; i < iters; i++) {                                            \
            GASNET_Safe(AMREQUEST(peer, PING_HIDX##_flood, myseg, sz DEST));     \
          }                                                                      \
          GASNET_BLOCKUNTIL(flag == iters);                                      \
          report(msg,TIME() - start, iters);                                     \
        }                                                                        \
        BARRIER();                                                               \
      }                                                                          \
    }                                                                            \
    if (mynode == 0) { printf("\n"); fflush(stdout); }                           \
    BARRIER();                                                                   \
  } while (0)

  #define MEDDEST
  #define LONGDEST , peerseg
  TESTAM_PERF("AMMedium",    gasnet_AMRequestMedium0,    hidx_ping_medhandler,  hidx_pong_medhandler,  maxmed,  MEDDEST);
  TESTAM_PERF("AMLong",      gasnet_AMRequestLong0,      hidx_ping_longhandler, hidx_pong_longhandler, maxlong, LONGDEST);
  TESTAM_PERF("AMLongAsync", gasnet_AMRequestLongAsync0, hidx_ping_longhandler, hidx_pong_longhandler, maxlong, LONGDEST);

  /* ------------------------------------------------------------------------------------ */
  }

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
