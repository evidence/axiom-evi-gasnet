/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testcore4.c,v $
 *     $Date: 2010/04/07 01:39:10 $
 * $Revision: 1.1 $
 * Description: GASNet Active Messages conformance test
 * Copyright 2009, Lawrence Berkeley National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#define TEST_SEGSZ (64ULL*1024)
#include <test.h>

#define XFER_SZ 8

gasnet_node_t mynode = 0;
gasnet_node_t peer = 0;
void *myseg = NULL;
void *peerseg = NULL;

#define hidx_mybase 200

#define hidx_ping_shorthandler   (hidx_mybase + 1)
#define hidx_pong_shorthandler   (hidx_mybase + 2)

#define hidx_ping_medhandler     (hidx_mybase + 3)
#define hidx_pong_medhandler     (hidx_mybase + 4)

#define hidx_ping_longhandler    (hidx_mybase + 5)
#define hidx_pong_longhandler    (hidx_mybase + 6)

/* For > 0 args we use first args to distinguish short/med/long and request/reply */
#define hidx_Shandler(args)      (hidx_mybase + 2*args + 5)
#define hidx_MLhandler(args)     (hidx_mybase + 2*args + 6)

/* Preprocess-time iterator with distinct base case */
#define HITER1(base,macro)     base
#define HITER2(base,macro)     HITER1(base,macro)  macro(2)
#define HITER3(base,macro)     HITER2(base,macro)  macro(3)
#define HITER4(base,macro)     HITER3(base,macro)  macro(4)
#define HITER5(base,macro)     HITER4(base,macro)  macro(5)
#define HITER6(base,macro)     HITER5(base,macro)  macro(6)
#define HITER7(base,macro)     HITER6(base,macro)  macro(7)
#define HITER8(base,macro)     HITER7(base,macro)  macro(8)
#define HITER9(base,macro)     HITER8(base,macro)  macro(9)
#define HITER10(base,macro)    HITER9(base,macro)  macro(10)
#define HITER11(base,macro)    HITER10(base,macro) macro(11)
#define HITER12(base,macro)    HITER11(base,macro) macro(12)
#define HITER13(base,macro)    HITER12(base,macro) macro(13)
#define HITER14(base,macro)    HITER13(base,macro) macro(14)
#define HITER15(base,macro)    HITER14(base,macro) macro(15)
#define HITER16(base,macro)    HITER15(base,macro) macro(16)

#define HARG_(val)  , val
#define HARGS(args) HITER##args(arg1,HARG_)

#define HARGPROTO_(val) , gasnet_handlerarg_t arg##val
#define HARGPROTO(args) HITER##args(gasnet_handlerarg_t arg1,HARGPROTO_)

/* Simpler iterator over required arg counts */
#if PLATFORM_ARCH_32
  #define HFOREACH(macro) \
    macro(1)  macro(2)  macro(3)  macro(4) \
    macro(5)  macro(6)  macro(7)  macro(8)
#else
  #define HFOREACH(macro) \
    macro(1)  macro(2)  macro(3)  macro(4) \
    macro(5)  macro(6)  macro(7)  macro(8) \
    macro(9)  macro(10) macro(11) macro(12)\
    macro(13) macro(14) macro(15) macro(16)
#endif

#define SARGS(dest,args) (dest, hidx_Shandler(args), HARGS(args))
#define MARGS(dest,args) (dest, hidx_MLhandler(args), myseg, XFER_SZ, HARGS(args))
#define LARGS(dest,args) (dest, hidx_MLhandler(args), myseg, XFER_SZ, peerseg, HARGS(args))

/* non-zero to catch case of zero passed "accidentally" */
#define DO_DONE 16
#define DO_SREP 32
#define DO_MREP 64
#define DO_LREP 128

