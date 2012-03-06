/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_thread.c,v $
 *     $Date: 2012/03/06 07:23:07 $
 * $Revision: 1.2 $
 * Description: GASNet vapi/ibv conduit implementation, progress thread logic
 * Copyright 2012, LBNL
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

#include <errno.h>

#if !GASNETI_CONDUIT_THREADS

/* Protect against compiler warnings about "empty compilation unit" */
extern int gasnetc_thread_dummy = 1;

#else

GASNETI_INLINE(gasnetc_testcancel)
void gasnetc_testcancel(void) {
  const int save_errno = errno;
  pthread_testcancel();
  if_pf (GASNETC_IS_EXITING()) pthread_exit(NULL);
  errno = save_errno;
}

static void * gasnetc_progress_thread(void *arg)
{
  gasnetc_progress_thread_t *pthr_p = arg;
  const gasnetc_hca_hndl_t hca_hndl         = pthr_p->hca;
  const gasnetc_cq_hndl_t cq_hndl           = pthr_p->cq;
  const gasnetc_comp_handler_t compl_hndl   = pthr_p->compl;
  void (* const fn)(gasnetc_wc_t *, void *) = pthr_p->fn;
  void * const fn_arg                       = pthr_p->fn_arg;
  const uint64_t min_us                     = pthr_p->min_us;

  while (1) {
    gasnetc_wc_t comp;
    int rc;

  #if GASNET_CONDUIT_VAPI
    rc = EVAPI_poll_cq_block(hca_hndl, cq_hndl, 0, &comp);
  #else
    rc = ibv_poll_cq(cq_hndl, 1, &comp);
  #endif
    gasnetc_testcancel();
    if (rc == GASNETC_POLL_CQ_OK) {
      gasneti_assert((comp.opcode == GASNETC_WC_RECV) ||
		     (comp.status != GASNETC_WC_SUCCESS));
      (fn)(&comp, fn_arg);

      /* Throttle thread's rate */
      if_pf (min_us) {
        uint64_t prev = pthr_p->prev_time;
        if_pt (prev) {
          uint64_t elapsed = gasneti_ticks_to_us(gasneti_ticks_now() - prev);
    
          if (elapsed < min_us) {
          #if HAVE_USLEEP
            uint64_t us_delay = (min_us - elapsed);
            usleep(us_delay);
          #elif HAVE_NANOSLEEP
            uint64_t ns_delay = 1000 * (min_us - elapsed);
            struct timespec ts = { ns_delay / 1000000000L, us_delay % 1000000000L };
            nanosleep(&ts, NULL);
          #elif HAVE_NSLEEP
            uint64_t ns_delay = 1000 * (min_us - elapsed);
            struct timespec ts = { ns_delay / 1000000000L, us_delay % 1000000000L };
            nsleep(&ts, NULL);
          #else
            do {
              gasneti_yield();
              elapsed = gasneti_ticks_to_us(gasneti_ticks_now() - prev);
            } while (elapsed < min_us);
          #endif
          }
        }
        pthr_p->prev_time = gasneti_ticks_now();
      }
    } else if (rc == GASNETC_POLL_CQ_EMPTY) {
    #if GASNET_CONDUIT_VAPI
      continue; /* false wake up - loop again */
    #else
      gasnetc_cq_hndl_t the_cq;
      void *the_ctx;

      /* block for event on the empty CQ */
      rc = ibv_get_cq_event(compl_hndl, &the_cq, &the_ctx);
      gasnetc_testcancel();
      GASNETC_VAPI_CHECK(rc, "while blocked for CQ event");
      gasneti_assert(the_cq == cq_hndl);

      /* ack the event and rearm */
      ibv_ack_cq_events(cq_hndl, 1);
      rc = ibv_req_notify_cq(cq_hndl, 0);
      GASNETC_VAPI_CHECK(rc, "while requesting CQ events");

      /* loop to poll for the new completion */
    #endif
    } else {
      gasneti_fatalerror("failed CQ poll an async thread");
    }
  }
}

extern void
gasnetc_spawn_progress_thread(gasnetc_progress_thread_t *pthr_p)
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  (void)pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); /* ignore failures */
  gasneti_assert_zeroret(pthread_create(&pthr_p->thread_id, &attr, gasnetc_progress_thread, pthr_p));
  gasneti_assert_zeroret(pthread_attr_destroy(&attr));
}
#endif
