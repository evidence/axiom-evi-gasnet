/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_reqrep.c                  $
 *     $Date: 2002/08/05 10:23:44 $
 * $Revision: 1.1 $
 * Description: GASNet elan conduit - AM request/reply implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>
#include <gasnet_core_internal.h>

#include <unistd.h>

/* locks:
    elan lock - protects all elan calls and tport rx fifo
    sendfifo lock - protects tport tx fifo
   should never hold more than one lock
 */
#ifdef GASNETI_THREADS
  #include <pthread.h>
  static pthread_mutex_t gasnetc_elanLock = PTHREAD_MUTEX_INITIALIZER;
  static pthread_mutex_t gasnetc_sendfifoLock = PTHREAD_MUTEX_INITIALIZER;
  #define LOCK_ELAN()       pthread_mutex_lock(&gasnetc_elanLock)
  #define UNLOCK_ELAN()     pthread_mutex_unlock(&gasnetc_elanLock)
  #define LOCK_SENDFIFO()   pthread_mutex_lock(&gasnetc_sendfifoLock)
  #define UNLOCK_SENDFIFO() pthread_mutex_unlock(&gasnetc_sendfifoLock)
#else
  #define LOCK_ELAN()
  #define UNLOCK_ELAN()
  #define LOCK_SENDFIFO()
  #define UNLOCK_SENDFIFO()
#endif

#define GASNETC_MEDHEADER_PADARG(numargs) \
        ((numargs & 0x1) ^ ((GASNETC_MED_HEADERSZ>>2) & 0x1))

/* round up a size to a given power of 2 */
#define ROUNDUP_TO_ALIGN(sz, align) ( ((sz) + (align)-1) & ~((align)-1) )

/* ------------------------------------------------------------------------------------ */
static ELAN_QUEUE *gasnetc_queue = NULL;
static ELAN_MAIN_QUEUE *gasnetc_mainqueue = NULL;
static int gasnetc_queuesz = 0; /* queue size for main queue and tport bufs */

static gasnetc_bufdesc_t *gasnetc_tportTxFree = NULL; /* list of free tx bufs (from startup) */
static gasnetc_bufdesc_t *gasnetc_tportTxFIFOHead = NULL; /* list of tx's in progress, */
static gasnetc_bufdesc_t *gasnetc_tportTxFIFOTail = NULL; /* and some will NULL events that havent launched yet */

