/* $Id: gasnet_core.c,v 1.10 2002/06/26 21:03:29 csbell Exp $
 * $Date: 2002/06/26 21:03:29 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = -1;
gasnet_node_t gasnetc_nodes = 0;
gasnetc_state_t _gmc;
gasnet_seginfo_t *gasnetc_seginfo = NULL;
int gasnetc_init_done = 0; /*  true after init */

#ifdef GASNETI_THREADS
pthread_mutex_t _gasnetc_lock_gm = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t _gasnetc_lock_reqfifo = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t _gasnetc_lock_amreq = PTHREAD_MUTEX_INITIALIZER;
#endif

void gasnetc_checkinit() {
  if (!gasnetc_init_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
}

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  return;
}

static int gasnetc_init(int *argc, char ***argv, 
                gasnet_handlerentry_t *table, int numentries, 
                void *segbase, uintptr_t segsize,
                int allowFaults) {
  int retval = GASNET_OK;
  size_t pagesize = gasneti_getSystemPageSize();
  int havehint = (segbase != GASNET_SEGBASE_ANY);
  int sizeeverything = (segsize == GASNET_SEGSIZE_EVERYTHING);
  char checkuniqhandler[256];

  /*  check system sanity */
  gasnetc_check_config();

  if (gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");


  /*  check segment base hint sanity */
  if (havehint) { /*  have a hint */
    if (((uintptr_t)segbase) > GASNET_SEGSIZE_EVERYTHING - segsize) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segment too large to fit at hint base address");

    if (((uintptr_t)segbase) % pagesize != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segbase not page-aligned");
  }

  /*  check segment size sanity */
  if (segsize == 0 || 
      (!sizeeverything && segsize % pagesize != 0)) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize zero or not even multiple of page size");

  if (sizeeverything && !allowFaults) 
      GASNETI_RETURN_ERRR(RESOURCE, "client requested GASNET_SEGSIZE_EVERYTHING, but !allowFaults "
	"(asking us to mmap entire VA space - don't even try to do that)");

#ifdef HAVE_GEXEC
  gasnetc_mynode = -1;
  gasnetc_nodes = 0;
#else
  if (gasnetc_gmpiconf_init() != GASNET_OK)
	  GASNETI_RETURN_ERRR(RESOURCE, "GMPI-based init failed");
  gasnetc_sendbuf_init();
  assert(_gmc.gm_nodes != NULL);
  assert(_gmc.gm_nodes_rev != NULL);
#endif

  /* ------------------------------------------------------------------------------------ */
  /*  register handlers */
  memset(checkuniqhandler, 0, 256);
  /* GMCORE BEGIN */
  gasnetc_AM_InitHandler();
  /* GMCORE END */
  { /*  core API handlers */
    gasnet_handlerentry_t const *ctable = gasnetc_get_handlertable();
    int i;
    assert(ctable);
    for (i = 0; ctable[i].fnptr; i++) {
      assert(ctable[i].index); /*  ensure all core API handlers have pre-assigned index 1..99 */
      assert(ctable[i].index >= 1 && ctable[i].index <= 99);
      /* discover duplicates */
      assert(checkuniqhandler[ctable[i].index] == 0);
      checkuniqhandler[ctable[i].index] = 1;
      retval = gasnetc_AM_SetHandler((gasnet_handler_t)ctable[i].index, ctable[i].fnptr);
      if (retval != GASNET_OK)
         GASNETI_RETURN_ERRR(RESOURCE, "AM_SetHandler() failed while registering core handlers");
    }
  }

  { /*  extended API handlers */
    gasnet_handlerentry_t const *etable = gasnete_get_handlertable();
    int i;
    assert(etable);
    for (i = 0; etable[i].fnptr; i++) {
      assert(etable[i].index); /*  ensure all extended API handlers have pre-assigned index 100..199 */
      assert(etable[i].index >= 100 && etable[i].index <= 199);
      /* discover duplicates */
      assert(checkuniqhandler[etable[i].index] == 0);
      checkuniqhandler[etable[i].index] = 1;
      retval = gasnetc_AM_SetHandler((gasnet_handler_t)etable[i].index, etable[i].fnptr);
      if (retval != GASNET_OK)
         GASNETI_RETURN_ERRR(RESOURCE, "AM_SetHandler() failed while registering extended handlers");
    }
  }

  if (table) { /*  client handlers */
    int i;
    
    if (numentries > 56) 
      GASNETI_RETURN_ERRR(BAD_ARG, "client tried to register too many handlers (limit=56)");

    /*  first pass - assign all fixed-index handlers */
    for (i = 0; i < numentries; i++) {
      if (table[i].index) {
        assert(table[i].index >= 200 && table[i].index <= 255);
        assert(table[i].fnptr);
        /* discover duplicates */
        assert(checkuniqhandler[table[i].index] == 0);
        checkuniqhandler[table[i].index] = 1;
        retval = gasnetc_AM_SetHandler((gasnet_handler_t)table[i].index, table[i].fnptr);
        if (retval != GASNET_OK)
           GASNETI_RETURN_ERRR(RESOURCE, "AM_SetHandler() failed while registering client handlers");
      }
    }
    /*  second pass - fill in dontcare-index handlers */
    for (i = 0; i < numentries; i++) {
      if (!table[i].index) {
        gasnet_handler_t tmp;
        assert(table[i].fnptr);
        for (tmp=200; tmp < 255; tmp++) {
          if (checkuniqhandler[tmp] == 0) break;
        }
	retval = gasnetc_AM_SetHandlerAny(&tmp, table[i].fnptr);
        table[i].index = tmp;
        /* discover duplicates */
        assert(checkuniqhandler[table[i].index] == 0);
        checkuniqhandler[table[i].index] = 1;
        if (retval != GASNET_OK)
           GASNETI_RETURN_ERRR(RESOURCE, 
	     "gasnetc_AM_SetHandlerAny() failed while registering don't-care client handlers");
      }
    }
  }

  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /*  (...) catch fatal signals and convert to SIGQUIT */

  /* ------------------------------------------------------------------------------------ */
  /*  gather the highest sbrk(0) for all nodes */
  
  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  if (segbase == GASNET_SEGBASE_ANY) {
    /* alignment guarenteed by segment_gather(0) */
    void *mem = gasnetc_segment_gather(0);

    mem = gasnetc_segment_alloc(mem, segsize);
    gasnetc_segment_register(mem, segsize);
    segbase = mem;
  } else {
    void *mem = gasnetc_segment_alloc(segbase, segsize);
    gasnetc_segment_register(mem, segsize);
  }

  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));
  {
    int i;
    for (i = 0; i < gasnetc_nodes; i++) {
      gasnetc_seginfo[i].addr = segbase;
      gasnetc_seginfo[i].size = segsize;
    }
  }

  assert(gasnetc_seginfo[gasnetc_mynode].addr == segbase &&
         gasnetc_seginfo[gasnetc_mynode].size == segsize);

  #if GASNET_ALIGNED_SEGMENTS == 1
    {  /*  need to check that segments are aligned */
      int i;
      for (i=0; i < gasnetc_nodes; i++) {
        if (gasnetc_seginfo[i].size != segsize) 
          GASNETI_RETURN_ERRR(RESOURCE, "client-requested sizes differ - failure");
        if (gasnetc_seginfo[i].addr != segbase) {
          /*  we grabbed unaligned memory */
          /*  TODO: need a recovery mechanism here ... */
          assert(0);
        }
      }
    }
  #endif

  gasnetc_SysBarrier();
  /*  grab GM buffers and make sure we have the maximum amount possible */
  gasnetc_provide_receive_buffers();

  /*  init complete */
  gasnetc_init_done = 1;
  GASNETI_TRACE_PRINTF(C, ("init done"));
  return GASNET_OK;
}


