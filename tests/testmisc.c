/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testmisc.c,v $
 *     $Date: 2005/02/28 10:23:10 $
 * $Revision: 1.13 $
 * Description: GASNet misc performance test
 *   Measures the overhead associated with a number of purely local 
 *   operations that involve no communication. 
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_tools.h>

#include <test.h>

#if defined(GASNET_PAR) || defined (GASNET_PARSYNC)
  #include <pthread.h>
#endif

int mynode = 0;

void report(const char *desc, int64_t totaltime, int iters) {
  if (mynode == 0) {
      printf("%-50s: %8.3f sec  %8.3f us/iter\n",
        desc, ((float)totaltime)/1000000, ((float)totaltime)/iters);
      fflush(stdout);
  }
}
/* ------------------------------------------------------------------------------------ */
#define hidx_null_shorthandler        201
#define hidx_justreply_shorthandler   202

#define hidx_null_medhandler          203
#define hidx_justreply_medhandler     204

#define hidx_null_longhandler         205
#define hidx_justreply_longhandler    206

void null_shorthandler(gasnet_token_t token) {
}

void justreply_shorthandler(gasnet_token_t token) {
  gasnet_AMReplyShort0(token, hidx_null_shorthandler);
}

void null_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
}

void justreply_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  gasnet_AMReplyMedium0(token, hidx_null_medhandler, buf, nbytes);
}

void null_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
}

void justreply_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  gasnet_AMReplyLong0(token, hidx_null_longhandler, buf, nbytes, buf);
}
/* ------------------------------------------------------------------------------------ */
/* This tester measures the performance of a number of miscellaneous GASNet functions 
   that don't involve actual communication, to assist in evaluating the overhead of 
   the GASNet layer itself
 */
