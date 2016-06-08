/*   $Source: bitbucket.org:berkeleylab/gasnet.git/other/amudp/amudp_reqrep.cpp $
 * Description: AMUDP Implementations of request/reply operations
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <errno.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include "amudp_internal.h" // must come after any other headers

/* forward decls */
static int AMUDP_RequestGeneric(amudp_category_t category, 
                          ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr,
                          uint8_t systemType, uint8_t systemArg);
static int AMUDP_ReplyGeneric(amudp_category_t category, 
                          amudp_buf_t *requestbuf, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr,
                          uint8_t systemType, uint8_t systemArg);

#if AMUDP_EXTRA_CHECKSUM
  static void AMUDP_SetChecksum(amudp_msg_t *m, size_t len);
  static void AMUDP_ValidateChecksum(amudp_msg_t const *m, size_t len);
#endif

/*------------------------------------------------------------------------------------
 * Private helpers
 *------------------------------------------------------------------------------------ */
static int intpow(int val, int exp) {
  int retval = 1;
  int i;
  AMUDP_assert(exp >= 0);
  for (i = 0; i < exp; i++) retval *= val;
  return retval;
}
/* ------------------------------------------------------------------------------------ */
typedef enum { REQUESTREPLY_PACKET, RETRANSMISSION_PACKET, REFUSAL_PACKET } packet_type;
static int sendPacket(ep_t ep, amudp_msg_t *packet, size_t length, en_t destaddress, packet_type type) {
  AMUDP_assert(ep && packet && length > 0);
  AMUDP_assert(length <= AMUDP_MAX_MSG);
  AMUDP_assert(!enEqual(destaddress, ep->name)); // should never be called for loopback

  #if AMUDP_DEBUG_VERBOSE
    { static int firsttime = 1;
      static int verbosesend = 0;
      if (firsttime) { verbosesend = !!AMUDP_getenv_prefixed("VERBOSE_SEND"); firsttime = 0; }
      if (verbosesend) { 
        char temp[80];
        fprintf(stderr, "sending packet to (%s)\n", AMUDP_enStr(destaddress, temp)); fflush(stderr);
      }
    }
  #endif

  #if AMUDP_EXTRA_CHECKSUM
    AMUDP_SetChecksum(packet, length);
  #endif

  if (sendto(ep->s, (char *)packet, length, /* Solaris requires cast to char* */
             0, (struct sockaddr *)&destaddress, sizeof(en_t)) == SOCKET_ERROR) {
    int err = errno;
    int i = 0;
    while (err == EPERM && i++ < 5) {
       /* Linux intermittently gets EPERM failures here at startup for no apparent reason -
          so allow a retry */
      #if AMUDP_DEBUG_VERBOSE
         AMUDP_Warn("Got a '%s'(%i) on sendto(), retrying...", strerror(err), err); 
      #endif
      sleep(1);
      if (sendto(ep->s, (char *)packet, length,
             0, (struct sockaddr *)&destaddress, sizeof(en_t)) != SOCKET_ERROR) goto success;
      err = errno;
    }
    if (err == ENOBUFS || err == ENOMEM) {
      /* some linuxes also generate ENOBUFS for localhost backpressure - 
         ignore it and treat it as a drop, let retransmisison handle if necessary */
      AMUDP_Warn("Got a '%s'(%i) on sendto(%i), ignoring...", strerror(err), err, (int)length); 
      goto success;
    }
    AMUDP_RETURN_ERRFR(RESOURCE, sendPacket, strerror(errno));
    success: ;
  }

  AMUDP_STATS(ep->stats.TotalBytesSent += length);
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
static int AMUDP_GetOpcode(int isrequest, amudp_category_t cat) {
  switch (cat) {
    case amudp_Short:
      if (isrequest) return AM_REQUEST_M;
      else return AM_REPLY_M;
    case amudp_Medium:
      if (isrequest) return AM_REQUEST_IM;
      else return AM_REPLY_IM;
    case amudp_Long:
      if (isrequest) return AM_REQUEST_XFER_M;
      else return AM_REPLY_XFER_M; 
    default: AMUDP_FatalErr("bad AM category");
      return -1;
  }
}
/* ------------------------------------------------------------------------------------ */
static int sourceAddrToId(ep_t ep, en_t sourceAddr) {
  /*  return source id in ep perproc table of this remote addr, or -1 for not found */
  /*  TODO: make this more efficient */
  for (int i = 0; i < (int)ep->P; i++) {
    if (enEqual(ep->perProcInfo[i].remoteName, sourceAddr))
      return i;
  }
  return -1;
}
/* ------------------------------------------------------------------------------------ */
#define RUN_HANDLER_SHORT(phandlerfn, token, pArgs, numargs) do {                       \
  AMUDP_assert(phandlerfn != NULL);                                                                   \
  if (numargs == 0) (*(AMUDP_HandlerShort)phandlerfn)((void *)token);                   \
  else {                                                                                \
    uint32_t const * const args = (uint32_t *)(pArgs); /* eval only once */             \
    switch (numargs) {                                                                  \
      case 1:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0]); break;         \
      case 2:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1]); break;\
      case 3:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2]); break; \
      case 4:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3]); break; \
      case 5:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4]); break; \
      case 6:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
      case 7:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
      case 8:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
      case 9:  (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
      case 10: (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
      case 11: (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
      case 12: (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
      case 13: (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
      case 14: (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
      case 15: (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
      case 16: (*(AMUDP_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
      default: AMUDP_FatalErr("bad AM arg count");                                                                 \
    }                                                                                   \
  }                                                                                     \
} while (0)
/* ------------------------------------------------------------------------------------ */
#define _RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs, pData, datalen) do {   \
  AMUDP_assert(phandlerfn != NULL);                                                         \
  if (numargs == 0) (*phandlerfn)(token, pData, datalen);                     \
  else {                                                                      \
    uint32_t const * const args = (uint32_t *)(pArgs); /* eval only once */   \
    switch (numargs) {                                                        \
      case 1:  (*phandlerfn)(token, pData, datalen, args[0]); break;         \
      case 2:  (*phandlerfn)(token, pData, datalen, args[0], args[1]); break;\
      case 3:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2]); break; \
      case 4:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3]); break; \
      case 5:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4]); break; \
      case 6:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
      case 7:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
      case 8:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
      case 9:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
      case 10: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
      case 11: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
      case 12: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
      case 13: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
      case 14: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
      case 15: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
      case 16: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
      default: AMUDP_FatalErr("bad AM arg count");                                                                 \
    }                                                                                   \
  }                                                                                     \
} while (0)
#define RUN_HANDLER_MEDIUM(phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    AMUDP_assert(((int)(uintptr_t)pData) % 8 == 0);  /* we guarantee double-word alignment for data payload of medium xfers */ \
    AMUDP_HandlerMedium pfn = (AMUDP_HandlerMedium)phandlerfn; /* temp var to work-around a Clang bug */ \
    _RUN_HANDLER_MEDLONG(pfn, (void *)token, pArgs, numargs, (void *)pData, (int)datalen); \
    } while(0)
#define RUN_HANDLER_LONG(phandlerfn, token, pArgs, numargs, pData, datalen)             \
  _RUN_HANDLER_MEDLONG((AMUDP_HandlerLong)phandlerfn, (void *)token, pArgs, numargs, (void *)pData, (int)datalen)
/* ------------------------------------------------------------------------------------ */
/* ioctl UDP fiasco:
 * According to POSIX, ioctl(I_NREAD) on a SOCK_DGRAM should report the EXACT size of
 * the next message waiting (or 0), not the number of bytes available on the socket. 
 * We can use this as an optimization in choosing the recv buffer size.
 * Linux (FIONREAD) and Solaris (I_NREAD) get this right, 
 * but all other systems seem to get it wrong, one way or another.
 * Cygwin: (bug 3284) not implemented
 * FreeBSD: (bug 2827) returns raw byte count, which can over or under-report
 * others: over-report by returning total bytes in all messages waiting
 */
#ifndef IOCTL_WORKS
 #if PLATFORM_OS_LINUX || PLATFORM_OS_SOLARIS || PLATFORM_OS_DARWIN
  #define IOCTL_WORKS 1
 #else
  #define IOCTL_WORKS 0
 #endif
#endif