extern int gasnet_init(int *argc, char ***argv, 
                gasnet_handlerentry_t *table, int numentries, 
                void *segbase, uintptr_t segsize,
                int allowFaults) {
  int retval = gasnetc_init(argc, argv, table, numentries, segbase, segsize, allowFaults);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasnete_init();
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern void gasnetc_exit(int exitcode) {
  gasnetc_sendbuf_finalize();
  if (gasnetc_init_done)
  	gm_close(_gmc.port);
  gm_finalize();
  exit(exitcode);
}

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
  GASNETC_CHECKINIT();
  assert(gasnetc_seginfo && seginfo_table);
  if (!gasnetc_init_done) GASNETI_RETURN_ERR(NOT_INIT);
  if (numentries < gasnetc_nodes) GASNETI_RETURN_ERR(BAD_ARG);
  memset(seginfo_table, 0, numentries*sizeof(gasnet_seginfo_t));
  memcpy(seginfo_table, gasnetc_seginfo, gasnetc_nodes*sizeof(gasnet_seginfo_t));
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  gasnet_node_t sourceid;
  gasnetc_bufdesc_t *bufd;

  GASNETC_CHECKINIT();
  if_pf (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if_pf (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  bufd = (gasnetc_bufdesc_t *) token;
  if_pf (!bufd->e) GASNETI_RETURN_ERRR(BAD_ARG, "No GM receive event");
  sourceid = 
  	gasnetc_gm_nodes_search(gm_ntoh_u16(bufd->e->recv.sender_node_id),
	    (uint16_t) gm_ntoh_u8(bufd->e->recv.sender_port_id));

  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;
  int len;

  GASNETC_CHECKINIT();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  va_start(argptr, numargs); /*  pass in last argument */

  retval = 1;
  if (dest == gasnetc_mynode) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    GASNETC_AMTRACE_RequestShort(Loopbk);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], (void *) -1, argbuf, numargs);
  }
  else {
    bufd = gasnetc_AMRequestPool_block();
    len = gasnetc_write_AMBufferShort(bufd->sendbuf, handler, numargs, argptr, REQUEST);
    gasnetc_tokensend_AMRequest(bufd->sendbuf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_AMRequest, (void *)bufd, 0);
    GASNETC_AMTRACE_RequestShort(Send);
  }

  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;
  int len;
  GASNETC_CHECKINIT();

  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  va_start(argptr, numargs); /*  pass in last argument */

  retval = 1;
  if (dest == gasnetc_mynode) { /* local handler */
    void *loopbuf;
    int argbuf[GASNETC_AM_MAX_ARGS];
    loopbuf = gasnetc_alloca(nbytes);
    memcpy(loopbuf, source_addr, nbytes);
    GASNETC_AMTRACE_RequestMedium(Loopbk);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler], (void *) -1,
				argbuf, numargs, loopbuf, nbytes);
  }
  else {
    bufd = gasnetc_AMRequestPool_block();
    len = gasnetc_write_AMBufferMedium(bufd->sendbuf, handler, numargs, argptr, 
		 nbytes, source_addr, REQUEST);
    gasnetc_tokensend_AMRequest(bufd->sendbuf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_AMRequest, (void *)bufd, 0);
    GASNETC_AMTRACE_RequestMedium(Send);
  }

  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;
  int port, id, len;
  size_t bytes_left;
  void *source_addr_cur;
  uint64_t dest_addr_cur;
  GASNETC_CHECKINIT();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  va_start(argptr, numargs); /*  pass in last argument */

  retval = 1;
  if (dest == gasnetc_mynode) {
    int argbuf[GASNETC_AM_MAX_ARGS];
    GASNETC_AMTRACE_RequestLong(Loopbk);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_AMPAYLOAD_WRITE(dest_addr, source_addr, nbytes);
    GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *) -1, argbuf, numargs,
			dest_addr, nbytes);
  }
  else {
    port = gasnetc_portid(dest);
    id = gasnetc_nodeid(dest);

    dest_addr_cur = (uint64_t) (uint32_t) dest_addr;
    source_addr_cur = source_addr;
    bytes_left = nbytes;

    while (bytes_left > GASNETC_AM_LONG_HEADER_LEN(numargs)) {
      int bytes_next = MIN(bytes_left, GASNETC_AM_LEN);

      bufd = gasnetc_AMRequestPool_block();
      gasnetc_write_AMBufferBulk(bufd->sendbuf, source_addr_cur, bytes_next);

      gasnetc_tokensend_AMRequest(bufd->sendbuf, bytes_next, id, port, 
		    gasnetc_callback_AMRequest, (void *) bufd, dest_addr_cur);

      dest_addr_cur += bytes_next;
      source_addr_cur += bytes_next;
      bytes_left -= bytes_next;
    }

    bufd = gasnetc_AMRequestPool_block();
    len = gasnetc_write_AMBufferLong(bufd->sendbuf, handler, numargs, argptr, 
		  nbytes, source_addr, dest_addr, REQUEST);
    if (bytes_left > 0) {
      gasnetc_write_AMBufferBulk(bufd->sendbuf+len, source_addr_cur, bytes_left);
      gasnetc_tokensend_AMRequest(bufd->sendbuf+len, bytes_left, id, port, 
		    gasnetc_callback_AMRequest_NOP, NULL, dest_addr_cur);
    }
    gasnetc_tokensend_AMRequest(bufd->sendbuf, len, id, port, 
		    gasnetc_callback_AMRequest, (void *)bufd, 0);
    GASNETC_AMTRACE_RequestLong(Send);
  }

  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

