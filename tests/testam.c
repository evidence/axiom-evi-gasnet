/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testam.c,v $
 *     $Date: 2005/05/15 22:28:16 $
 * $Revision: 1.20 $
 * Description: GASNet Active Messages performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

int mynode = 0;
void *myseg = NULL;
int sender, recvr;
int peer;
void *peerseg = NULL;

void report(const char *desc, int64_t totaltime, int iters) {
  if (sender) {
      char nodestr[10];
      if (gasnet_nodes() > 2) sprintf(nodestr,"%i: ",mynode);
      else nodestr[0] = '\0';
      printf("%s%-56s: %7.3f sec %7.3f us\n",
        nodestr, desc, ((float)totaltime)/1000000, ((float)totaltime)/iters);
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
int iters=0;
int i = 0;
int maxsz=64*1024;
int maxmed, maxlong;
void doAMShort();
void doAMMed();
void doAMLong();
void doAMLongAsync();

int main(int argc, char **argv) {
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
  peerseg = TEST_SEG(peer);
  sender = mynode % 2 == 0;
  recvr = (!sender || (gasnet_nodes()%2 == 1 && mynode == gasnet_nodes()-1));

  if (mynode == 0) {
      printf("Running AM performance test with %i iterations...\n",iters);
      printf("%-50s         Total time  Avg. time\n"
             "%-50s         ----------  ---------\n", "", "");
      fflush(stdout);
  }

  doAMShort();
  doAMMed();
  doAMLong();
  doAMLongAsync();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
/* ------------------------------------------------------------------------------------ */
void doAMShort() {
    GASNET_BEGIN_FUNCTION();

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
      report("AMShort ping-pong ReqRep roundtrip latency",TIME() - start, iters);
    }

    BARRIER();
    /* ------------------------------------------------------------------------------------ */
    {
      int64_t start = TIME();
      flag = -1;
      BARRIER();
      if (sender && recvr) {
        assert(peer == mynode);
        for (i=0; i < iters; i++) {
          int lim = i << 1;
          GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_pong_shorthandler));
          GASNET_BLOCKUNTIL(flag == lim);
          lim++;
          GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_pong_shorthandler));
          GASNET_BLOCKUNTIL(flag == lim);
        }
      } else if (sender) {
        for (i=0; i < iters; i++) {
          GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_pong_shorthandler));
          GASNET_BLOCKUNTIL(flag == i);
        }
      } else if (recvr) {
        for (i=0; i < iters; i++) {
          GASNET_BLOCKUNTIL(flag == i);
          GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_pong_shorthandler));
        }
      }
      report("AMShort ping-pong ReqReq roundtrip latency",TIME() - start, iters);
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
      if (recvr) GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
      report("AMShort flood one-way Req throughput",TIME() - start, iters);
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
      report("AMShort flood roundtrip ReqRep throughput",TIME() - start, iters);
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();
}
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
        sprintf(msg, DESC_STR"(sz=%5i) ping-pong ReqRep roundtrip latency", sz); \
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
        sprintf(msg, DESC_STR"(sz=%5i) ping-pong ReqReq roundtrip latency", sz); \
        BARRIER();                                                               \
        {                                                                        \
          int64_t start = TIME();                                                \
          flag = -1;                                                             \
          BARRIER();                                                             \
          if (sender && recvr) {                                                 \
            assert(peer == mynode);                                              \
            for (i=0; i < iters; i++) {                                          \
              int lim = i << 1;                                                  \
              GASNET_Safe(AMREQUEST(peer, PONG_HIDX, myseg, sz DEST));           \
              GASNET_BLOCKUNTIL(flag == lim);                                    \
              lim++;                                                             \
              GASNET_Safe(AMREQUEST(peer, PONG_HIDX, myseg, sz DEST));           \
              GASNET_BLOCKUNTIL(flag == lim);                                    \
            }                                                                    \
          } else if (sender) {                                                   \
            for (i=0; i < iters; i++) {                                          \
              GASNET_Safe(AMREQUEST(peer, PONG_HIDX, myseg, sz DEST));           \
              GASNET_BLOCKUNTIL(flag == i);                                      \
            }                                                                    \
          } else if (recvr) {                                                    \
            for (i=0; i < iters; i++) {                                          \
              GASNET_BLOCKUNTIL(flag == i);                                      \
              GASNET_Safe(AMREQUEST(peer, PONG_HIDX, myseg, sz DEST));           \
            }                                                                    \
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
        sprintf(msg, DESC_STR"(sz=%5i) flood one-way Req throughput", sz);       \
        BARRIER();                                                               \
        if (sender) {                                                            \
          int64_t start = TIME();                                                \
          for (i=0; i < iters; i++) {                                            \
            GASNET_Safe(AMREQUEST(peer, PONG_HIDX##_flood, myseg, sz DEST));     \
          }                                                                      \
          if (recvr) GASNET_BLOCKUNTIL(flag == iters);                           \
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
        sprintf(msg, DESC_STR"(sz=%5i) flood roundtrip ReqRep throughput", sz);  \
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
/* ------------------------------------------------------------------------------------ */
void doAMMed() {
  GASNET_BEGIN_FUNCTION();
  TESTAM_PERF("AMMedium",    gasnet_AMRequestMedium0,    hidx_ping_medhandler,  hidx_pong_medhandler,  maxmed,  MEDDEST);
}
/* ------------------------------------------------------------------------------------ */
void doAMLong() {
  GASNET_BEGIN_FUNCTION();
  TESTAM_PERF("AMLong",      gasnet_AMRequestLong0,      hidx_ping_longhandler, hidx_pong_longhandler, maxlong, LONGDEST);
}
/* ------------------------------------------------------------------------------------ */
void doAMLongAsync() {
  GASNET_BEGIN_FUNCTION();
  TESTAM_PERF("AMLongAsync", gasnet_AMRequestLongAsync0, hidx_ping_longhandler, hidx_pong_longhandler, maxlong, LONGDEST);
}
/* ------------------------------------------------------------------------------------ */