/* ------------------------------------------------------------------------------------ */
/*  AMUDP_DrainNetwork - read anything outstanding from hardware/kernel buffers into app space */
static int AMUDP_DrainNetwork(ep_t ep) {
    int totalBytesDrained = 0;
    while (1) {
      IOCTL_FIONREAD_ARG_T bytesAvail = 0;
      #if IOCTL_WORKS
        #if PLATFORM_OS_DARWIN // Apple-specific getsockopt(SO_NREAD) returns what we need
          GETSOCKOPT_LENGTH_T junk = sizeof(bytesAvail);
          if_pf (SOCK_getsockopt(ep->s, SOL_SOCKET, SO_NREAD, &bytesAvail, &junk) == SOCKET_ERROR)
            AMUDP_RETURN_ERRFR(RESOURCE, "getsockopt(SO_NREAD)", strerror(errno));
        #else
          if_pf (SOCK_ioctlsocket(ep->s, _FIONREAD, &bytesAvail) == SOCKET_ERROR)
            AMUDP_RETURN_ERRFR(RESOURCE, "ioctl(FIONREAD)", strerror(errno));
        #endif

        // sanity check
        if_pf ((size_t)bytesAvail > AMUDP_MAX_MSG) {
          char x;
          int retval = recvfrom(ep->s, (char *)&x, 1, MSG_PEEK, NULL, NULL);
          fprintf(stderr, "bytesAvail=%lu  recvfrom(MSG_PEEK)=%i\n", (unsigned long)bytesAvail, retval); fflush(stderr);
          AMUDP_RETURN_ERRFR(RESOURCE, "AMUDP_DrainNetwork: received message that was too long", strerror(errno));
        }
      #else
        if (inputWaiting(ep->s, false)) bytesAvail = AMUDP_MAX_MSG;
      #endif
      if (bytesAvail == 0) break; 

        /* TODO: another possible workaround for !IOCTL_WORKS:
         * Use a MSG_PEEK of the header to retrieve the header and GET_MSG_SZ
         * to allocate an exact-sized buffer. 
         * Probably not worth the overhead for a short-lived Rx buffer, 
         * especially since some OSs will buffer overrun on MSG_PEEK of a partial datagram.
         * However this same strategy could be used (possibly on a dedicated socket) on any OS
         * to scatter-recv AMLong payloads directly into their final destination, saving a copy.
         */

      /* something waiting, acquire a buffer for it */
      size_t const msgsz = bytesAvail;
      if (ep->rxCnt >= ep->recvDepth) { /* out of buffers - postpone draining */
        #if AMUDP_DEBUG
          AMUDP_Warn("Receive buffer full - unable to drain network. Consider raising RECVDEPTH or polling more often.");
        #endif
        break;
      }
      amudp_buf_t *destbuf = AMUDP_AcquireBuffer(ep, MSGSZ_TO_BUFFERSZ(msgsz));

      #if AMUDP_EXTRA_CHECKSUM && AMUDP_DEBUG
        memset((char *)&destbuf->msg, 0xCC, msgsz); // init recv buffer to a known value
      #endif

      /* perform the receive */
      struct sockaddr sa;
      int sz = sizeof(en_t);
      int retval = myrecvfrom(ep->s, (char *)&destbuf->msg, msgsz, 0, &sa, &sz);

      #if IOCTL_WORKS
        if_pt (retval == (int)msgsz) ; // success
      #else
        if_pt (retval <= (int)msgsz) ; // success
      #endif
        else if_pf (retval == SOCKET_ERROR)
          AMUDP_RETURN_ERRFR(RESOURCE, "AMUDP_DrainNetwork: recvfrom()", strerror(errno));
        else if_pf (retval == 0)
          AMUDP_RETURN_ERRFR(RESOURCE, "AMUDP_DrainNetwork: recvfrom() returned zero", strerror(errno));
        else if_pf ((size_t)retval < AMUDP_MIN_MSG) 
          AMUDP_RETURN_ERRFR(RESOURCE, "AMUDP_DrainNetwork: incomplete message received in recvfrom()", strerror(errno));
        else if_pf ((size_t)retval > msgsz) 
            AMUDP_RETURN_ERRFR(RESOURCE, "AMUDP_DrainNetwork: buffer overrun in recvfrom()", strerror(errno));
        else { /* detect broken ioctl implementations */
          AMUDP_assert(IOCTL_WORKS && retval != (int)bytesAvail);
          AMUDP_Warn("ioctl() is probably broken: bytesAvail=%i  recvfrom returned=%i", (int)bytesAvail, retval);
        }
      #if AMUDP_DEBUG
        if_pf (sz != sizeof(en_t)) // should never happen
          AMUDP_RETURN_ERRFR(RESOURCE, "AMUDP_DrainNetwork: recvfrom() returned wrong sockaddr size", strerror(errno));
      #endif

      #if AMUDP_EXTRA_CHECKSUM
        // the following lines can be uncommented to inject errors and verify the checksum support is working
        //memset(((char*)destbuf)+retval-8, 0, 8);
        //destbuf->msg.chk2 = 4;
        //destbuf->msg.packetlen = 4;
        AMUDP_ValidateChecksum(&(destbuf->msg), retval);
      #endif

      destbuf->status.rx.sourceAddr = *(en_t *)&sa;

      // add it to the recv queue
      destbuf->status.rx.next = NULL;
      if (!ep->rxCnt) { // first element
        AMUDP_assert(!ep->rxHead && !ep->rxTail);
        ep->rxTail = ep->rxHead = destbuf;
      } else { // append to FIFO
        AMUDP_assert(ep->rxHead && ep->rxTail);
        AMUDP_assert(ep->rxHead != ep->rxTail || ep->rxCnt == 1);
        ep->rxTail->status.rx.next = destbuf;
        ep->rxTail = destbuf;
      }
      ep->rxCnt++;

      totalBytesDrained += retval;
    } // drain recv loop

    #if USE_SOCKET_RECVBUFFER_GROW
      /* heuristically decide whether we should expand the OS socket recv buffers */
      if (totalBytesDrained + AMUDP_MAX_MSG > ep->socketRecvBufferSize) {
        /* it's possible we dropped something due to insufficient OS socket buffer space */
        if (!ep->socketRecvBufferMaxedOut) { /* try to do something about it */
          /* TODO: we may want to add some hysterisis here to prevent artifical inflation
           * due to retransmits after a long period of no polling 
           */
          const int sanitymax = AMUDP_SOCKETBUFFER_MAX;
          int newsize = 2 * ep->socketRecvBufferSize;

          if (newsize > sanitymax) { /* create a semi-sane upper bound */
            AMUDP_growSocketBufferSize(ep, sanitymax, SO_RCVBUF, "SO_RCVBUF");
            ep->socketRecvBufferMaxedOut = 1;
          } else { 
            ep->socketRecvBufferMaxedOut = AMUDP_growSocketBufferSize(ep, newsize, SO_RCVBUF, "SO_RCVBUF");
          }
        }
      }
    #endif
    return AM_OK; /* done */
}
static int AMUDP_WaitForEndpointActivity(eb_t eb, struct timeval *tv) {
    /* drain network and block up to tv time for endpoint recv buffers to become non-empty (NULL to block)
     * return AM_OK for activity, AM_ERR_ for other error, -1 for timeout 
     * wakeupOnControlActivity controls whether we return on control socket activity (for blocking)
     */

    {/* drain network and see if some receive buffer already non-empty */
      int i;
      for (i = 0; i < eb->n_endpoints; i++) {
        ep_t ep = eb->endpoints[i];
        int retval = AMUDP_DrainNetwork(ep);
        if (retval != AM_OK) AMUDP_RETURN(retval);
      }
      for (i = 0; i < eb->n_endpoints; i++) {
        ep_t ep = eb->endpoints[i];
        if (ep->rxCnt) return AM_OK;
      }
    }

    while (1) {
      fd_set sockset;
      fd_set* psockset = &sockset;
      int maxfd = 0;

      FD_ZERO(psockset);
      for (int i = 0; i < eb->n_endpoints; i++) {
        SOCKET s = eb->endpoints[i]->s;
        FD_SET(s, psockset);
        if ((int)s > maxfd) maxfd = s;
      }
      if (AMUDP_SPMDControlSocket != INVALID_SOCKET) {
        ASYNC_TCP_DISABLE();
        FD_SET(AMUDP_SPMDControlSocket, psockset);
        if ((int)AMUDP_SPMDControlSocket > maxfd) maxfd = AMUDP_SPMDControlSocket;
      }
      /* wait for activity */
      amudp_cputick_t starttime = getCPUTicks();
      { int retval = select(maxfd+1, psockset, NULL, NULL, tv);
        if (AMUDP_SPMDControlSocket != INVALID_SOCKET) ASYNC_TCP_ENABLE();
        if_pf (retval == SOCKET_ERROR) { 
          AMUDP_RETURN_ERRFR(RESOURCE, "AMUDP_Block: select()", strerror(errno));
        }
        else if (retval == 0) return -1; /* time limit expired */
      }
      if (FD_ISSET(AMUDP_SPMDControlSocket, psockset)) {
        AMUDP_SPMDIsActiveControlSocket = TRUE; /* we may have missed a signal */
        AMUDP_SPMDHandleControlTraffic(NULL);
        if (AMUDP_SPMDwakeupOnControlActivity) break;
      }
      else break; /* activity on some endpoint in bundle */
      amudp_cputick_t endtime = getCPUTicks();

      if (tv) { /* readjust remaining time */
        int64_t elapsedtime = ticks2us(endtime - starttime);
        if (elapsedtime < tv->tv_usec) tv->tv_usec -= elapsedtime;
        else {
          int64_t remainingtime = ((int64_t)tv->tv_sec) * 1000000 + tv->tv_usec;
          remainingtime -= elapsedtime;
          if (remainingtime <= 0) return -1; /* time limit expired */
          tv->tv_sec = (long)(remainingtime / 1000000);
          tv->tv_usec = (long)(remainingtime % 1000000);
        }
      }
    }

    return AM_OK; /* some endpoint activity is waiting */
}
/* ------------------------------------------------------------------------------------ */
// Manage the doubly-linked tx ring
static void AMUDP_EnqueueTxBuffer(ep_t ep, amudp_buf_t *buf) {
  if (!ep->timeoutCheckPosn) { // empty ring
    AMUDP_assert(ep->outstandingRequests == 0);
    ep->timeoutCheckPosn = buf;
    buf->status.tx.next = buf;
    buf->status.tx.prev = buf;
    ep->outstandingRequests = 1;
  } else { // insert "behind" current check posn
    AMUDP_assert(ep->outstandingRequests >= 1);
    buf->status.tx.next = ep->timeoutCheckPosn;
    buf->status.tx.prev = ep->timeoutCheckPosn->status.tx.prev;
    ep->timeoutCheckPosn->status.tx.prev = buf;
    buf->status.tx.prev->status.tx.next = buf;
    ep->outstandingRequests++;
  }
}
static void AMUDP_DequeueTxBuffer(ep_t ep, amudp_buf_t *buf) {
  AMUDP_assert(buf->status.tx.next);
  AMUDP_assert(buf->status.tx.prev);
  AMUDP_assert(ep->timeoutCheckPosn);
  if (buf->status.tx.next == buf) { // removing last element
    AMUDP_assert(ep->outstandingRequests == 1);
    AMUDP_assert(buf->status.tx.prev == buf);
    AMUDP_assert(ep->timeoutCheckPosn == buf);
    ep->timeoutCheckPosn = NULL;
    ep->outstandingRequests = 0;
  } else { // extract from ring
    AMUDP_assert(ep->outstandingRequests > 1);
    if (ep->timeoutCheckPosn == buf) // advance posn
      ep->timeoutCheckPosn = buf->status.tx.next;
    buf->status.tx.prev->status.tx.next = buf->status.tx.next;
    buf->status.tx.next->status.tx.prev = buf->status.tx.prev;
    ep->outstandingRequests--;
  }
  #if AMUDP_DEBUG
    buf->status.tx.next = NULL;
    buf->status.tx.prev = NULL;
  #endif
}
/* ------------------------------------------------------------------------------------ */
static int AMUDP_HandleRequestTimeouts(ep_t ep, int numtocheck) {
  /* check the next numtocheck requests for timeout (or -1 for all)
   * and retransmit as necessary. return AM_OK or AM_ERR_XXX
   */
  amudp_buf_t *buf = ep->timeoutCheckPosn;

  if (!buf) { // tx ring empty
    AMUDP_assert(ep->outstandingRequests == 0);
    return AM_OK; 
  }

  amudp_cputick_t now = getCPUTicks();

  AMUDP_assert(ep->outstandingRequests > 0);
  AMUDP_assert(ep->outstandingRequests <= ep->PD); // sanity: weak test b/c ignores loopback
  if (numtocheck == -1) numtocheck = ep->outstandingRequests;
  else numtocheck = MIN(numtocheck, ep->outstandingRequests);
  for (int i = 0; i < numtocheck; i++) {
    if_pf (buf->status.tx.timestamp <= now && 
           AMUDP_InitialRequestTimeout_us != AMUDP_TIMEOUT_INFINITE) {

      static amudp_cputick_t initial_requesttimeout_cputicks = 0;
      static int max_retryCount = 0;
      static int firsttime = 1;
      if_pf (firsttime) { // init precomputed values
        if (AMUDP_MaxRequestTimeout_us == AMUDP_TIMEOUT_INFINITE) {
          max_retryCount = 0;
        } else {
          uint32_t temp = AMUDP_InitialRequestTimeout_us;
          while (temp <= AMUDP_MaxRequestTimeout_us) {
            temp *= AMUDP_RequestTimeoutBackoff;
            max_retryCount++;
          }
        }
        initial_requesttimeout_cputicks = us2ticks(AMUDP_InitialRequestTimeout_us);
        firsttime = 0;
      }

      amudp_msg_t * const msg = &buf->msg;
      amudp_category_t const cat = AMUDP_MSG_CATEGORY(msg);
      AMUDP_assert(AMUDP_MSG_ISREQUEST(msg));
      amudp_node_t const destP = buf->status.tx.destId;

      if_pf (buf->status.tx.retryCount >= max_retryCount && max_retryCount) {
        /* we already waited too long - request is undeliverable */
        AMUDP_HandlerReturned handlerfn = (AMUDP_HandlerReturned)ep->handler[0];
        int opcode = AMUDP_GetOpcode(1, cat);

        AMUDP_DequeueTxBuffer(ep, buf);

        /* pretend this is a bounced recv buffer */
        /* note that source/dest for returned mesgs reflect the virtual "message denied" packet 
         * although it doesn't really matter because the AM2 spec is too vague
         * about the argblock returned message argument for it to be of any use to anyone
         */
        buf->status.rx.sourceId = destP; 
        buf->status.rx.sourceAddr = ep->perProcInfo[destP].remoteName;
        buf->status.rx.dest = ep;

        buf->status.rx.replyIssued = TRUE; /* prevent any reply */
        buf->status.rx.handlerRunning = TRUE;
        AMUDP_assert(handlerfn != NULL);
        (*handlerfn)(ECONGESTION, opcode, (void *)buf);
        buf->status.rx.handlerRunning = FALSE;

        AMUDP_ReleaseBuffer(ep, buf);
        AMUDP_STATS(ep->stats.ReturnedMessages++);
      } else {
        buf->status.tx.retryCount++;
      
        /* retransmit */
        size_t msgsz = GET_MSG_SZ(msg);
        en_t destaddress = ep->perProcInfo[destP].remoteName;
        /* tag should NOT be changed for retransmit */
        #if AMUDP_DEBUG_VERBOSE
          AMUDP_Warn("Retransmitting a request...");
        #endif
        int retval = sendPacket(ep, msg, msgsz, destaddress, RETRANSMISSION_PACKET);
        if (retval != AM_OK) AMUDP_RETURN(retval);        
        buf->status.tx.transmitCount++;
        AMUDP_STATS(ep->stats.RequestsRetransmitted[cat]++);
        AMUDP_STATS(ep->stats.RequestTotalBytesSent[cat] += msgsz);

        now = getCPUTicks(); // may have blocked in send
        amudp_cputick_t timetowait = initial_requesttimeout_cputicks * 
           intpow(AMUDP_RequestTimeoutBackoff, buf->status.tx.retryCount);
        buf->status.tx.timestamp = now + timetowait;
      }
    } // time expired

    buf = buf->status.tx.next; // advance
    AMUDP_assert(buf);
  }
  
  /* advance checked posn */
  ep->timeoutCheckPosn = buf;

  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
#define MAXINT64    ((((uint64_t)1) << 63) - 1)
static amudp_cputick_t AMUDP_FindEarliestRequestTimeout(eb_t eb) {
  /* return the soonest timeout value for an active request
   * (which may have already passed)
   * return 0 for no outstanding requests
   */
  amudp_cputick_t earliesttime = (amudp_cputick_t)MAXINT64;
  for (int i = 0; i < eb->n_endpoints; i++) {
    ep_t ep = eb->endpoints[i];
    amudp_buf_t * const startpos = ep->timeoutCheckPosn;
    if (!startpos) continue;
    amudp_buf_t *buf = startpos;
    do { 
      amudp_cputick_t timestamp = buf->status.tx.timestamp;
      if (timestamp < earliesttime) earliesttime = timestamp;
      buf = buf->status.tx.next;
    } while (buf != startpos);
  }
  if (earliesttime == MAXINT64) return 0;
  else return earliesttime;
}
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_Block(eb_t eb) {
  /* block until some endpoint receive buffer becomes non-empty
   * does not poll, but does handle SPMD control socket events
   */

  /* first, quickly determine if something is already waiting */
  { struct timeval tv = {0,0};
    int retval = AMUDP_WaitForEndpointActivity(eb, &tv);
    if (retval != -1) AMUDP_RETURN(retval); /* error or something waiting */
  }

  while (1) {
    /* we need to be careful we don't sleep longer than the next packet timeout */
    amudp_cputick_t nexttimeout = AMUDP_FindEarliestRequestTimeout(eb);
    int retval;
    if (nexttimeout) {
      struct timeval tv;
      amudp_cputick_t now = getCPUTicks();
      if (nexttimeout < now) goto timeout; /* already have a request timeout */
      uint32_t const uspause = (uint32_t)ticks2us(nexttimeout - now);
      tv.tv_sec = (long)(uspause / 1000000);
      tv.tv_usec = (long)(uspause % 1000000);
      retval = AMUDP_WaitForEndpointActivity(eb, &tv);
    } else /* no outstanding requests, so just block */
      retval = AMUDP_WaitForEndpointActivity(eb, NULL); 
    if (retval != -1) AMUDP_RETURN(retval); /* error or something waiting */
     
    /* some request has timed out - handle it */
    timeout:
    { int i;
      for (i = 0; i < eb->n_endpoints; i++) {
        ep_t ep = eb->endpoints[i];
        if (ep->depth != -1) {
          int retval = AMUDP_HandleRequestTimeouts(ep, -1);
          if (retval != AM_OK) AMUDP_RETURN(retval);
        }
      }
    }
  }

}
/* ------------------------------------------------------------------------------------ */
#if AMUDP_DEBUG
  #define REFUSE_NOTICE(reason) AMUDP_Err("I just refused a message and returned to sender. Reason: %s", reason)
#else
  #define REFUSE_NOTICE(reason) (void)0
#endif

/* this is a local-use-only macro for AMUDP_processPacket */
#define AMUDP_REFUSEMESSAGE(errcode) do {                                       \
    msg->systemMessageType = (uint8_t)amudp_system_returnedmessage;             \
    msg->systemMessageArg = (uint8_t)errcode;                                   \
    if (isloopback) {                                                           \
      AMUDP_processPacket(buf, 1);                                              \
    } else {                                                                    \
      int retval = sendPacket(ep, msg, GET_MSG_SZ(msg),                         \
                        buf->status.rx.sourceAddr, REFUSAL_PACKET);             \
       /* ignore errors sending this */                                         \
      if (retval != AM_OK) AMUDP_Err("failed to sendPacket to refuse message"); \
      else REFUSE_NOTICE(#errcode);                                             \
    }                                                                           \
    return;                                                                     \
  } while(0)

// Process an incoming buffer from any source, and return when complete
// Does NOT release the buffer
void AMUDP_processPacket(amudp_buf_t * const buf, int isloopback) {
  amudp_msg_t * const msg = &buf->msg;
  ep_t const ep = buf->status.rx.dest;
  int const sourceID = buf->status.rx.sourceId;
  int const numargs = AMUDP_MSG_NUMARGS(msg);
  uint8_t const seqnum = AMUDP_MSG_SEQNUM(msg);
  uint16_t const instance = AMUDP_MSG_INSTANCE(msg);
  int const isrequest = AMUDP_MSG_ISREQUEST(msg);
  amudp_category_t const cat = AMUDP_MSG_CATEGORY(msg);
  int const issystemmsg = ((amudp_system_messagetype_t)msg->systemMessageType) != amudp_system_user;

  /* handle returned messages */
  if_pf (issystemmsg) { 
    amudp_system_messagetype_t type = ((amudp_system_messagetype_t)msg->systemMessageType);
    if (type == amudp_system_returnedmessage) { 
      AMUDP_HandlerReturned handlerfn = (AMUDP_HandlerReturned)ep->handler[0];
      op_t opcode;
      if (sourceID < 0) return; /*  unknown source, ignore message */
      if (isrequest && !isloopback) { /*  the returned message is a request, so free that request buffer */
        amudp_bufdesc_t * const desc = GET_REQ_DESC(ep, sourceID, instance);
        amudp_buf_t *reqbuf = desc->buffer;
        if (desc->buffer && desc->seqNum == seqnum) {
          AMUDP_DequeueTxBuffer(ep, desc->buffer);
          AMUDP_ReleaseBuffer(ep, desc->buffer);
          desc->buffer = NULL;
          desc->seqNum = AMUDP_SEQNUM_INC(desc->seqNum);
          ep->perProcInfo[sourceID].instanceHint = instance;
        }
      }
      opcode = AMUDP_GetOpcode(isrequest, cat);

      /* note that source/dest for returned mesgs reflect the virtual "message denied" packet 
       * although it doesn't really matter because the AM2 spec is too vague
       * about the argblock returned message argument for it to be of any use to anyone
       */
      buf->status.rx.replyIssued = TRUE; /* prevent any reply */
      buf->status.rx.handlerRunning = TRUE;
        AMUDP_assert(handlerfn != NULL);
        (*handlerfn)(msg->systemMessageArg, opcode, (void *)buf);
      buf->status.rx.handlerRunning = FALSE;
      AMUDP_STATS(ep->stats.ReturnedMessages++);
      return;
    }
  }

  if (!isloopback) {
    if (isrequest) AMUDP_STATS(ep->stats.RequestsReceived[cat]++);
    else AMUDP_STATS(ep->stats.RepliesReceived[cat]++);
  }

  /* perform acceptance checks */

  if_pf (ep->tag == AM_NONE || 
     (ep->tag != msg->tag && ep->tag != AM_ALL))
      AMUDP_REFUSEMESSAGE(EBADTAG);
  if_pf (instance >= ep->depth)
      AMUDP_REFUSEMESSAGE(EUNREACHABLE);
  if_pf (ep->handler[msg->handlerId] == amudp_unused_handler &&
      !issystemmsg && msg->handlerId != 0)
      AMUDP_REFUSEMESSAGE(EBADHANDLER);

  switch (cat) {
    case amudp_Short:
      if_pf (msg->nBytes > 0 || msg->destOffset > 0)
        AMUDP_REFUSEMESSAGE(EBADLENGTH);
      break;
    case amudp_Medium:
      if_pf (msg->nBytes > AMUDP_MAX_MEDIUM || msg->destOffset > 0)
        AMUDP_REFUSEMESSAGE(EBADLENGTH);
      break;
    case amudp_Long: 
      /* check segment limits */
      if_pf (msg->nBytes > AMUDP_MAX_LONG)
        AMUDP_REFUSEMESSAGE(EBADLENGTH);
      if_pf ( ep->segLength == 0 || /* empty seg */
              ((uintptr_t)ep->segAddr + msg->destOffset) == 0) /* NULL target */
        AMUDP_REFUSEMESSAGE(EBADSEGOFF);
      if_pf (msg->destOffset + msg->nBytes > ep->segLength)
        AMUDP_REFUSEMESSAGE(EBADLENGTH);
      break;
    default: AMUDP_FatalErr("bad AM category");
  }

  /*  check the source id */
  if_pf (sourceID < 0) AMUDP_REFUSEMESSAGE(EBADENDPOINT);

  // fetch the descriptor relevant to this network message
  amudp_bufdesc_t * const desc = (isloopback ? NULL :
                       AMUDP_get_desc(ep, sourceID, instance, 
                                      !isrequest,  // the alternate descriptor is the relevant one
                                      isrequest)); // should only need to allocate if this is a request

  if (!isloopback) {
    static const char *OOOwarn = "Detected arrival of out-of-order %s!\n"
      " It appears your system is delivering IP packets out-of-order between worker nodes,\n"
      " most likely due to striping over multiple adapters or links.\n"
      " This might (rarely) lead to corruption of AMUDP traffic.";
    /* check sequence number to see if this is a new request/reply or a duplicate */
    if (isrequest) {
      if_pf (seqnum != desc->seqNum) { 
        if_pf (AMUDP_SEQNUM_INC(seqnum) != desc->seqNum) {
          AMUDP_STATS(ep->stats.OutOfOrderRequests++);
          if (OOOwarn) {
            AMUDP_Warn(OOOwarn, "request");
            OOOwarn = NULL;
          }
        }
        /* request resent or reply got dropped - resend reply */
        amudp_buf_t * const replybuf = desc->buffer;
        AMUDP_assert(replybuf);
        amudp_msg_t * const replymsg = &replybuf->msg;

        size_t msgsz = GET_MSG_SZ(replymsg);
        #if AMUDP_DEBUG_VERBOSE
          AMUDP_Warn("Got a duplicate request - resending previous reply.");
        #endif
        int retval = sendPacket(ep, replymsg, msgsz,
            ep->perProcInfo[sourceID].remoteName, RETRANSMISSION_PACKET);
        if (retval != AM_OK) AMUDP_Err("sendPacket failed while resending a reply");
        replybuf->status.tx.transmitCount++;
        int cat = AMUDP_MSG_CATEGORY(replymsg);
        AMUDP_STATS(ep->stats.RepliesRetransmitted[cat]++);
        AMUDP_STATS(ep->stats.ReplyTotalBytesSent[cat] += msgsz);
        return;
      }
    } else {
      if (seqnum != desc->seqNum) { /*  duplicate reply, we already ran handler - ignore it */
        if_pf (AMUDP_SEQNUM_INC(seqnum) != desc->seqNum) {
          AMUDP_STATS(ep->stats.OutOfOrderReplies++);
          if (OOOwarn) {
            AMUDP_Warn(OOOwarn, "reply");
            OOOwarn = NULL;
          }
        }
        #if AMUDP_DEBUG_VERBOSE
          AMUDP_Warn("Ignoring a duplicate reply.");
        #endif
        return;
      }
    }

    /* --- message accepted --- */

    if (isrequest) { //  alternate the reply sequence number so duplicates of this request get ignored
        desc->seqNum = AMUDP_SEQNUM_INC(desc->seqNum);
    } else { /* it's a reply, free the corresponding request */
      amudp_buf_t * const reqbuf = desc->buffer;
      if_pt (reqbuf) { 
        #if AMUDP_COLLECT_LATENCY_STATS && AMUDP_COLLECT_STATS
          { /* gather some latency statistics */
            amudp_cputick_t now = getCPUTicks();
            amudp_cputick_t latency = (now - reqbuf->status.tx.firstSendTime);
            ep->stats.RequestSumLatency += latency;
            if (latency < ep->stats.RequestMinLatency) ep->stats.RequestMinLatency = latency;
            if (latency > ep->stats.RequestMaxLatency) ep->stats.RequestMaxLatency = latency;
          }
        #endif
        AMUDP_DequeueTxBuffer(ep, reqbuf);
        AMUDP_ReleaseBuffer(ep, reqbuf);
        desc->buffer = NULL;
        desc->seqNum = AMUDP_SEQNUM_INC(desc->seqNum);
        ep->perProcInfo[sourceID].instanceHint = instance;
      } else { /* request timed out and we decided it was undeliverable, then a reply arrived */
        desc->seqNum = AMUDP_SEQNUM_INC(desc->seqNum);
        /* TODO: seq numbers may get out of sync on timeout 
         * if request got through but replies got lost 
         * we also may do bad things if a reply to an undeliverable message 
         * arrives after we've reused the request buffer (very unlikely)
         * possible soln: add an epoch number
         */
        return; /* reply handler should NOT be run in this situation */
      }
    }
  }

  { /*  run the handler */
    buf->status.rx.replyIssued = FALSE;
    buf->status.rx.handlerRunning = TRUE;
    if (issystemmsg) { /* an AMUDP system message */
      amudp_system_messagetype_t type = ((amudp_system_messagetype_t)(msg->systemMessageType & 0xF));
      switch (type) {
        case amudp_system_autoreply:
          AMUDP_assert(!isloopback);
          /*  do nothing, already taken care of */
          break;
        default: AMUDP_FatalErr("bad AM type");
      }
    } else { /* a user message */
      uint32_t * const pargs = GET_MSG_ARGS(msg);
      amudp_handler_fn_t const phandler = ep->handler[msg->handlerId];
      switch (cat) {
        case amudp_Short: 
          if (ep->preHandlerCallback) 
            ep->preHandlerCallback(amudp_Short, isrequest, msg->handlerId, buf, 
                                   NULL, 0, numargs, pargs);
          RUN_HANDLER_SHORT(phandler, buf, pargs, numargs);
          if (ep->postHandlerCallback) ep->postHandlerCallback(cat, isrequest);
          break;
        case amudp_Medium: {
          uint8_t * const pData = GET_MSG_DATA(msg);
          if (ep->preHandlerCallback) 
            ep->preHandlerCallback(amudp_Medium, isrequest, msg->handlerId, buf, 
                                   pData, msg->nBytes, numargs, pargs);
          RUN_HANDLER_MEDIUM(phandler, buf, pargs, numargs, pData, msg->nBytes);
          if (ep->postHandlerCallback) ep->postHandlerCallback(cat, isrequest);
          break;
        }
        case amudp_Long: {
          uint8_t * const pData = ((uint8_t *)ep->segAddr) + msg->destOffset;
          /*  a single-message bulk transfer. do the copy */
          if (!isloopback) memcpy(pData, GET_MSG_DATA(msg), msg->nBytes);
          if (ep->preHandlerCallback) 
            ep->preHandlerCallback(amudp_Long, isrequest, msg->handlerId, buf, 
                                   pData, msg->nBytes, numargs, pargs);
          RUN_HANDLER_LONG(phandler, buf, pargs, numargs, pData, msg->nBytes);
          if (ep->postHandlerCallback) ep->postHandlerCallback(cat, isrequest);
          break;
        }
        default: AMUDP_FatalErr("bad AM category");
      }
    }
    buf->status.rx.handlerRunning = FALSE;
    if (!isloopback) {
      if (isrequest && !buf->status.rx.replyIssued) {
        static va_list va_dummy; /* dummy value - static to prevent uninit warnings */
        /*  user didn't reply, so issue an auto-reply */
        if_pf (AMUDP_ReplyGeneric(amudp_Short, buf, 0, 0, 0, 0, 0, va_dummy, amudp_system_autoreply, 0) 
            != AM_OK) /*  should never happen - don't return here to prevent leaking buffer */
          AMUDP_Err("Failed to issue auto reply in AMUDP_ServiceIncomingMessages");
      }
    }
  }
}
#undef AMUDP_REFUSEMESSAGE  /* this is a local-use-only macro */
/* ------------------------------------------------------------------------------------ */
/* main message receive workhorse - 
 * drain network once and service available incoming messages, up to AMUDP_MAX_RECVMSGS_PER_POLL
 */
static int AMUDP_ServiceIncomingMessages(ep_t ep) {
  /* drain network */
  int retval = AMUDP_DrainNetwork(ep);
  if (retval != AM_OK) AMUDP_RETURN(retval);

  for (int i = 0; AMUDP_MAX_RECVMSGS_PER_POLL == 0 || i < MAX(AMUDP_MAX_RECVMSGS_PER_POLL, ep->depth); i++) {
      amudp_buf_t * const buf = ep->rxHead;

      if (!buf) return AM_OK; /* nothing else waiting */

      /* we have a real message waiting - dequeue it */
      ep->rxHead = buf->status.rx.next;
      AMUDP_assert(ep->rxCnt > 0);
      ep->rxCnt--;
      if (ep->rxCnt == 0) {
        AMUDP_assert(!ep->rxHead);
        ep->rxTail = NULL;
      }

      buf->status.rx.dest = ep; /* remember which ep recvd this message */
      buf->status.rx.sourceId = (amudp_node_t)sourceAddrToId(ep, buf->status.rx.sourceAddr);

      if (AMUDP_FaultInjectionEnabled) { /* allow fault injection to drop some revcd messages */
        double randval = rand() / (double)RAND_MAX;
        AMUDP_assert(randval >= 0.0 && AMUDP_FaultInjectionRate >= 0.0);
        if (randval < AMUDP_FaultInjectionRate) {
          #if AMUDP_DEBUG_VERBOSE
            fprintf(stderr, "fault injection dropping a packet..\n"); fflush(stderr);
          #endif
          goto donewithmessage;
        }
      }
  
      AMUDP_processPacket(buf, 0);
      donewithmessage: /* message handled - continue to next one */

      /* free the handled buffer */
      AMUDP_ReleaseBuffer(ep, buf);

  }  /*  for */
  return AM_OK;
} /*  AMUDP_ServiceIncomingMessages */
/*------------------------------------------------------------------------------------
 * Poll
 *------------------------------------------------------------------------------------ */
extern int AM_Poll(eb_t eb) {
  AMUDP_CHECKINIT();
  AMUDP_CHECK_ERR(!eb, BAD_ARG);

  for (int i = 0; i < eb->n_endpoints; i++) {
    int retval;
    ep_t ep = eb->endpoints[i];

    if_pt (ep->depth != -1) { /* only poll endpoints which have buffers */

      #if USE_ASYNC_TCP_CONTROL
        if_pf (AMUDP_SPMDIsActiveControlSocket) /*  async check */
      #endif
      { retval = AMUDP_SPMDHandleControlTraffic(NULL);
        if_pf (retval != AM_OK) AMUDP_RETURN(retval);
      }

      retval = AMUDP_ServiceIncomingMessages(ep); /* drain network and check for activity */
      if_pf (retval != AM_OK) AMUDP_RETURN(retval);

      retval = AMUDP_HandleRequestTimeouts(ep, AMUDP_TIMEOUTS_CHECKED_EACH_POLL);
      if_pf (retval != AM_OK) AMUDP_RETURN(retval);
    }
  }

  return AM_OK;
}
/*------------------------------------------------------------------------------------
 * Generic Request/Reply
 *------------------------------------------------------------------------------------ */
static int AMUDP_RequestGeneric(amudp_category_t category, 
                          ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr, 
                          uint8_t systemType, uint8_t systemArg) {

  amudp_translation_t const * const trans = &request_endpoint->translation[reply_endpoint];
  amudp_node_t const destP = trans->id;
  en_t const destaddress = trans->name;
  const int isloopback = enEqual(destaddress, request_endpoint->name);

  uint16_t instance;
  amudp_perproc_info_t *perProcInfo;
  amudp_bufdesc_t *outgoingdesc = NULL;

  /*  always poll before sending a request */
  int retval = AM_Poll(request_endpoint->eb);
  if_pf (retval != AM_OK) AMUDP_RETURN(retval);

  size_t const msgsz = COMPUTE_MSG_SZ(numargs, nbytes);
  size_t const buffersz = MSGSZ_TO_BUFFERSZ(msgsz);
  amudp_buf_t * const outgoingbuf = AMUDP_AcquireBuffer(request_endpoint, buffersz);

  if (isloopback) {
    #if AMUDP_DEBUG
      instance = 0; /* not used */
      perProcInfo = NULL;
    #endif
  } else { /*  acquire a free request buffer */
    int const depth = request_endpoint->depth;
    perProcInfo = &request_endpoint->perProcInfo[destP];

    while(1) { // send resource acquisition loop
      uint16_t const hint = perProcInfo->instanceHint;
      AMUDP_assert(hint <= depth);
      amudp_bufdesc_t * const descs = GET_REQ_DESC_ALLOC(request_endpoint, destP, 0);
      amudp_bufdesc_t * const hintdesc = &descs[hint];

      if_pt (!hintdesc->buffer) { /*  hint is right */
        instance = hint;
        outgoingdesc = hintdesc;
        perProcInfo->instanceHint = (hint+1==depth?0:hint+1);
        goto gotinstance;
      } else { /*  hint is wrong */
        /*  search for a free instance */
        instance = hint; 
        do {
          instance = ((instance+1)==depth?0:instance+1);
          amudp_bufdesc_t * const tdesc = &descs[hint];
          if (!tdesc->buffer) {
            outgoingdesc = tdesc;
            goto gotinstance;
          }
        } while (instance != hint);

        /*  no buffers available - wait until one is open 
         *  (hint will point to a free buffer) 
         */
        do {
          int retval = AM_OK;
          if (AMUDP_PoliteSync) {
            retval = AMUDP_Block(request_endpoint->eb);
          }
          if_pt (retval == AM_OK) retval = AM_Poll(request_endpoint->eb);
          if_pf (retval != AM_OK) {
            AMUDP_ReleaseBuffer(request_endpoint, outgoingbuf); // prevent leak
            AMUDP_RETURN(retval);
          }
        } while (descs[perProcInfo->instanceHint].buffer);
      }
    } 

  gotinstance:
    AMUDP_assert(outgoingdesc);
    AMUDP_assert(!outgoingdesc->buffer);
    outgoingdesc->buffer = outgoingbuf; // claim desc
  }

  /*  setup message meta-data */
  amudp_msg_t * const msg = &outgoingbuf->msg;
  if (isloopback) AMUDP_MSG_SETFLAGS(msg, TRUE, category, numargs, 0, 0);
  else AMUDP_MSG_SETFLAGS(msg, TRUE, category, numargs, outgoingdesc->seqNum, instance);
  msg->destOffset = dest_offset;
  msg->handlerId = handler;
  msg->nBytes = (uint16_t)nbytes;
  msg->systemMessageType = systemType;
  msg->systemMessageArg = systemArg;
  msg->tag = trans->tag;
  AMUDP_assert(GET_MSG_SZ(msg) == msgsz);

  { /*  setup args */
    int i;
    uint32_t *args = GET_MSG_ARGS(msg);
    for (i = 0; i < numargs; i++) {
      args[i] = (uint32_t)va_arg(argptr, int); /* must be int due to default argument promotion */
    }
    #if USE_CLEAR_UNUSED_SPACE
      if (i < AMUDP_MAX_SHORT) args[i] = 0;
    #endif
  }

  if (isloopback) { /* run handler synchronously */
    amudp_bufstatus_t* const status = &(outgoingbuf->status); /* the status block for this buffer */
    if (nbytes > 0) { /* setup data */
      if (category == amudp_Long) { /* one-copy: buffer was overallocated, could be reduced with more complexity */
        AMUDP_CHECK_ERRFRC(dest_offset + nbytes > request_endpoint->segLength, BAD_ARG, 
                           "AMRequestXfer", "segment overflow", 
                           AMUDP_ReleaseBuffer(request_endpoint, outgoingbuf));
        memmove(((int8_t *)request_endpoint->segAddr) + dest_offset, 
                source_addr, nbytes);
      } else { /* mediums still need data copy */
        memcpy(GET_MSG_DATA(msg), source_addr, nbytes);
      }
    }
    /* pretend its a recv buffer */
    outgoingbuf->status.rx.dest = request_endpoint;
    outgoingbuf->status.rx.sourceId = reply_endpoint;
    outgoingbuf->status.rx.sourceAddr = destaddress;

    AMUDP_processPacket(outgoingbuf, 1);

    AMUDP_ReleaseBuffer(request_endpoint, outgoingbuf);
  } else { /* perform the send */

    /*  setup data */
    if (nbytes > 0) {
      memcpy(GET_MSG_DATA(msg), source_addr, nbytes);
    }

    int retval = sendPacket(request_endpoint, msg, msgsz, destaddress, REQUESTREPLY_PACKET);
    if_pf (retval != AM_OK) {
      outgoingdesc->buffer = NULL; /*  send failed, so message rejected - release buffer */
      AMUDP_ReleaseBuffer(request_endpoint, outgoingbuf);
      perProcInfo->instanceHint = instance;
      AMUDP_RETURN(retval);
    }

    { amudp_cputick_t now = getCPUTicks();
      uint32_t ustimeout = AMUDP_InitialRequestTimeout_us;
      /* we carefully use 32-bit datatypes here to avoid 64-bit multiply/divide */
      static uint32_t expectedusperbyte = 0; /* cache precomputed value */
      static amudp_cputick_t ticksperus = 0;
      static int firsttime = 1;
      if_pf (firsttime) {
        ticksperus = us2ticks(1);
        expectedusperbyte = /* allow 2x of slop for reply */
          (uint32_t)((2 * 1000000.0 / 1024.0) / AMUDP_ExpectedBandwidth);
        firsttime = 0;
      }
     if (AMUDP_InitialRequestTimeout_us == AMUDP_TIMEOUT_INFINITE) { // never timeout
       outgoingbuf->status.tx.timestamp = (amudp_cputick_t)-1;
       outgoingbuf->status.tx.retryCount = 0;
     } else {
      uint32_t expectedus = (msgsz * expectedusperbyte);
      /* bulk transfers may have a noticeable wire delay, so we grow the initial timeout
       * accordingly to allow time for the transfer to arrive and be serviced
       * These are the transfers that are really expensive to retransmit, 
       * so we want to avoid that until we're relatively certain they've really been lost
       */
      int retryCount = 0;
      while (ustimeout < expectedus && ustimeout < AMUDP_MaxRequestTimeout_us) {
        ustimeout *= AMUDP_RequestTimeoutBackoff;
        retryCount++;
      }
      outgoingbuf->status.tx.timestamp = now + (((amudp_cputick_t)ustimeout)*ticksperus);
      outgoingbuf->status.tx.retryCount = retryCount;
     }
     outgoingbuf->status.tx.transmitCount = 1;
     #if AMUDP_COLLECT_LATENCY_STATS
       outgoingbuf->status.tx.firstSendTime = now;
     #endif
    }
    outgoingbuf->status.tx.destId = destP;
    AMUDP_EnqueueTxBuffer(request_endpoint, outgoingbuf);

    AMUDP_STATS(request_endpoint->stats.RequestsSent[category]++);
    AMUDP_STATS(request_endpoint->stats.RequestDataBytesSent[category] += sizeof(int) * numargs + nbytes);
    AMUDP_STATS(request_endpoint->stats.RequestTotalBytesSent[category] += msgsz);
  }

  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
static int AMUDP_ReplyGeneric(amudp_category_t category, 
                          amudp_buf_t *requestbuf, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr,
                          uint8_t systemType, uint8_t systemArg) {
  ep_t const ep = requestbuf->status.rx.dest;
  amudp_node_t const destP = requestbuf->status.rx.sourceId;
  const int isloopback = enEqual(requestbuf->status.rx.sourceAddr, ep->name);
  amudp_perproc_info_t * const perProcInfo = &ep->perProcInfo[destP];

  /*  we don't poll within a reply because by definition we are already polling somewhere in the call chain */

  size_t const msgsz = COMPUTE_MSG_SZ(numargs, nbytes);
  size_t const buffersz = MSGSZ_TO_BUFFERSZ(msgsz);
  amudp_buf_t * const outgoingbuf = AMUDP_AcquireBuffer(ep, buffersz);
  amudp_bufdesc_t *outgoingdesc;
  uint16_t instance;

  if (isloopback) {
    #if AMUDP_DEBUG
      outgoingdesc = NULL; /* not used */
      instance = 0; /* not used */
    #endif
  } else {
    /*  acquire a free descriptor  */
    /*  trivial because replies always overwrite previous reply in request instance */
    instance = AMUDP_MSG_INSTANCE(&(requestbuf->msg)); 
    outgoingdesc = GET_REP_DESC(ep, destP, instance); // reply desc alloc in processPacket

    if (outgoingdesc->buffer) { /* free buffer of previous reply */
      AMUDP_ReleaseBuffer(ep, outgoingdesc->buffer);
    }
    outgoingdesc->buffer = outgoingbuf;
  }

  /*  setup message meta-data */
  amudp_msg_t * const msg = &outgoingbuf->msg;
  if (isloopback) AMUDP_MSG_SETFLAGS(msg, FALSE, category, numargs, 0, 0);
  else AMUDP_MSG_SETFLAGS(msg, FALSE, category, numargs, 
                          AMUDP_MSG_SEQNUM(&requestbuf->msg), // clone request seqnum, as rep_desc already inc
                          instance);
  msg->destOffset = dest_offset;
  msg->handlerId = handler;
  msg->nBytes = (uint16_t)nbytes;
  msg->systemMessageType = systemType;
  msg->systemMessageArg = systemArg;
  msg->tag = perProcInfo->tag;
  AMUDP_assert(GET_MSG_SZ(msg) == msgsz);

  { /*  setup args */
    int i;
    uint32_t *args = GET_MSG_ARGS(msg);
    for (i = 0; i < numargs; i++) {
      args[i] = (uint32_t)va_arg(argptr, int); /* must be int due to default argument promotion */
    }
    #if USE_CLEAR_UNUSED_SPACE
      if (i < AMUDP_MAX_SHORT) args[i] = 0;
    #endif
  }

  en_t const destaddress = perProcInfo->remoteName;
  if (isloopback) { /* run handler synchronously */
    amudp_bufstatus_t* const status = &(outgoingbuf->status); /* the status block for this buffer */
    if (nbytes > 0) { /* setup data */
      if (category == amudp_Long) { /* one-copy */
        AMUDP_CHECK_ERRFRC(dest_offset + nbytes > ep->segLength, BAD_ARG, 
                           "AMRequestXfer", "segment overflow",
                           AMUDP_ReleaseBuffer(ep, outgoingbuf));
        memmove(((int8_t *)ep->segAddr) + dest_offset, 
                source_addr, nbytes);
      } else { /* mediums still need data copy */
        memcpy(GET_MSG_DATA(msg), source_addr, nbytes);
      }
    }

    /* pretend its a recv buffer */
    outgoingbuf->status.rx.dest = ep;
    outgoingbuf->status.rx.sourceId = destP;
    outgoingbuf->status.rx.sourceAddr = destaddress;

    AMUDP_processPacket(outgoingbuf, 1);

    AMUDP_ReleaseBuffer(ep, outgoingbuf);
  } else { /* perform the send */
    /*  setup data */
    memcpy(GET_MSG_DATA(msg), source_addr, nbytes);

    int retval = sendPacket(ep, msg, msgsz, destaddress, REQUESTREPLY_PACKET);
    if_pf (retval != AM_OK) AMUDP_RETURN(retval);

    outgoingbuf->status.tx.transmitCount = 1;
    AMUDP_STATS(ep->stats.RepliesSent[category]++);
    AMUDP_STATS(ep->stats.ReplyDataBytesSent[category] += sizeof(int) * numargs + nbytes);
    AMUDP_STATS(ep->stats.ReplyTotalBytesSent[category] += msgsz);
  }

  requestbuf->status.rx.replyIssued = TRUE;
  return AM_OK;
}

/*------------------------------------------------------------------------------------
 * Request
 *------------------------------------------------------------------------------------ */
extern int AMUDP_RequestVA(ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                         int numargs, va_list argptr) {
  AMUDP_CHECKINIT();
  AMUDP_CHECK_ERR(!request_endpoint, BAD_ARG);
  AMUDP_CHECK_ERR(AMUDP_BADHANDLERVAL(handler), BAD_ARG);
  AMUDP_CHECK_ERR(request_endpoint->depth == -1, NOT_INIT); /* it's an error to call before AM_SetExpectedResources */
  AMUDP_CHECK_ERR(reply_endpoint >= request_endpoint->translationsz ||
     !request_endpoint->translation[reply_endpoint].inuse, BAD_ARG);
  AMUDP_assert(numargs >= 0 && numargs <= AMUDP_MAX_SHORT);

  return AMUDP_RequestGeneric(amudp_Short, 
                                  request_endpoint, reply_endpoint, handler, 
                                  NULL, 0, 0,
                                  numargs, argptr,
                                  amudp_system_user, 0);

}
extern int AMUDP_Request(ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                         int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMUDP_RequestVA(request_endpoint, reply_endpoint, handler, 
                           numargs, argptr);
    va_end(argptr);
    return retval;
}
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_RequestIVA(ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, va_list argptr) {
  AMUDP_CHECKINIT();
  AMUDP_CHECK_ERR(!request_endpoint || reply_endpoint < 0, BAD_ARG);
  AMUDP_CHECK_ERR(AMUDP_BADHANDLERVAL(handler), BAD_ARG);
  AMUDP_CHECK_ERR(request_endpoint->depth == -1, NOT_INIT); /* it's an error to call before AM_SetExpectedResources */
  AMUDP_CHECK_ERR(reply_endpoint >= request_endpoint->translationsz ||
     !request_endpoint->translation[reply_endpoint].inuse, BAD_ARG);
  AMUDP_CHECK_ERR(!source_addr, BAD_ARG);
  AMUDP_CHECK_ERR(nbytes < 0 || nbytes > AMUDP_MAX_MEDIUM, BAD_ARG);
  AMUDP_assert(numargs >= 0 && numargs <= AMUDP_MAX_SHORT);

  return AMUDP_RequestGeneric(amudp_Medium, 
                                  request_endpoint, reply_endpoint, handler, 
                                  source_addr, nbytes, 0,
                                  numargs, argptr,
                                  amudp_system_user, 0);
}
extern int AMUDP_RequestI(ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMUDP_RequestIVA(request_endpoint, reply_endpoint, handler, 
                              source_addr, nbytes,
                              numargs, argptr);
    va_end(argptr);
    return retval; 
}
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_RequestXferVA(ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int async, 
                          int numargs, va_list argptr) {
  AMUDP_CHECKINIT();
  AMUDP_CHECK_ERR(!request_endpoint || reply_endpoint < 0, BAD_ARG);
  AMUDP_CHECK_ERR(AMUDP_BADHANDLERVAL(handler), BAD_ARG);
  AMUDP_CHECK_ERR(request_endpoint->depth == -1, NOT_INIT); /* it's an error to call before AM_SetExpectedResources */
  AMUDP_CHECK_ERR(reply_endpoint >= request_endpoint->translationsz ||
     !request_endpoint->translation[reply_endpoint].inuse, BAD_ARG);
  AMUDP_CHECK_ERR(!source_addr, BAD_ARG);
  AMUDP_CHECK_ERR(nbytes < 0 || nbytes > AMUDP_MAX_LONG, BAD_ARG);
  AMUDP_CHECK_ERR(dest_offset > AMUDP_MAX_SEGLENGTH, BAD_ARG);
  AMUDP_assert(numargs >= 0 && numargs <= AMUDP_MAX_SHORT);

  amudp_translation_t const * const trans = &request_endpoint->translation[reply_endpoint];
  amudp_node_t destP = trans->id;
  const int isloopback = enEqual(trans->name, request_endpoint->name);

  if (async && !isloopback) { /*  decide if we can satisfy request without blocking */
      /* it's unclear from the spec whether we should poll before an async failure,
       * but by definition the app must be prepared for handlers to run when calling this 
       * request, so it shouldn't cause anything to break, and the async request is more likely
       * to succeed if we do. so:
       */
      AM_Poll(request_endpoint->eb);

      /* see if there's a free buffer */
      amudp_bufdesc_t * const desc = GET_REQ_DESC_ALLOC(request_endpoint, destP, 0);
      int i;
      int const depth = request_endpoint->depth;
      for (i = 0; i < depth; i++) {
        if (!desc[i].buffer) break;
      }
      if (i == depth) AMUDP_RETURN_ERRFR(IN_USE, AMUDP_RequestXferAsync, 
                                         "Request can't be satisfied without blocking right now");
  }

  /* perform the send */
  return AMUDP_RequestGeneric(amudp_Long, 
                                  request_endpoint, reply_endpoint, handler, 
                                  source_addr, nbytes, dest_offset,
                                  numargs, argptr,
                                  amudp_system_user, 0);
}
extern int AMUDP_RequestXfer(ep_t request_endpoint, amudp_node_t reply_endpoint, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int async, 
                          int numargs, ...) {
      int retval;
      va_list argptr;
      va_start(argptr, numargs); /*  pass in last argument */
      retval = AMUDP_RequestXferVA(request_endpoint, reply_endpoint, handler, 
                                source_addr, nbytes, dest_offset,
                                async,
                                numargs, argptr);
      va_end(argptr);
      return retval; 
}
/*------------------------------------------------------------------------------------
 * Reply
 *------------------------------------------------------------------------------------ */
extern int AMUDP_ReplyVA(void *token, handler_t handler, 
                       int numargs, va_list argptr) {
  AMUDP_CHECKINIT();
  AMUDP_CHECK_ERR(!token, BAD_ARG);
  AMUDP_CHECK_ERR(AMUDP_BADHANDLERVAL(handler), BAD_ARG);
  AMUDP_assert(numargs >= 0 && numargs <= AMUDP_MAX_SHORT);

  amudp_buf_t * const buf = (amudp_buf_t *)token;
  amudp_msg_t * const msg = &buf->msg;

  //  semantic checking on reply
  AMUDP_CHECK_ERR(!AMUDP_MSG_ISREQUEST(msg), RESOURCE);       /* token is not a request */
  AMUDP_CHECK_ERR(!buf->status.rx.handlerRunning, RESOURCE); /* token is not for an active request */
  AMUDP_CHECK_ERR((buf->status.rx.replyIssued), RESOURCE);     /* already issued a reply */
  AMUDP_CHECK_ERR(((amudp_system_messagetype_t)msg->systemMessageType) != amudp_system_user,
                    RESOURCE); /* can't reply to a system message (returned message) */

  return AMUDP_ReplyGeneric(amudp_Short, 
                                  buf, handler, 
                                  NULL, 0, 0,
                                  numargs, argptr,
                                  amudp_system_user, 0);
}
extern int AMUDP_Reply(void *token, handler_t handler, 
                       int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMUDP_ReplyVA(token, handler,
                                  numargs, argptr);
    va_end(argptr);
    return retval; 
}
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_ReplyIVA(void *token, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, va_list argptr) {
  AMUDP_CHECKINIT();
  AMUDP_CHECK_ERR(!token, BAD_ARG);
  AMUDP_CHECK_ERR(AMUDP_BADHANDLERVAL(handler), BAD_ARG);
  AMUDP_CHECK_ERR(!source_addr, BAD_ARG);
  AMUDP_CHECK_ERR(nbytes < 0 || nbytes > AMUDP_MAX_MEDIUM, BAD_ARG);
  AMUDP_assert(numargs >= 0 && numargs <= AMUDP_MAX_SHORT);

  amudp_buf_t * const buf = (amudp_buf_t *)token;
  amudp_msg_t * const msg = &buf->msg;

  //  semantic checking on reply
  AMUDP_CHECK_ERR(!AMUDP_MSG_ISREQUEST(msg), RESOURCE);       /* token is not a request */
  AMUDP_CHECK_ERR(!buf->status.rx.handlerRunning, RESOURCE); /* token is not for an active request */
  AMUDP_CHECK_ERR(buf->status.rx.replyIssued, RESOURCE);     /* already issued a reply */
  AMUDP_CHECK_ERR(((amudp_system_messagetype_t)msg->systemMessageType) != amudp_system_user,
                    RESOURCE); /* can't reply to a system message (returned message) */

  return AMUDP_ReplyGeneric(amudp_Medium, 
                                  buf, handler, 
                                  source_addr, nbytes, 0,
                                  numargs, argptr,
                                  amudp_system_user, 0);
}
extern int AMUDP_ReplyI(void *token, handler_t handler, 
                          void *source_addr, int nbytes,
                          int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMUDP_ReplyIVA(token, handler,
                                  source_addr, nbytes,
                                  numargs, argptr);
    va_end(argptr);
    return retval; 
}
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_ReplyXferVA(void *token, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, va_list argptr) {
  AMUDP_CHECKINIT();
  AMUDP_CHECK_ERR(!token, BAD_ARG);
  AMUDP_CHECK_ERR(AMUDP_BADHANDLERVAL(handler), BAD_ARG);
  AMUDP_CHECK_ERR(!source_addr, BAD_ARG);
  AMUDP_CHECK_ERR(nbytes < 0 || nbytes > AMUDP_MAX_LONG, BAD_ARG);
  AMUDP_CHECK_ERR(dest_offset > AMUDP_MAX_SEGLENGTH, BAD_ARG);
  AMUDP_assert(numargs >= 0 && numargs <= AMUDP_MAX_SHORT);

  amudp_buf_t * const buf = (amudp_buf_t *)token;
  amudp_msg_t * const msg = &buf->msg;

  //  semantic checking on reply
  AMUDP_CHECK_ERR(!AMUDP_MSG_ISREQUEST(msg), RESOURCE);       /* token is not a request */
  AMUDP_CHECK_ERR(!buf->status.rx.handlerRunning, RESOURCE); /* token is not for an active request */
  AMUDP_CHECK_ERR(buf->status.rx.replyIssued, RESOURCE);     /* already issued a reply */
  AMUDP_CHECK_ERR(((amudp_system_messagetype_t)msg->systemMessageType) != amudp_system_user,
                    RESOURCE); /* can't reply to a system message (returned message) */

  return AMUDP_ReplyGeneric(amudp_Long, 
                                  buf, handler, 
                                  source_addr, nbytes, dest_offset,
                                  numargs, argptr,
                                  amudp_system_user, 0);
}
extern int AMUDP_ReplyXfer(void *token, handler_t handler, 
                          void *source_addr, int nbytes, uintptr_t dest_offset, 
                          int numargs, ...) {
    int retval;
    va_list argptr;
    va_start(argptr, numargs); /*  pass in last argument */
    retval = AMUDP_ReplyXferVA(token, handler,
                                  source_addr, nbytes, dest_offset,
                                  numargs, argptr);
    va_end(argptr);
    return retval; 
}
/* ------------------------------------------------------------------------------------ */
extern void AMUDP_DefaultReturnedMsg_Handler(int status, op_t opcode, void *token) {
  const char *statusStr = "*unknown*";
  const char *opcodeStr = "*unknown*";
  amudp_buf_t * const buf = (amudp_buf_t *)token;
  amudp_msg_t * const msg = &buf->msg;
  int numArgs = AMUDP_MSG_NUMARGS(msg);
  uint32_t const * const args = GET_MSG_ARGS(msg);
  char argStr[255];

  #define STATCASE(name, desc) case name: statusStr = #name ": " desc; break;
  switch (status) {
    STATCASE(EBADARGS      , "Arguments to request or reply function invalid    ");
    STATCASE(EBADENTRY     , "X-lation table index selected unbound table entry ");
    STATCASE(EBADTAG       , "Sender's tag did not match the receiver's EP tag  "); 
    STATCASE(EBADHANDLER   , "Invalid index into the recv.'s handler table      "); 
    STATCASE(EBADSEGOFF    , "Offset into the dest-memory VM segment invalid    ");
    STATCASE(EBADLENGTH    , "Bulk xfer length goes beyond a segment's end      ");
    STATCASE(EBADENDPOINT  , "Destination endpoint does not exist               ");
    STATCASE(ECONGESTION   , "Congestion at destination endpoint                ");
    STATCASE(EUNREACHABLE  , "Destination endpoint unreachable                  ");
    STATCASE(EREPLYREJECTED, "Destination endpoint refused reply message        ");
    }
  #define OPCASE(name) case name: opcodeStr = #name; break;
  switch (opcode) {
    OPCASE(AM_REQUEST_M);
    OPCASE(AM_REQUEST_IM);
    OPCASE(AM_REQUEST_XFER_M);
    OPCASE(AM_REPLY_M);
    OPCASE(AM_REPLY_IM);
    OPCASE(AM_REPLY_XFER_M);
  }

  argStr[0] = '\0';
  for (int i=0; i < numArgs; i++) {
    char tmp[20];
    sprintf(tmp, "0x%08x  ", (int)args[i]);
    strcat(argStr, tmp);
  }
  { char temp1[80];
    char temp2[80];
    AMUDP_FatalErr("An active message was returned to sender,\n"
             "    and trapped by the default returned message handler (handler 0):\n"
             "Error Code: %s\n"
             "Message type: %s\n"
             "Destination: %s (%i)\n"
             "Handler: %i\n"
             "Tag: %s\n"
             "Arguments(%i): %s\n"
             "Aborting...",
             statusStr, opcodeStr, 
             AMUDP_enStr(buf->status.rx.sourceAddr, temp1), buf->status.rx.sourceId,
             msg->handlerId, AMUDP_tagStr(msg->tag, temp2),
             numArgs, argStr);
  }
}
/* ------------------------------------------------------------------------------------ */
#if AMUDP_EXTRA_CHECKSUM
static uint16_t checksum(uint8_t const * const data, size_t len) {
  uint16_t val = 0;
  for (size_t i=0; i < len; i++) { // a simple, fast, non-secure checksum
    uint8_t stir = (uint8_t)(i & 0xFF);
    val = (val << 8) | 
          ( ((val >> 8) & 0xFF) ^ data[i] ^ stir );
  }
  return val;
}
static void AMUDP_SetChecksum(amudp_msg_t * const m, size_t len) {
  AMUDP_assert(len > 0 && len <= AMUDP_MAX_MSG);
  m->packetlen = (uint32_t)len;
  uint8_t *data = (uint8_t *)&(m->packetlen); 
  uint16_t chk = checksum(data, len - 4); // checksum includes chk* fields
  m->chk1 = chk;
  m->chk2 = chk;
}
static void AMUDP_ValidateChecksum(amudp_msg_t const * const m, size_t len) {
  static char report[512];
  int failed = 0;

  { static int firstcall = 1;
    if (firstcall) AMUDP_Warn("AMUDP_EXTRA_CHECKSUM is enabled. This mode is ONLY intended for debugging system problems.");
    firstcall = 0;
  }

  if_pf (m->chk1 != m->chk2) {
    strcat(report, " : Checksum field corrupted");
    failed = 1;
  }
  if_pf (len != m->packetlen) {
    strcat(report, " : Length mismatch");
    failed = 1;
  }
  if_pf (len < AMUDP_MIN_MSG || len > AMUDP_MAX_MSG) {
    strcat(report, " : Packet length illegal");
    failed = 1;
  }

  uint8_t const * const data = (uint8_t const *)&(m->packetlen); 
  size_t datalen = len-4;
  uint16_t recvchk = checksum(data, datalen);

  if_pf (recvchk != m->chk1) {
    strcat(report, " : Checksum mismatch on data");
    failed = 1;
  }

  if_pf (failed) {
    // further analysis
    uint8_t val = data[datalen-1];
    int rep = 0;
    for (int i=datalen-1; i >= 0; i--) {
      if (data[i] == val) rep++;
      else break;
    }
    if (rep > 1) {
      char tmp[80];
      sprintf(tmp," : Final %d bytes are 0x%02x",rep,val);
      strcat(report,tmp);
    }
    AMUDP_FatalErr("UDP packet failed checksum!\n  recvLen: %d  packetlen: %d\n  chk1:0x%04x  chk2:0x%04x  recvchk:0x%04x\n  Analysis%s\n",
                    (int)len, (int)m->packetlen, m->chk1, m->chk2, recvchk, report);
  }
}
#endif