#ifdef GASNETC_DYNAMIC_REGISTRATION
extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETC_CHECKINIT();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  va_start(argptr, numargs); /*  pass in last argument */

    /* (...) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = ...;
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}
#else
#define gasnetc_AMRequestLongAsyncM gasnetc_AMRequestLongM
#endif

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;

  va_start(argptr, numargs); /*  pass in last argument */
  retval = 1;
  if ((void *)token == (void*)-1) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    GASNETC_AMTRACE_ReplyShort(Loopbk);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], (void *) token, argbuf, numargs);
  }
  else {
    bufd = gasnetc_bufdesc_from_token(token);
    bufd->len = gasnetc_write_AMBufferShort(bufd->sendbuf, handler, 
		    numargs, argptr, REPLY);
  
    GASNETC_GM_MUTEX_LOCK;
    if (gasnetc_token_hi_acquire()) {
       GASNETC_AMTRACE_ReplyShort(Send);
       gasnetc_gm_send_AMReply(bufd);
    } else {
       GASNETC_AMTRACE_ReplyShort(Queued);
       gasnetc_fifo_insert(bufd);
    }
    GASNETC_GM_MUTEX_UNLOCK;
  }

  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;
  va_start(argptr, numargs); /*  pass in last argument */

  retval = 1;
  if ((void *)token == (void *)-1) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    void *loopbuf;
    loopbuf = gasnetc_alloca(nbytes);
    memcpy(loopbuf, source_addr, nbytes);
    GASNETC_AMTRACE_ReplyMedium(Loopbk);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler], (void *) token,
				argptr, numargs, loopbuf, nbytes);
  }
  else {
    if_pf (nbytes > GASNETC_AM_MEDIUM_MAX) 
	    GASNETI_RETURN_ERRR(BAD_ARG,"AMMedium Payload too large");
    bufd = gasnetc_bufdesc_from_token(token);
    bufd->len = 
	    gasnetc_write_AMBufferMedium(bufd->sendbuf, handler, numargs, 
                    argptr, nbytes, source_addr, REPLY);
    GASNETC_GM_MUTEX_LOCK;
    if (gasnetc_token_hi_acquire()) {
       GASNETC_AMTRACE_ReplyMedium(Send);
       gasnetc_gm_send_AMReply(bufd); 
    } else {
       GASNETC_AMTRACE_ReplyMedium(Queued);
       gasnetc_fifo_insert(bufd);
    }
    GASNETC_GM_MUTEX_UNLOCK;
  }
  
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMReplyLongM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  gasnet_node_t dest;
  va_list argptr;
  int hdr_len;
  gasnetc_bufdesc_t *bufd;

  retval = gasnet_AMGetMsgSource(token, &dest);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  va_start(argptr, numargs); /*  pass in last argument */
  retval = 1;
  if ((void *)token == (void *)-1) {
    int argbuf[GASNETC_AM_MAX_ARGS];
    GASNETC_AMTRACE_ReplyLong(Loopbk);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *) token, argptr, numargs,
			dest_addr, nbytes);
  }
  else {
    if_pf (nbytes > GASNETC_AM_LONG_REPLY_MAX) 
	    GASNETI_RETURN_ERRR(BAD_ARG,"AMLong Payload too large");
    bufd = gasnetc_bufdesc_from_token(token);
    hdr_len = gasnetc_write_AMBufferLong(bufd->sendbuf, handler, numargs, argptr, 
		    nbytes, source_addr, dest_addr, REPLY);
    gasnetc_write_AMBufferBulk(bufd->sendbuf + hdr_len, source_addr, nbytes);
    bufd->dest_addr = (uint64_t) (uint32_t) dest_addr;
    bufd->rdma_off = hdr_len;
  
    /* We need two sends, so the buffering policy must be aware of handling
     * bufdescs (tokens) which imply two sends.  This is possible by setting
     * rmda_off to a value greater than zero.  See the _NOP callback
     * documentation for details.
     */
  
    GASNETC_GM_MUTEX_LOCK;
    if (gasnetc_token_hi_acquire()) {
        gasnetc_gm_send_AMReply(bufd);
	/* Assume successful directed send */
	bufd->rdma_off = 0;
  
        if (gasnetc_token_hi_acquire()) {
	    GASNETC_AMTRACE_ReplyLong(Send);
            gasnetc_gm_send_AMReply(bufd);
	} else {
	    GASNETC_AMTRACE_ReplyLong(Queued);
            gasnetc_fifo_insert(bufd);
	}
    } else {
	GASNETC_AMTRACE_ReplyLong(Queued);
        gasnetc_fifo_insert(bufd);
    }
    GASNETC_GM_MUTEX_UNLOCK;
  }
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
  This section is only required for conduits that may use interrupt-based handler dispatch
  See the GASNet spec and http://www.cs.berkeley.edu/~bonachea/upc/gasnet.html for
    philosophy and hints on efficiently implementing no-interrupt sections
  Note: the extended-ref implementation provides a thread-specific void* within the 
    gasnete_threaddata_t data structure which is reserved for use by the core 
    (and this is one place you'll probably want to use it)
*/
#if NEED_INTERRUPTS
  extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKINIT();
    /* (...) add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKINIT();
    /* (...) add code here to re-enable handler interrupts for _this_ thread */
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%i",strerror(retval));
  }
  #endif

  /* (...) add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%i",strerror(retval));
  }
  #endif

  /* (...) add code here to cleanup conduit-specific HSL state */
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();

  #ifdef GASNETI_THREADS
  { int retval; 
    #if GASNETC_HSL_SPINLOCK
      do {
        retval = pthread_mutex_trylock(&(hsl->lock));
      } while (retval == EBUSY);
    #else
        retval = pthread_mutex_lock(&(hsl->lock));
    #endif
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_lock(), pthread_mutex_lock()=%i",strerror(retval));
  }
  #endif

  /* (...) conduits with interrupt-based handler dispatch need to add code here to 
           disable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock acquire and we're not inside an enclosing no-interrupt section)
   */
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();

  /* (...) conduits with interrupt-based handler dispatch need to add code here to 
           re-enable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock release and we're not inside an enclosing no-interrupt section)
   */

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_unlock(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%i",strerror(retval));
  }
  #endif
}

/* ------------------------------------------------------------------------------------ */
/*
  Private Handlers:
  ================
  see mpi-conduit and extended-ref for examples on how to declare AM handlers here
  (for internal conduit use in bootstrapping, job management, etc.)
*/
static gasnet_handlerentry_t const gasnetc_handlers[] = {
  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */
