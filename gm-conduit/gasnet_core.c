/* $Id: gasnet_core.c,v 1.16 2002/08/08 06:53:26 csbell Exp $
 * $Date: 2002/08/08 06:53:26 $
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

gasnet_seginfo_t *gasnetc_seginfo = NULL;

int gasnetc_init_done = 0;   /*  true after init */
int gasnetc_attach_done = 0; /*  true after attach */

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

#ifdef GASNETI_THREADS
pthread_mutex_t _gasnetc_lock_gm = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t _gasnetc_lock_reqfifo = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t _gasnetc_lock_amreq = PTHREAD_MUTEX_INITIALIZER;
#endif
gasnetc_state_t _gmc;

void gasnetc_checkinit() {
  if (!gasnetc_init_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
}

void gasnetc_checkattach() {
  if (!gasnetc_attach_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
}

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  assert(gm_min_size_for_length(GASNETC_AM_MEDIUM_MAX) <= GASNETC_AM_SIZE);
  assert(gm_min_size_for_length(GASNETC_AM_LONG_REPLY_MAX) <= GASNETC_AM_SIZE);
  assert(gm_max_length_for_size(GASNETC_AM_SIZE) <= GASNETC_AM_PACKET);
  assert(gm_max_length_for_size(GASNETC_SYS_SIZE) <= GASNETC_AM_PACKET);
  assert(GASNETC_AM_MEDIUM_MAX <= (uint16_t)(-1));
  assert(GASNETC_AM_MAX_HANDLERS >= 256);
  return;
}

static int gasnetc_init(int *argc, char ***argv) {

  /* check system sanity */
  gasnetc_check_config();

  if (gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  #if DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

#ifdef HAVE_GEXEC
#error GEXEC support not implemented yet
#else
  if (gasnetc_gmpiconf_init() != GASNET_OK)
	  GASNETI_RETURN_ERRR(RESOURCE, "GMPI-based init failed");
  gasnetc_sendbuf_init();
#endif

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    { 
      size_t	segsize = (unsigned) GASNETC_MMAP_INITIAL_SIZE;

      if (gasnetc_mmap_segment_search(&_gmc.segment_mmap, segsize, segsize/2) 
          != GASNET_OK)
	      gasneti_fatalerror("Could not find any segment using mmap");
      _gmc.segment_base = _gmc.segment_mmap.addr;
      GASNETC_DPRINTF(("mmap segment %d bytes at 0x%x\n", 
          (unsigned int) _gmc.segment_mmap.size, 
	  (uintptr_t) _gmc.segment_mmap.addr) );

      /* after gather_MaxSegment,
       * _gmc.segment_base holds the highest base of the job (the "new"
       *                   segbase) since we guarentee alignment.
       * _gmc.segment_mmap.addr holds *this* node's mmap base
       * _gmc.segment_mmap.size holds *this* node's mmap size
       */
      gasnetc_MaxGlobalSegmentSize = 
	  gasnetc_gather_MaxSegment(_gmc.segment_base, 
	      _gmc.segment_mmap.size);

      gasnetc_MaxLocalSegmentSize = (uintptr_t)_gmc.segment_mmap.addr + 
	      (uintptr_t)segsize - (uintptr_t)_gmc.segment_base;

      /*  grab GM buffers and make sure we have the maximum amount possible */
      while (_gmc.stoks.hi != 0) {
        if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
          gasneti_fatalerror("Unexpected message during bootstrap");
      }
    }
  #elif defined(GASNET_SEGMENT_EVERYTHING)
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
  #else
    #error Bad segment config
  #endif

  gasnetc_init_done = 1;

  return GASNET_OK;
}

extern uintptr_t gasnetc_getMaxLocalSegmentSize() {
  GASNETC_CHECKINIT();
  return gasnetc_MaxLocalSegmentSize;
}
extern uintptr_t gasnetc_getMaxGlobalSegmentSize() {
  GASNETC_CHECKINIT();
  return gasnetc_MaxGlobalSegmentSize;
}
/* ------------------------------------------------------------------------------------ */
static char checkuniqhandler[256] = { 0 };
static int gasnetc_reghandlers(gasnet_handlerentry_t *table, int numentries,
                               int lowlimit, int highlimit,
                               int dontcare, int *numregistered) {
  int i, retval;
  *numregistered = 0;
  for (i = 0; i < numentries; i++) {
    int newindex;

    if (table[i].index && dontcare) continue;
    else if (table[i].index) newindex = table[i].index;
    else { /* deterministic assignment of dontcare indexes */
      for (newindex = lowlimit; newindex <= highlimit; newindex++) {
        if (!checkuniqhandler[newindex]) break;
      }
      if (newindex > highlimit) {
        char s[255];
        sprintf(s,"Too many handlers. (limit=%i)", highlimit - lowlimit + 1);
        GASNETI_RETURN_ERRR(BAD_ARG, s);
      }
    }

    /*  ensure handlers fall into the proper range of pre-assigned values */
    if (newindex < lowlimit || newindex > highlimit) {
      char s[255];
      sprintf(s, "handler index (%i) out of range [%i..%i]", newindex, lowlimit, highlimit);
      GASNETI_RETURN_ERRR(BAD_ARG, s);
    }

    /* discover duplicates */
    if (checkuniqhandler[newindex] != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "handler index not unique");
    checkuniqhandler[newindex] = 1;

    /* register the handler */
    retval = gasnetc_AM_SetHandler((gasnet_handler_t) newindex, table[i].fnptr);
    if (retval != GASNET_OK)
        GASNETI_RETURN_ERRR(RESOURCE, "AM_SetHandler() failed while registering core handlers");

    if (dontcare) table[i].index = newindex;
    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries, 
		          uintptr_t segsize, uintptr_t minheapoffset) {
  size_t pagesize;
  int retval = GASNET_OK, i = 0;

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%i, minheapoffset=%i)",
                          numentries, (int)segsize, (int)minheapoffset));

  if (!gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
  if (gasnetc_attach_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    pagesize = gasneti_getSystemPageSize();
    if ((segsize % pagesize) != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize not page-aligned");
    if (segsize > gasnetc_getMaxLocalSegmentSize()) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize too large");
    if ((minheapoffset % pagesize) != 0) /* round up the minheapoffset to page sz */
      minheapoffset = ((minheapoffset / pagesize) + 1) * pagesize;
  #else
    segsize = 0;
    minheapoffset = 0;
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  register handlers */
  gasnetc_AM_InitHandler();
  { /*  core API handlers */
    gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *)gasnetc_get_handlertable();
    int len = 0;
    int numreg = 0;
    assert(ctable);
    while (ctable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(ctable, len, 1, 99, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
    assert(numreg == len);
  }

  { /*  extended API handlers */
    gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
    int len = 0;
    int numreg = 0;
    assert(etable);
    while (etable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(etable, len, 100, 199, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
    assert(numreg == len);
  }

  if (table) { /*  client handlers */
    int numreg1 = 0;
    int numreg2 = 0;

    /*  first pass - assign all fixed-index handlers */
    if (gasnetc_reghandlers(table, numentries, 200, 255, 0, &numreg1) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

    /*  second pass - fill in dontcare-index handlers */
    if (gasnetc_reghandlers(table, numentries, 200, 255, 1, &numreg2) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

    assert(numreg1 + numreg2 == numentries);
  }

  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /*  (...) catch fatal signals and convert to SIGQUIT */

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
     hold/resume interrupts functions are operational yet */
  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    if (segsize == 0) { /* no segment */
      _gmc.segment_base = NULL;
      if (gasnetc_munmap_segment(&_gmc.segment_mmap) != GASNET_OK)
	      gasneti_fatalerror("could not unmap initial mmap segment");
	      
      _gmc.segment_mmap.addr = NULL;
      _gmc.segment_mmap.size = 0;
    }
    else if (segsize == (uintptr_t) _gmc.segment_mmap.size)	/* segsize == maxlocal */
      _gmc.segment_base = (void *) _gmc.segment_mmap.size;
    else {
      if (gasnetc_munmap_segment(&_gmc.segment_mmap) != GASNET_OK)
	      gasneti_fatalerror("could not unmap initial mmap segment");
      _gmc.segment_mmap.size = segsize;
      if (gasnetc_mmap_segment(&_gmc.segment_mmap) != GASNET_OK)
	      gasneti_fatalerror("could not re-map segment after unmapping initial segment");
      _gmc.segment_base = (void *) _gmc.segment_mmap.addr;
    }
  #else
    /* GASNET_SEGMENT_EVERYTHING */
    _gmc.segment_base = _gmc.segment_mmap.addr = (void *)0;
    segsize = _gmc.segment_mmap.size = (uintptr_t)-1;
  #endif
  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
     hold/resume interrupts functions are operational yet */
  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));
  memset(gasnetc_seginfo, 0, gasnetc_nodes*sizeof(gasnet_seginfo_t));
  gasnetc_seginfo[gasnetc_mynode].addr = _gmc.segment_base;
  gasnetc_seginfo[gasnetc_mynode].size = segsize;

  /* GASNet GM always has aligned segments, we can safely assume all segbases
   * are aligned at the same address
   */
  for (i = 0; i < gasnetc_nodes; i++)
    gasnetc_seginfo[i].addr = _gmc.segment_base;

  if (gasnetc_gather_seginfo(gasnetc_seginfo) != GASNET_OK)
	      gasneti_fatalerror("could not gather job-wide seginfo");

#ifdef TRACE
  for (i = 0; i < gasnetc_nodes; i++)
      GASNETI_TRACE_PRINTF(C, ("SEGINFO at %4d (0x%x, %d)", i,
        (uintptr_t) gasnetc_seginfo[i].addr, (unsigned int) gasnetc_seginfo[i].size) );
#endif

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasnetc_attach_done = 1;

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

  gasnetc_SysBarrier();
  gasnete_init();
  gasnetc_SysBarrier();

  /*  grab GM buffers and make sure we have the maximum amount possible */
  while (_gmc.stoks.hi != 0) {
    if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
      gasneti_fatalerror("Unexpected message during bootstrap");
  }
  gasnetc_provide_receive_buffers();


  GASNETI_TRACE_PRINTF(C,
  	("Send tokens: lo=%3d, hi=%3d, tot=%3d, max=%3d\n",
	_gmc.stoks.lo, _gmc.stoks.hi, _gmc.stoks.total, _gmc.stoks.max));
  GASNETI_TRACE_PRINTF(C,
  	("Recv tokens: lo=%3d, hi=%3d, tot=%3d, max=%3d\n",
	_gmc.rtoks.lo, _gmc.rtoks.hi, _gmc.rtoks.total, _gmc.rtoks.max));

  return GASNET_OK;
}

extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasneti_trace_init();
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern void gasnetc_exit(int exitcode) {
  gasnetc_sendbuf_finalize();
  gasneti_trace_finish();
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
  Misc. Core handlers
*/
void
gasnetc_am_medcopy_inner(gasnet_token_t token, void *addr, size_t nbytes, 
			 void *dest)
{
	memcpy(dest, addr, nbytes);
	printf("am_medcopy (%p, %p, %d)\n", dest, addr, nbytes); fflush(stdout);
}
MEDIUM_HANDLER(gasnetc_am_medcopy,1,2,
              (token,addr,nbytes, UNPACK(a0)    ),
              (token,addr,nbytes, UNPACK2(a0, a1)));
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
  if ((void *)token == (void *)-1) {
	  *srcindex = gasnetc_mynode;
	  return GASNET_OK;
  }
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
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = 1;
  if (dest == gasnetc_mynode) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], (void *) -1, argbuf, numargs);
  }
  else {
    bufd = gasnetc_AMRequestPool_block();
    len = gasnetc_write_AMBufferShort(bufd->sendbuf, handler, numargs, 
		    argptr, GASNETC_AM_REQUEST);
    gasnetc_tokensend_AMRequest(bufd->sendbuf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_AMRequest, (void *)bufd, 0);
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
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  assert(nbytes <= GASNETC_AM_MEDIUM_MAX);
  retval = 1;
  if (dest == gasnetc_mynode) { /* local handler */
    void *loopbuf;
    int argbuf[GASNETC_AM_MAX_ARGS];
    loopbuf = gasnetc_alloca(nbytes);
    memcpy(loopbuf, source_addr, nbytes);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler], (void *) -1,
				argbuf, numargs, loopbuf, nbytes);
  }
  else {
    bufd = gasnetc_AMRequestPool_block();
    len = gasnetc_write_AMBufferMedium(bufd->sendbuf, handler, numargs, argptr, 
		 nbytes, source_addr, GASNETC_AM_REQUEST);
    gasnetc_tokensend_AMRequest(bufd->sendbuf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_AMRequest, (void *)bufd, 0);
    /* GASNETC_AMTRACE_RequestMedium(Send); */
  }

  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

#define gasnetc_ispinned(dest, dest_addr, nbytes)	(1)

GASNET_INLINE_MODIFIER(gasnetc_AMRequestLongM_inner)
void
gasnetc_AMRequestLongM_inner(gasnet_node_t dest, gasnet_handler_t handler,
		void *source_addr, size_t nbytes, void *dest_addr, int numargs, 
		va_list argptr)
{
	int	bytes_left = nbytes;
	int	port, id, len;
	uint8_t	*psrc, *pdest;
	gasnetc_bufdesc_t	*bufd;

	psrc  = (uint8_t *) source_addr;
	pdest = (uint8_t *) dest_addr;
	port  = gasnetc_portid(dest);
	id    = gasnetc_nodeid(dest);

	if (gasnetc_ispinned(dest, dest_addr, nbytes)) {
		while (bytes_left >GASNETC_AM_LEN-GASNETC_LONG_OFFSET) {
			bufd = gasnetc_AMRequestPool_block();
			gasnetc_write_AMBufferBulk(bufd->sendbuf, 
				psrc, GASNETC_AM_LEN);
			gasnetc_tokensend_AMRequest(bufd->sendbuf, 
			   GASNETC_AM_LEN, id, port, gasnetc_callback_AMRequest,
			   (void *) bufd, (uintptr_t) pdest);
			psrc += GASNETC_AM_LEN;
			pdest += GASNETC_AM_LEN;
			bytes_left -= GASNETC_AM_LEN;
		}
		bufd = gasnetc_AMRequestPool_block();
		len =
		    gasnetc_write_AMBufferLong(bufd->sendbuf, 
		        handler, numargs, argptr, nbytes, source_addr, 
			(uintptr_t) dest_addr, GASNETC_AM_REQUEST);
		if (bytes_left > 0) {
			gasnetc_write_AMBufferBulk(
				bufd->sendbuf+GASNETC_LONG_OFFSET, 
				psrc, bytes_left);
			gasnetc_tokensend_AMRequest(
			    bufd->sendbuf+GASNETC_LONG_OFFSET,
			    bytes_left, id, port, 
			    gasnetc_callback_AMRequest_NOP, NULL, 
			    (uintptr_t) pdest);
		}
		gasnetc_tokensend_AMRequest(bufd->sendbuf, len, id, 
		    port, gasnetc_callback_AMRequest, (void *)bufd, 0);
	}
	else {
		int32_t	dest_addr_ptr[2];
		int	long_len;

		while (bytes_left >GASNETC_AM_LEN-GASNETC_LONG_OFFSET) {
			bufd = gasnetc_AMRequestPool_block();
			GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) pdest);
			len = gasnetc_write_AMBufferMedium(bufd->sendbuf,
			    gasneti_handleridx(gasnetc_am_medcopy), 
				GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
				gasnet_AMMaxMedium(), (void *) psrc, GASNETC_AM_REQUEST);
			gasnetc_tokensend_AMRequest(bufd->sendbuf, len, id, 
			    port, gasnetc_callback_AMRequest, (void *) bufd, 0);
			psrc += gasnet_AMMaxMedium();
			pdest += gasnet_AMMaxMedium();
			bytes_left -= gasnet_AMMaxMedium();
		}
		bufd = gasnetc_AMRequestPool_block();
		long_len =
		    gasnetc_write_AMBufferLong(bufd->sendbuf, 
		        handler, numargs, argptr, nbytes, source_addr, 
			(uintptr_t) dest_addr, GASNETC_AM_REQUEST);
		if (bytes_left > 0) {
			uintptr_t	pbuf;
			pbuf = (uintptr_t) bufd->sendbuf + (uintptr_t) long_len;
			GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) pdest);
			len = gasnetc_write_AMBufferMedium((void *)pbuf,
		    	    gasneti_handleridx(gasnetc_am_medcopy), 
			    GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
			    bytes_left, (void *) psrc, GASNETC_AM_REQUEST);
			gasnetc_tokensend_AMRequest((void *)pbuf, len, id, port,
			    gasnetc_callback_AMRequest_NOP, NULL, 0);
		}
		gasnetc_tokensend_AMRequest(bufd->sendbuf, long_len, id, port, 
		    gasnetc_callback_AMRequest, (void *)bufd, 0);
	}
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...)
{
	int	retval;
	va_list	argptr;

	gasnetc_bufdesc_t	*bufd;
	GASNETC_CHECKINIT();
  
	gasnetc_boundscheck(dest, dest_addr, nbytes);
	assert(nbytes <= gasnet_AMMaxLongRequest());
	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)< ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	    ((uintptr_t)dest_addr) + nbytes > 
	        ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
	GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
	va_start(argptr, numargs); /*  pass in last argument */

	retval = 1;
	assert(nbytes <= GASNETC_AM_LONG_REQUEST_MAX);

	if (dest == gasnetc_mynode) {
		int	argbuf[GASNETC_AM_MAX_ARGS];

		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_AMPAYLOAD_WRITE(dest_addr, source_addr, nbytes);
		GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *) -1, 
		    argbuf, numargs, dest_addr, nbytes);
	}
	else {
		/* XXX assert(GASNET_LONG_OFFSET >= LONG_HEADER) */
		gasnetc_AMRequestLongM_inner(dest, handler, source_addr, 
		    nbytes, dest_addr, numargs, argptr);
	}

	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...)
{
	int	retval;
	va_list	argptr;

	gasnetc_bufdesc_t	*bufd;
	GASNETC_CHECKINIT();
	
	gasnetc_boundscheck(dest, dest_addr, nbytes);
	assert(nbytes <= gasnet_AMMaxLongRequest());
	if_pf (dest >= gasnetc_nodes)
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)<((uintptr_t)gasnetc_seginfo[dest].addr) ||
	    ((uintptr_t)dest_addr) + nbytes > 
	        ((uintptr_t)gasnetc_seginfo[dest].addr)+gasnetc_seginfo[dest].size) 
		GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
	GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
	va_start(argptr, numargs); /*  pass in last argument */
	retval = 1;

	if (!gasnetc_ispinned(gasnetc_mynode, source_addr, nbytes)) {
		gasnetc_AMRequestLongM_inner(dest, handler, source_addr, 
		    nbytes, dest_addr, numargs, argptr);
	}
	else {
		uint16_t port, id;
		int	 len;

		port = gasnetc_portid(dest);
		id   = gasnetc_nodeid(dest);
		bufd = gasnetc_AMRequestPool_block();
		len =
		    gasnetc_write_AMBufferLong(bufd->sendbuf, 
		        handler, numargs, argptr, nbytes, source_addr, 
			(uintptr_t) dest_addr, GASNETC_AM_REQUEST);
		/* send the DMA first */
		gasnetc_tokensend_AMRequest(source_addr, nbytes, id, 
		    port, gasnetc_callback_AMRequest_NOP, NULL, 
		    (uintptr_t) dest_addr);
		/* followed by the Long Header */
		gasnetc_tokensend_AMRequest(bufd->sendbuf, len, id, 
		    port, gasnetc_callback_AMRequest, (void *)bufd, 0);
	}

	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;

  va_start(argptr, numargs); /*  pass in last argument */
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  retval = 1;
  if ((void *)token == (void*)-1) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    /* GASNETC_AMTRACE_ReplyShort(Loopbk); */
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], (void *) token, argbuf, numargs);
  }
  else {
    bufd = gasnetc_bufdesc_from_token(token);
    bufd->len = gasnetc_write_AMBufferShort(bufd->sendbuf, handler, 
		    numargs, argptr, GASNETC_AM_REPLY);
  
    GASNETC_GM_MUTEX_LOCK;
    if (gasnetc_token_hi_acquire()) {
       /* GASNETC_AMTRACE_ReplyShort(Send); */
       gasnetc_gm_send_bufd(bufd);
    } else {
       /* GASNETC_AMTRACE_ReplyShort(Queued); */
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

  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  retval = 1;
  assert(nbytes <= GASNETC_AM_MEDIUM_MAX);
  if ((void *)token == (void *)-1) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    void *loopbuf;
    loopbuf = gasnetc_alloca(nbytes);
    memcpy(loopbuf, source_addr, nbytes);
    /* GASNETC_AMTRACE_ReplyMedium(Loopbk); */
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler], (void *) token,
				argbuf, numargs, loopbuf, nbytes);
  }
  else {
    if_pf (nbytes > GASNETC_AM_MEDIUM_MAX) 
	    GASNETI_RETURN_ERRR(BAD_ARG,"AMMedium Payload too large");
    bufd = gasnetc_bufdesc_from_token(token);
    bufd->len = 
	    gasnetc_write_AMBufferMedium(bufd->sendbuf, handler, numargs, 
                    argptr, nbytes, source_addr, GASNETC_AM_REPLY);
    GASNETC_GM_MUTEX_LOCK;
    if (gasnetc_token_hi_acquire()) {
       /* GASNETC_AMTRACE_ReplyMedium(Send); */
       gasnetc_gm_send_bufd(bufd); 
    } else {
       /* GASNETC_AMTRACE_ReplyMedium(Queued); */
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
		int numargs, ...)
{
	int	retval;
	int	hdr_len;
	va_list	argptr;
	gasnet_node_t		dest;
	gasnetc_bufdesc_t 	*bufd;

	retval = gasnet_AMGetMsgSource(token, &dest);
	if (retval != GASNET_OK) GASNETI_RETURN(retval);
	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)< ((uintptr_t)gasnetc_seginfo[dest].addr)||
	    ((uintptr_t)dest_addr) + nbytes > 
	    ((uintptr_t)gasnetc_seginfo[dest].addr)+gasnetc_seginfo[dest].size)
		GASNETI_RETURN_ERRR(BAD_ARG,
		    "destination address out of segment range");

	va_start(argptr, numargs); /*  pass in last argument */
	GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,
	    numargs);
	retval = 1;
	assert(nbytes <= GASNETC_AM_LONG_REPLY_MAX);
	if ((void *)token == (void *)-1) {
		int	argbuf[GASNETC_AM_MAX_ARGS];
		GASNETC_AMTRACE_ReplyLong(Loopbk);
		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *)token, 
		    argbuf, numargs, dest_addr, nbytes);
	}
	else {
		uintptr_t	pbuf;
		unsigned int	len;
	
    		bufd = gasnetc_bufdesc_from_token(token);
		if (gasnetc_ispinned(dest, dest_addr, nbytes)) {
			pbuf = (uintptr_t) bufd->sendbuf + 
			    (uintptr_t) GASNETC_LONG_OFFSET;
			len =
			    gasnetc_write_AMBufferLong(bufd->sendbuf, handler,
			        numargs, argptr, nbytes, source_addr, 
				(uintptr_t) dest_addr, GASNETC_AM_REPLY);
			gasnetc_write_AMBufferBulk((void *)pbuf, source_addr, 
			    nbytes);
			bufd->len = len;
			bufd->rdma_off = GASNETC_LONG_OFFSET; 
			bufd->rdma_len = nbytes;
			bufd->dest_addr = (uintptr_t) dest_addr;
		}
		else {
			int32_t	dest_addr_ptr[2];
			int	long_len;
	
			long_len =
			    gasnetc_write_AMBufferLong(bufd->sendbuf, 
			        handler, numargs, argptr, nbytes, source_addr, 
				(uintptr_t) dest_addr, GASNETC_AM_REQUEST);
			pbuf = (uintptr_t)bufd->sendbuf + (uintptr_t) long_len;
			GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) dest_addr);
			len = gasnetc_write_AMBufferMedium((void *)pbuf,
			    gasneti_handleridx(gasnetc_am_medcopy), GASNETC_ARGPTR_NUM,
			    (va_list) dest_addr_ptr, nbytes, (void *) source_addr, 
			    GASNETC_AM_REPLY);
			bufd->len = long_len;
			bufd->rdma_off = long_len; 
			bufd->rdma_len = len;
			bufd->dest_addr = 0;
		}

		GASNETC_GM_MUTEX_LOCK;
	
		if (gasnetc_token_hi_acquire()) {
	        	gasnetc_gm_send_bufd(bufd);
			if (gasnetc_token_hi_acquire()) {
				GASNETC_AMTRACE_ReplyLong(Send);
				gasnetc_gm_send_bufd(bufd);
			} 
			else {
				GASNETC_AMTRACE_ReplyLong(Queued);
				gasnetc_fifo_insert(bufd);
			}
		} 
		else {
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
    #if defined(STATS) || defined(TRACE)
      gasneti_stattime_t startlock = GASNETI_STATTIME_NOW_IFENABLED(L);
    #endif
    #if GASNETC_HSL_SPINLOCK
      do {
        retval = pthread_mutex_trylock(&(hsl->lock));
      } while (retval == EBUSY);
    #else
        retval = pthread_mutex_lock(&(hsl->lock));
    #endif
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_lock(), pthread_mutex_lock()=%i",strerror(retval));
    #if defined(STATS) || defined(TRACE)
      hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
      GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
    #endif
  }
  #elif defined(STATS) || defined(TRACE)
    hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
    GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, 0);
  #endif

  /* (###) conduits with interrupt-based handler dispatch need to add code here to 
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

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

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