static gasnetc_bufdesc_t *gasnetc_tportRxFIFOHead = NULL; /* list of rx's waiting */
static gasnetc_bufdesc_t *gasnetc_tportRxFIFOTail = NULL;
/* ------------------------------------------------------------------------------------ */
static gasnetc_bufdesc_t *gasnetc_tportGetTxBuf() {
  /* pop send fifo head buffer and wait for completion,
     add to send fifo with null event 
     assumes elan lock NOT held
  */
  gasnetc_bufdesc_t *desc = NULL;
  
  LOCK_SENDFIFO();
  while (!desc) {
    if (gasnetc_tportTxFree) { /* free list contains some bufs */
      desc = gasnetc_tportTxFree;
      gasnetc_tportTxFree = desc->next;
      assert(desc->event == NULL);
    } else { /* need to reap some tx bufs */
      if (gasnetc_tportTxFIFOHead && 
          gasnetc_tportTxFIFOHead->event &&
          elan_tportTxDone(gasnetc_tportTxFIFOHead->event)) {
        /* common case - oldest tx is complete */
        desc = gasnetc_tportTxFIFOHead;
        elan_tportTxWait(desc->event); /* TODO: necessary? */
        gasnetc_tportTxFIFOHead = gasnetc_tportTxFIFOHead->next;
        if (gasnetc_tportTxFIFOHead == NULL) gasnetc_tportTxFIFOTail = NULL;
      } else { /* poop - head busy, need to scan for tx */
        if (gasnetc_tportTxFIFOHead) {
          gasnetc_bufdesc_t *lastdesc = gasnetc_tportTxFIFOHead;
          while (lastdesc->next) {
            gasnetc_bufdesc_t *tmp = lastdesc->next;
            if (tmp->event && elan_tportTxDone(tmp->event)) { /* found one */
              lastdesc->next = tmp->next;
              if (lastdesc->next == NULL) gasnetc_tportTxFIFOTail = lastdesc;
              desc = tmp;
              elan_tportTxWait(desc->event); /* TODO: necessary? */
              break;
            }
            lastdesc = lastdesc->next;
          }
          assert(desc || lastdesc == gasnetc_tportTxFIFOTail);
        }
        if (!desc) { /* nothing available now - poll */
          UNLOCK_SENDFIFO();
          gasnetc_AMPoll();
          LOCK_SENDFIFO();
        }
      }
    }
  }
  UNLOCK_SENDFIFO();

  /* add to send fifo - event will be filled in later by caller */
  desc->event = NULL;
  desc->next = NULL;
  if (gasnetc_tportTxFIFOTail) { /* fifo non-empty */
    assert(gasnetc_tportTxFIFOHead);
    assert(gasnetc_tportTxFIFOTail->next == NULL);
    gasnetc_tportTxFIFOTail->next = desc;
    gasnetc_tportTxFIFOTail = desc;
  } else {
    assert(!gasnetc_tportTxFIFOHead);
    gasnetc_tportTxFIFOHead = desc;
    gasnetc_tportTxFIFOTail = desc;
  }
  return desc;
}
/* ------------------------------------------------------------------------------------ */
static void gasnetc_tportReleaseTxBuf(gasnetc_bufdesc_t *desc) {
  /* release a Tx buf without sending it
     assumes elan lock NOT held  
   */
  LOCK_SENDFIFO();
    assert(desc->event == NULL);
    desc->next = gasnetc_tportTxFree;
    gasnetc_tportTxFree = desc;
  UNLOCK_SENDFIFO();
}
/* ------------------------------------------------------------------------------------ */
static gasnetc_bufdesc_t *gasnetc_tportCheckRx() {
 /* return a buffer if there's an incoming tport msg 
     assumes elan lock is held 
  */
  gasnetc_bufdesc_t *desc = gasnetc_tportRxFIFOHead;

  if (desc && elan_tportRxDone(desc->event)) {
    int sender,tag,size;
    gasnetc_buf_t *buf = elan_tportRxWait(desc->event, &sender, &tag, &size);
    desc->buf = buf;
    desc->event = NULL;
    gasnetc_tportRxFIFOHead = desc->next;
    desc->next = NULL;
    if_pf (gasnetc_tportRxFIFOHead == NULL) gasnetc_tportRxFIFOTail = NULL;
    return desc;
  } 
  else return NULL;
}
/* ------------------------------------------------------------------------------------ */
static void gasnetc_tportAddRxBuf(gasnetc_bufdesc_t *desc) {
  /* issue an rx and return the buffer to the rx queue
     assumes elan lock is held 
   */

  if (desc->buf != desc->buf_owned) {/* free a system buffer, if neccessary */
    elan_tportBufFree(TPORT(),desc->buf);
    desc->buf = desc->buf_owned;
  }

  /* post a new recv */
  assert(!desc->event);
  desc->event = elan_tportRxStart(TPORT(), 
                    ELAN_TPORT_RXBUF | ELAN_TPORT_RXANY, 
                    0, 0, 0, 0,
                    desc->buf, sizeof(gasnetc_buf_t));
  assert(desc->event);

  /* push on tail of queue */
  desc->next = NULL;
  if_pt (gasnetc_tportRxFIFOTail) {
    assert(gasnetc_tportRxFIFOHead);
    gasnetc_tportRxFIFOTail->next = desc;
    gasnetc_tportRxFIFOTail = desc;
  } else { /* list empty (rare case..) */
    assert(gasnetc_tportRxFIFOHead == NULL);
    gasnetc_tportRxFIFOHead = desc;
    gasnetc_tportRxFIFOTail = desc;
  }
}