int main(int argc, char **argv) {
  int iters=0;
  int i = 0;
  void *myseg = NULL;
  gasnet_handlerentry_t htable[] = { 
    { hidx_null_shorthandler,       null_shorthandler },
    { hidx_justreply_shorthandler,  justreply_shorthandler },
    { hidx_null_medhandler,         null_medhandler },
    { hidx_justreply_medhandler,    justreply_medhandler },
    { hidx_null_longhandler,        null_longhandler },
    { hidx_justreply_longhandler,   justreply_longhandler }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t),
                            TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  TEST_DEBUGPERFORMANCE_WARNING();

  MSG("running...");

  mynode = gasnet_mynode();
  myseg = TEST_MYSEG();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 100000;

  if (mynode == 0) {
      printf("Running misc performance test with %i iterations...\n",iters);
      printf("GASNET_CONFIG:%s\n",GASNET_CONFIG_STRING);
      printf("%-50s    Total time    Avg. time\n"
             "%-50s    ----------    ---------\n", "", "");
      fflush(stdout);
  }

  /* ------------------------------------------------------------------------------------ */
  { GASNET_BEGIN_FUNCTION();

    char p[1];
    gasnet_hsl_t hsl = GASNET_HSL_INITIALIZER;
    gasnett_atomic_t a = gasnett_atomic_init(0);
    int32_t temp = 0;
    int8_t bigtemp[1024];
    gasnet_handle_t handles[8];

    for (i=0;i<8;i++) handles[i] = GASNET_INVALID_HANDLE;

    #define TIME_OPERATION_FULL(desc, preop, op, postop) \
      { int64_t start,end;                               \
        BARRIER();                                       \
        start = TIME();                                  \
        preop;                                           \
        for (i=0; i < iters; i++) { op; }                \
        postop;                                          \
        end = TIME();                                    \
        BARRIER();                                       \
        if ((desc) && ((char*)(desc))[0])                \
          report((desc), end - start, iters);            \
        else report(#op, end - start, iters);            \
      }
    #define TIME_OPERATION(desc, op) TIME_OPERATION_FULL(desc, {}, op, {})

    TIME_OPERATION("Tester overhead", {});
    
    TIME_OPERATION("Do-nothing gasnet_AMPoll()",
      { gasnet_AMPoll(); });
    
    TIME_OPERATION("Loopback do-nothing gasnet_AMRequestShort0()",
      { gasnet_AMRequestShort0(mynode, hidx_null_shorthandler); });

    TIME_OPERATION("Loopback do nothing AM short request-reply",
      { gasnet_AMRequestShort0(mynode, hidx_justreply_shorthandler); });

    TIME_OPERATION("Loopback do-nothing gasnet_AMRequestMedium0()",
      { gasnet_AMRequestMedium0(mynode, hidx_null_medhandler, p, 0); });

    TIME_OPERATION("Loopback do nothing AM medium request-reply",
      { gasnet_AMRequestMedium0(mynode, hidx_justreply_medhandler, p, 0); });

    TIME_OPERATION("Loopback do-nothing gasnet_AMRequestLong0()",
      { gasnet_AMRequestLong0(mynode, hidx_null_medhandler, p, 0, myseg); });

    TIME_OPERATION("Loopback do nothing AM long request-reply",
      { gasnet_AMRequestLong0(mynode, hidx_justreply_medhandler, p, 0, myseg); });

    /* ------------------------------------------------------------------------------------ */

    TIME_OPERATION("hold/resume interrupts",
      { gasnet_hold_interrupts(); gasnet_resume_interrupts(); });

    #if defined(GASNET_PAR) || defined (GASNET_PARSYNC)
      { static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        TIME_OPERATION("lock/unlock uncontended pthread mutex",
          { pthread_mutex_lock(&mutex); pthread_mutex_unlock(&mutex); });
      }
    #endif

    TIME_OPERATION("lock/unlock uncontended HSL",
      { gasnet_hsl_lock(&hsl); gasnet_hsl_unlock(&hsl); });

    TIME_OPERATION("gasnett_local_wmb", gasnett_local_wmb());
    TIME_OPERATION("gasnett_local_rmb", gasnett_local_rmb());
    TIME_OPERATION("gasnett_local_mb", gasnett_local_mb());

    TIME_OPERATION("gasnett_atomic_read", gasnett_atomic_read(&a));
    TIME_OPERATION("gasnett_atomic_increment", gasnett_atomic_increment(&a));
    TIME_OPERATION("gasnett_atomic_decrement", gasnett_atomic_decrement(&a));
    TIME_OPERATION("gasnett_atomic_decrement_and_test", gasnett_atomic_decrement_and_test(&a));

    /* ------------------------------------------------------------------------------------ */

    TIME_OPERATION("local 4-byte gasnet_put",
      { gasnet_put(mynode, myseg, &temp, 4); });

    TIME_OPERATION("local 4-byte gasnet_put_nb",
      { gasnet_wait_syncnb(gasnet_put_nb(mynode, myseg, &temp, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_put_nbi", {},
      { gasnet_put_nbi(mynode, myseg, &temp, 4); },
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("local 4-byte gasnet_put_bulk",
      { gasnet_put_bulk(mynode, myseg, &temp, 4); });

    TIME_OPERATION("local 4-byte gasnet_put_nb_bulk",
      { gasnet_wait_syncnb(gasnet_put_nb_bulk(mynode, myseg, &temp, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_put_nbi_bulk", {},
      { gasnet_put_nbi(mynode, myseg, &temp, 4); },
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("local 4-byte gasnet_put_val",
      { gasnet_put_val(mynode, myseg, temp, 4); });

    TIME_OPERATION("local 4-byte gasnet_put_nb_val",
      { gasnet_wait_syncnb(gasnet_put_nb_val(mynode, myseg, temp, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_put_nbi_val", {},
      { gasnet_put_nbi_val(mynode, myseg, temp, 4); },
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("local 1024-byte gasnet_put_bulk",
      { gasnet_put_bulk(mynode, myseg, &bigtemp, 1024); });

    /* ------------------------------------------------------------------------------------ */

    TIME_OPERATION("local 4-byte gasnet_get",
      { gasnet_get(&temp, mynode, myseg, 4); });

    TIME_OPERATION("local 4-byte gasnet_get_nb",
      { gasnet_wait_syncnb(gasnet_get_nb(&temp, mynode, myseg, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_get_nbi", {},
      { gasnet_get_nbi(&temp, mynode, myseg, 4); },
      { gasnet_wait_syncnbi_gets(); });

    TIME_OPERATION("local 4-byte gasnet_get_bulk",
      { gasnet_get_bulk(&temp, mynode, myseg, 4); });

    TIME_OPERATION("local 4-byte gasnet_get_nb_bulk",
      { gasnet_wait_syncnb(gasnet_get_nb_bulk(&temp, mynode, myseg, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_get_nbi_bulk", {},
      { gasnet_get_nbi_bulk(&temp, mynode, myseg, 4); },
      { gasnet_wait_syncnbi_gets(); });

    TIME_OPERATION("local 4-byte gasnet_get_val",
      { temp = (int32_t)gasnet_get_val(mynode, myseg, 4); });

    TIME_OPERATION("local 4-byte gasnet_get_nb_val",
      { gasnet_valget_handle_t handle = gasnet_get_nb_val(mynode, myseg, 4);
        temp = (int32_t)gasnet_wait_syncnb_valget(handle);
      });

    TIME_OPERATION("local 1024-byte gasnet_get_bulk",
      { gasnet_get_bulk(&bigtemp, mynode, myseg, 1024); });

    /* ------------------------------------------------------------------------------------ */

    { int32_t temp1 = 0;
      int32_t temp2 = 0;
      int32_t volatile *ptemp1 = &temp1;
      int32_t volatile *ptemp2 = &temp2;
      TIME_OPERATION("local 4-byte assignment",
        { *(ptemp1) = *(ptemp2); });
    }

    { int8_t temp1[1024];
      int8_t temp2[1024];
      int64_t start = TIME();
      TIME_OPERATION("local 1024-byte memcpy",
        { memcpy(temp1, temp2, 1024); });
    }

    /* ------------------------------------------------------------------------------------ */

    TIME_OPERATION("do-nothing gasnet_wait_syncnb()",
      { gasnet_wait_syncnb(GASNET_INVALID_HANDLE);  });

    TIME_OPERATION("do-nothing gasnet_try_syncnb()",
      { gasnet_try_syncnb(GASNET_INVALID_HANDLE); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnb_all() (8 handles)",
      { gasnet_wait_syncnb_all(handles, 8); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnb_some() (8 handles)",
      { gasnet_wait_syncnb_some(handles, 8); });

    TIME_OPERATION("do-nothing gasnet_try_syncnb_all() (8 handles)",
      { gasnet_try_syncnb_all(handles, 8);  });

    TIME_OPERATION("do-nothing gasnet_try_syncnb_some() (8 handles)",
      { gasnet_try_syncnb_some(handles, 8); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnbi_all()",
      { gasnet_wait_syncnbi_all(); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnbi_puts()",
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnbi_gets()",
      { gasnet_wait_syncnbi_gets(); });

    TIME_OPERATION("do-nothing gasnet_try_syncnbi_all()",
      { gasnet_try_syncnbi_all(); });

    TIME_OPERATION("do-nothing gasnet_try_syncnbi_puts()",
      { gasnet_try_syncnbi_puts(); });

    TIME_OPERATION("do-nothing gasnet_try_syncnbi_gets()",
      { gasnet_try_syncnbi_gets(); });

    TIME_OPERATION("do-nothing begin/end nbi accessregion",
      { gasnet_begin_nbi_accessregion();
        gasnet_wait_syncnb(gasnet_end_nbi_accessregion());
      });

    TIME_OPERATION("single-node barrier",
      { gasnete_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS);            
        gasnete_barrier_wait(0,GASNET_BARRIERFLAG_ANONYMOUS); 
      });
    if (gasnet_nodes() > 1)
      MSG0("Note: this is actually the barrier time for %i nodes, "
           "since you're running with more than one node.\n", (int)gasnet_nodes());
  /* ------------------------------------------------------------------------------------ */

  }

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