#define HCHECK(val) ; assert_always(arg##val == val)
#define HBODY(args) do {                                           \
    gasnet_handlerarg_t orig_arg1 = arg1;                          \
    gasnet_node_t srcid;                                           \
    gasnet_AMGetMsgSource(token, &srcid);                          \
    assert_always(srcid == peer);                                  \
    HITER##args((void)0,HCHECK);                                   \
    arg1 = DO_DONE;                                                \
    switch(orig_arg1) {                                            \
      case DO_DONE:                                                \
        flag++;                                                    \
        break;                                                     \
      case DO_SREP:                                                \
        GASNET_Safe(gasnet_AMReplyShort##args  SARGS(token,args)); \
        break;                                                     \
      case DO_MREP:                                                \
        GASNET_Safe(gasnet_AMReplyMedium##args MARGS(token,args)); \
        break;                                                     \
      case DO_LREP:                                                \
        GASNET_Safe(gasnet_AMReplyLong##args   LARGS(token,args)); \
        break;                                                     \
      default:                                                     \
        FATALERR("Invalid arg1 = %d", arg1);                       \
    }                                                              \
  } while(0);
#define HDEFN(args) \
    void Shandler##args(gasnet_token_t token, HARGPROTO(args)) \
        { HBODY(args); } \
    void MLhandler##args(gasnet_token_t token, void *buf, size_t nbytes, HARGPROTO(args))\
        { HBODY(args); }

#define HTABLE(args)                          \
  { hidx_Shandler(args), Shandler##args },    \
  { hidx_MLhandler(args), MLhandler##args },

#define HTEST(args) do {                                             \
    gasnet_handlerarg_t arg1;                                        \
    int goal = flag + 1;                                             \
    MSG0("testing %d-argument AM calls", args);                      \
    arg1 = DO_SREP;                                                  \
      GASNET_Safe(gasnet_AMRequestShort##args SARGS(peer,args));     \
      GASNET_BLOCKUNTIL(flag == goal); ++goal;                       \
    arg1 = DO_MREP;                                                  \
      GASNET_Safe(gasnet_AMRequestMedium##args MARGS(peer,args));    \
      GASNET_BLOCKUNTIL(flag == goal); ++goal;                       \
    arg1 = DO_LREP;                                                  \
      GASNET_Safe(gasnet_AMRequestLong##args LARGS(peer,args));      \
      GASNET_BLOCKUNTIL(flag == goal); ++goal;                       \
      GASNET_Safe(gasnet_AMRequestLongAsync##args LARGS(peer,args)); \
      GASNET_BLOCKUNTIL(flag == goal); ++goal;                       \
    BARRIER();                                                       \
  } while(0);


/* Define all the handlers */
volatile int flag = 0;
HFOREACH(HDEFN)
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


int main(int argc, char **argv) {
  gasnet_handlerentry_t htable[] = { 
    HFOREACH(HTABLE)
    { hidx_ping_shorthandler,  ping_shorthandler  },
    { hidx_pong_shorthandler,  pong_shorthandler  },
    { hidx_ping_medhandler,    ping_medhandler    },
    { hidx_pong_medhandler,    pong_medhandler    },
    { hidx_ping_longhandler,   ping_longhandler   },
    { hidx_pong_longhandler,   pong_longhandler   }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t),
                            TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

  test_init("testcore4", 0, "[no argument]");
  if (argc > 1) test_usage();

  TEST_PRINT_CONDUITINFO();

  mynode = gasnet_mynode();
  peer = mynode ^ 1;
  if (peer == gasnet_nodes()) {
    /* w/ odd # of nodes, last one talks to self */
    peer = mynode;
  }

  myseg = TEST_MYSEG();
  peerseg = TEST_SEG(peer);

  BARRIER();
  
  MSG0("testing 0-argument AM calls");

  GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_ping_shorthandler));
  GASNET_BLOCKUNTIL(flag == 1);

  GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_ping_medhandler, myseg, XFER_SZ));
  GASNET_BLOCKUNTIL(flag == 2);

  GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_ping_longhandler, myseg, XFER_SZ, peerseg));
  GASNET_BLOCKUNTIL(flag == 3);
 
  GASNET_Safe(gasnet_AMRequestLongAsync0(peer, hidx_ping_longhandler, myseg, XFER_SZ, peerseg));
  GASNET_BLOCKUNTIL(flag == 4);

  BARRIER();

  /* Now 1 ... 8 or 16 */
  HFOREACH(HTEST)

  MSG("done.");

  gasnet_exit(0);
  return 0;
}

/* ------------------------------------------------------------------------------------ */