/* ------------------------------------------------------------------------------------ */
extern void gasnetc_initbufs() {
  /* create a tport message queue */
  ELAN_QUEUE *tport_queue;
  #ifdef ELAN_VER_1_2
    tport_queue = elan_gallocQueue(BASE()->galloc, GROUP());
  #else
    tport_queue = elan_gallocQueue(BASE(), GROUP());
  #endif
  if (tport_queue == NULL) gasneti_fatalerror("elan_gallocQueue() failed");

  #if 0
    GASNETI_TRACE_PRINTF(D,("TPORT queue: main="GASNETI_LADDRFMT
                                       "  elan="GASNETI_LADDRFMT,
                                       GASNETI_LADDRSTR(tport_queue), 
                                       GASNETI_LADDRSTR(elan_main2elan(STATE(),tport_queue))));
  #endif

  /* init tport with the default values we got in base */
  gasnetc_elan_tport = elan_tportInit(STATE(), 
                                      tport_queue, 
    #ifdef ELAN_VER_1_2
                                      elan_main2elan(STATE(),tport_queue),
    #endif
                                      BASE()->tport_nslots,
                                      BASE()->tport_smallmsg,
                                      BASE()->tport_bigmsg,
                                      BASE()->waitType,
                                      BASE()->retryCount
    #if defined(ELAN_VER_1_2) || defined(ELAN_VER_1_3) 
                                      ,
                                      &(BASE()->shm_key),
                                      BASE()->shm_fifodepth,
                                      BASE()->shm_fragsize
    #endif
                                      );

  gasnetc_queuesz = BASE()->tport_nslots;

  /* setup main queue */
  #ifdef ELAN_VER_1_2
    gasnetc_queue = elan_gallocQueue(BASE()->galloc,GROUP());
  #else
    gasnetc_queue = elan_gallocQueue(BASE(),GROUP());
  #endif
  gasnetc_mainqueue = elan_mainQueueInit(STATE(), gasnetc_queue, gasnetc_queuesz, GASNETC_ELAN_MAX_QUEUEMSG);

  { /* setup buffers */
    gasnetc_bufdesc_t *txdesc = elan_allocMain(STATE(), 8, gasnetc_queuesz*sizeof(gasnetc_bufdesc_t));
    gasnetc_bufdesc_t *rxdesc = elan_allocMain(STATE(), 8, gasnetc_queuesz*sizeof(gasnetc_bufdesc_t));
    int bufsize = ROUNDUP_TO_ALIGN(sizeof(gasnetc_buf_t),64);
    uint8_t *txbuf = elan_allocMain(STATE(), 64, gasnetc_queuesz*bufsize);
    uint8_t *rxbuf = elan_allocMain(STATE(), 64, gasnetc_queuesz*bufsize);
    int i;

    if (!txdesc || !rxdesc || !txbuf || !rxbuf)
      gasneti_fatalerror("Elan-conduit failed to allocate network buffers!");

    /* Tx buffers */
    assert(gasnetc_tportTxFree == NULL);
    assert(gasnetc_tportTxFIFOHead == NULL);
    assert(gasnetc_tportTxFIFOTail == NULL);
    for (i = gasnetc_queuesz-1; i >= 0 ; i--) {
      gasnetc_buf_t *buf = (gasnetc_buf_t *)(txbuf + (i*bufsize));
      txdesc[i].buf = buf;
      txdesc[i].buf_owned = NULL;
      txdesc[i].event = NULL;
      txdesc[i].next = gasnetc_tportTxFree;
      gasnetc_tportTxFree = &txdesc[i];
    }

    /* Rx buffers */
    assert(gasnetc_tportRxFIFOHead == NULL);
    assert(gasnetc_tportRxFIFOTail == NULL);
    for (i = 0; i < gasnetc_queuesz; i++) {
      gasnetc_buf_t *buf = (gasnetc_buf_t *)(rxbuf + (i*bufsize));
      rxdesc[i].buf = buf;
      rxdesc[i].buf_owned = buf;
      rxdesc[i].event = NULL;
      gasnetc_tportAddRxBuf(&rxdesc[i]);
    }

    { /* extra checking */
      int i;
      gasnetc_bufdesc_t *desc = gasnetc_tportRxFIFOHead;
      for (i=0; i < gasnetc_queuesz; i++) {
        assert(desc == &rxdesc[i]);
        assert(desc->event);
        desc = desc->next;
      }
      assert(desc == NULL);
      assert(gasnetc_tportRxFIFOTail == &rxdesc[gasnetc_queuesz-1]);
    }
  }
}
/* ------------------------------------------------------------------------------------ */
static void gasnetc_processPacket(gasnetc_bufdesc_t *desc) {
  gasnetc_buf_t *buf = desc->buf;
  gasnetc_msg_t *msg = &(buf->msg);
  gasnetc_handler_fn_t handler = gasnetc_handler[msg->handlerId];
  gasnetc_category_t category = GASNETC_MSG_CATEGORY(msg);
  int numargs = GASNETC_MSG_NUMARGS(msg);

  desc->replyIssued = 0;
  desc->handlerRunning = 1;
  switch (category) {
    case gasnetc_Short:
      { gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *)(&(buf->msg)+1);
        RUN_HANDLER_SHORT(handler,desc,pargs,numargs);
      }
    break;
    case gasnetc_Medium:
      { gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *)(&(buf->medmsg)+1);
        int nbytes = buf->medmsg.nBytes;
        void *pdata = (pargs + numargs + GASNETC_MEDHEADER_PADARG(numargs));
        RUN_HANDLER_MEDIUM(handler,desc,pargs,numargs,pdata,nbytes);
      }
    break;
    case gasnetc_Long:
      { gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *)(&(buf->longmsg)+1);
        int nbytes = buf->longmsg.nBytes;
        void *pdata = (void *)(buf->longmsg.destLoc);
        RUN_HANDLER_LONG(handler,desc,pargs,numargs,pdata,nbytes);
      }
    break;
    default: abort();
  }
  desc->handlerRunning = 0;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_AMPoll() {
  int i;
  GASNETC_CHECKATTACH();

  for (i = 0; GASNETC_MAX_RECVMSGS_PER_POLL == 0 || i < GASNETC_MAX_RECVMSGS_PER_POLL; i++) {
    gasnetc_bufdesc_t *desc;

    LOCK_ELAN();
    if (elan_queueHaveReq(gasnetc_mainqueue)) {
      char _buf[GASNETC_ELAN_MAX_QUEUEMSG];
      gasnetc_bufdesc_t _desc;
      desc = &_desc;
      desc->buf = (gasnetc_buf_t *)&_buf; 
      assert((void *)&(desc->buf->msg) == (void *)_buf);
      elan_queueWait(gasnetc_mainqueue, _buf, ELAN_POLL_EVENT);
      UNLOCK_ELAN();

      gasnetc_processPacket(desc);

    } else if ((desc = gasnetc_tportCheckRx())) {
      UNLOCK_ELAN();

      gasnetc_processPacket(desc);

      LOCK_ELAN();
        /* set new recv and push on fifo */
        gasnetc_tportAddRxBuf(desc);
      UNLOCK_ELAN();
    } else { /* no more incoming msgs waiting */
      UNLOCK_ELAN();
      break;
    }
  }

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
                         int dest, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {
  char _shortbuf[GASNETC_ELAN_MAX_QUEUEMSG]; 
  gasnetc_bufdesc_t _descbuf; 
  gasnetc_bufdesc_t *desc = NULL;
  gasnetc_buf_t *buf = NULL;
  gasnet_handlerarg_t *pargs;
  int msgsz;
  assert(numargs >= 0 && numargs <= GASNETC_MAX_SHORT);

  switch (category) {
    case gasnetc_Short:
      { desc = &_descbuf;
        buf = (gasnetc_buf_t *)_shortbuf;
        desc->buf = buf;
        pargs = (gasnet_handlerarg_t *)(&(buf->msg)+1);
        msgsz = (uintptr_t)(pargs + numargs) - (uintptr_t)buf;
      }
    break;
    case gasnetc_Medium:
      { uint8_t *pdata;
        int actualargs = numargs + GASNETC_MEDHEADER_PADARG(numargs);
        msgsz = GASNETC_MED_HEADERSZ + (actualargs<<2) + nbytes;
        if (msgsz <= GASNETC_ELAN_MAX_QUEUEMSG) {
          desc = &_descbuf;
          buf = (gasnetc_buf_t *)_shortbuf;
          desc->buf = buf;
        }
        else {
          desc = gasnetc_tportGetTxBuf();
          buf = desc->buf;
        }
        pargs = (gasnet_handlerarg_t *)(&(buf->medmsg)+1);
        pdata = (uint8_t *)(pargs + actualargs);
        memcpy(pdata, source_addr, nbytes);
        buf->medmsg.nBytes = nbytes;
      }
    break;
    case gasnetc_Long:
      { desc = &_descbuf;
        buf = (gasnetc_buf_t *)_shortbuf;        
        desc->buf = buf;
        pargs = (gasnet_handlerarg_t *)(&(buf->longmsg)+1);
        buf->longmsg.nBytes = nbytes;
        buf->longmsg.destLoc = (uintptr_t)dest_ptr;
        msgsz = (uintptr_t)(pargs + numargs) - (uintptr_t)buf;
      }
    break;
    default: abort();
  }
  GASNETC_MSG_SETFLAGS(&(buf->msg), isReq, category, numargs);
  buf->msg.handlerId = handler;
  buf->msg.sourceId = gasnetc_mynode;
  { int i;
    for(i=0; i < numargs; i++) {
      pargs[i] = (gasnet_handlerarg_t)va_arg(argptr, int);
    }
  }

  if (dest == gasnetc_mynode) {
    if (category == gasnetc_Long) memcpy(dest_ptr, source_addr, nbytes);
    gasnetc_processPacket(desc);
    if (desc != &_descbuf) {
      assert(msgsz > GASNETC_ELAN_MAX_QUEUEMSG);
      gasnetc_tportReleaseTxBuf(desc);
    }
  }
  else {
    LOCK_ELAN();
      if (category == gasnetc_Long) {
        /* do put and block for completion */
        ELAN_EVENT *putevt;
        void *bouncebuf = NULL;

        if (nbytes < GASNETC_ELAN_SMALLPUTSZ ||
            elan_addressable(STATE(), source_addr, nbytes)) {
          /* safe to put directly from source */
          putevt = elan_put(STATE(), source_addr, dest_ptr, nbytes, dest);
        } else { /* need to use a bounce buffer */
          /* TODO: would be nice to use SDRAM here, but not sure put interface can handle it... */
          bouncebuf = elan_allocMain(STATE(), 64, nbytes);
          memcpy(bouncebuf, source_addr, nbytes);
          putevt = elan_put(STATE(), bouncebuf, dest_ptr, nbytes, dest);
        }
        /* loop until put is complete (required to ensure ordering semantics) */
        while (!elan_poll(putevt, 5)) {
          UNLOCK_ELAN();
          gasnetc_AMPoll();
          LOCK_ELAN();
        }
        if (bouncebuf) elan_free(STATE(), bouncebuf);
      }

      if (msgsz <= GASNETC_ELAN_MAX_QUEUEMSG) {
        assert(desc == &_descbuf);
        elan_queueReq(gasnetc_mainqueue, dest, &(buf->msg), msgsz);
      }
      else {
        desc->event = elan_tportTxStart(TPORT(), 0, dest, 
                                        gasnetc_mynode, 0, 
                                        &(buf->medmsg), msgsz);
      }
    UNLOCK_ELAN();
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_RequestGeneric(gasnetc_category_t category, 
                         int dest, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {

  gasnetc_AMPoll(); /* ensure progress */

  return gasnetc_ReqRepGeneric(category, 1, dest, handler, 
                               source_addr, nbytes, dest_ptr, 
                               numargs, argptr); 
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_ReplyGeneric(gasnetc_category_t category, 
                         gasnet_token_t token, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {
  gasnetc_bufdesc_t *reqdesc = (gasnetc_bufdesc_t *)token;
  int retval;

  assert(reqdesc->handlerRunning);
  assert(!reqdesc->replyIssued);
  assert(GASNETC_MSG_ISREQUEST(&(reqdesc->buf->msg)));
  
  retval = gasnetc_ReqRepGeneric(category, 0, reqdesc->buf->msg.sourceId, handler, 
                                 source_addr, nbytes, dest_ptr, 
                                 numargs, argptr); 

  reqdesc->replyIssued = 1;
  return retval;
}
/* ------------------------------------------------------------------------------------ */

