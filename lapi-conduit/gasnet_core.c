/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2002/11/22 05:50:44 $
 * $Revision: 1.7 $
 * Description: GASNet lapi conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

/* =======================================================================
 * LAPI Conduit Implementation for IBM SP.
 * Michael Welcome
 * Lawrence Berkeley National Laboratory
 * mlwelcome@lbl.gov
 * November, 2002
 * =======================================================================
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

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;
gasnet_seginfo_t *gasnetc_seginfo = NULL;

static int gasnetc_init_done = 0; /*  true after init */
static int gasnetc_attach_done = 0; /*  true after attach */
void gasnetc_checkinit() {
    if (!gasnetc_init_done)
	gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
}
void gasnetc_checkattach() {
    if (!gasnetc_attach_done)
	gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
}


/* -------------------------------------------------------------------
 * Begin: LAPI specific variables
 * -------------------------------------------------------------------
 */
lapi_handle_t  gasnetc_lapi_context;
lapi_info_t    gasnetc_lapi_info;
int            gasnetc_max_lapi_uhdr_size;
int            gasnetc_max_lapi_data_size;

/* NOTE: this is not thread-safe */
int            gasnetc_lapi_errno;
char           gasnetc_lapi_msg[LAPI_MAX_ERR_STRING];

/* This is the official core AM handler table.  All registered
 * entries go here
 */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS] = { NULL };
void** gasnetc_remote_req_hh = NULL;
void** gasnetc_remote_reply_hh = NULL;
gasnetc_lapimode_t gasnetc_lapi_default_mode = gasnetc_Interrupt;

#if GASNETC_USE_IBH
volatile int gasnetc_interrupt_held[GASNETC_MAX_THREAD] = { 0 };
#endif

gasnetc_uhdr_freelist_t gasnetc_uhdr_freelist;

/* -------------------------------------------------------------------
 * End: LAPI specific variables
 * -------------------------------------------------------------------
 */
/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
    /* (###) add code to do some sanity checks on the number of nodes, handlers
     * and/or segment sizes */ 
}

static void gasnetc_bootstrapBarrier() {
    /* (###) add code here to implement an external barrier 
       this barrier should not rely on AM or the GASNet API because it's used 
       during bootstrapping before such things are fully functional
       It need not be particularly efficient, because we only call it a few times
       and only during bootstrapping - it just has to work correctly
       If your underlying spawning or batch system provides barrier functionality,
       that would probably be a good choice for this
    */
    /* MLW: All calls to bootstrapBarrier occur after LAPI_init and thus
     * we can safely use the LAPI global fence operations
     */
    GASNETC_LCHECK(LAPI_Gfence(gasnetc_lapi_context));
}

/* --------------------------------------------------------------------------
 * NOTE: the POE job control system on the IBM SP guarantees that argc 
 *       and argv, as well as the environment variables, are distributed 
 *       to all tasks in the parallel jobs.  
 * --------------------------------------------------------------------------
 */
static int gasnetc_init(int *argc, char ***argv) {
    int task_id;
    int num_tasks;
    unsigned int max_uhdr_payload;
    unsigned int max_payload;

    /*  check system sanity */
    gasnetc_check_config();

    if (gasnetc_init_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

    if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

#if DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
#endif

    /* (###) add code here to bootstrap the nodes for your conduit */
    bzero(&gasnetc_lapi_info, sizeof(lapi_info_t));
    gasnetc_lapi_info.err_hndlr = gasnetc_lapi_err_handler;
    GASNETC_LCHECK(LAPI_Init(&gasnetc_lapi_context, &gasnetc_lapi_info));


    /* get task number and number of tasks in job */
    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, TASK_ID, &task_id));
    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, NUM_TASKS, &num_tasks));
    if (num_tasks < 0 || num_tasks > GASNET_MAXNODES) {
	gasneti_fatalerror("Invalid number of LAPI tasks: %d, must be < %d",
			   num_tasks,GASNET_MAXNODES);
    }
    if (task_id < 0 || task_id > GASNET_MAXNODES) {
	gasneti_fatalerror("Invalid LAPI id: %d, must be < %d",
			   task_id,GASNET_MAXNODES);
    }
    gasnetc_mynode = (gasnet_node_t)task_id;
    gasnetc_nodes = (gasnet_node_t)num_tasks;

    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, MAX_UHDR_SZ, &gasnetc_max_lapi_uhdr_size));
    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, MAX_DATA_SZ, &gasnetc_max_lapi_data_size));
    max_uhdr_payload = gasnetc_max_lapi_uhdr_size - sizeof(gasnetc_token_t);
    if (max_uhdr_payload < GASNETC_UHDR_SIZE) {
	gasneti_fatalerror("Must recompile with GASNETC_UHDR_SIZE <= %d",
			   max_uhdr_payload);
    }
    if (gasnetc_max_lapi_data_size < GASNETC_AM_MAX_LONG) {
	gasneti_fatalerror("Must recompile with GASNETC_AM_MAX_LONG <= %d",
			   gasnetc_max_lapi_data_size);
    }
    max_payload = GASNETC_UHDR_SIZE - sizeof(gasnetc_token_t);
    if (GASNETC_AM_MAX_MEDIUM > max_payload) {
	fprintf(stderr,"WARNING: MAX_MEDIUM %d > max_payload %d",
		GASNETC_AM_MAX_MEDIUM,max_payload);
    }
    

    /* Do we want to use polling or interrupt mode?  How to
     * communicate this?  Env variable?
     */
    if (gasnetc_lapi_default_mode == gasnetc_Interrupt) {
	/* turn on interrupt mode */
	GASNETC_LCHECK(LAPI_Senv(gasnetc_lapi_context, INTERRUPT_SET, 1));
    } else {
	/* polling mode, turn off interrupts */
	GASNETC_LCHECK(LAPI_Senv(gasnetc_lapi_context, INTERRUPT_SET, 0));
    }

    /* collect remote addresses of header handler function */
    gasnetc_remote_req_hh = (void**)gasneti_malloc_inhandler(num_tasks*sizeof(void*));
    gasnetc_remote_reply_hh = (void**)gasneti_malloc_inhandler(num_tasks*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnetc_lapi_AMreq_hh,
				     gasnetc_remote_req_hh));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnetc_lapi_AMreply_hh,
				     gasnetc_remote_reply_hh));

#if DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
	    gasnetc_mynode, gasnetc_nodes); fflush(stderr);
#endif

#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    { 
	/* Add code here to determine optimistic maximum segment size and
	 * the MIN(MaxLocalSegmentSize) over all nodes 
	gasnetc_MaxLocalSegmentSize = ###;
	gasnetc_MaxGlobalSegmentSize = ###;
	 * - OR -
	 * it may be appropriate to use gasneti_segmentInit() here to set 
	   gasnetc_MaxLocalSegmentSize and gasnetc_MaxGlobalSegmentSize,
	   if your conduit can use memory anywhere in the address space
	   (you may want to tune GASNETI_MMAP_MAX_SIZE to limit the max size)
	*/
	/* On the SP, mmaped regions are allocated in segments distinct from
	 * static, stack and heap data.  gasneti_segmentInit should work
	 * well.
	 */
	gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,&gasnetc_MaxGlobalSegmentSize,
			    (uintptr_t)-1,gasnetc_nodes,gasnetc_lapi_exchange);
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

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
    int retval = gasnetc_init(argc, argv);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasneti_trace_init();
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
    int i;
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
	/* (###) add code here to register table[i].fnptr 
	   on index (gasnet_handler_t)newindex */
	gasnetc_handler[newindex] = table[i].fnptr;
	GASNETI_TRACE_PRINTF(C,("Registered handler %x at index %d",
				table[i].fnptr,newindex));

	if (dontcare) table[i].index = newindex;
	(*numregistered)++;
    }
    return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
    size_t pagesize = gasneti_getSystemPageSize();
    void *segbase = NULL;
  
    GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%i, minheapoffset=%i)",
			    numentries, (int)segsize, (int)minheapoffset));

    if (!gasnetc_init_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
    if (gasnetc_attach_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

    /*  check argument sanity */
#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
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
    { /*  core API handlers */
	gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *)gasnetc_get_handlertable();
	int len = 0;
	int numreg = 0;
	assert(ctable);
	while (ctable[len].fnptr) len++; /* calc len */
	if (gasnetc_reghandlers(ctable, len, 1, 63, 0, &numreg) != GASNET_OK)
	    GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
	assert(numreg == len);
    }

    { /*  extended API handlers */
	gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
	int len = 0;
	int numreg = 0;
	assert(etable);
	while (etable[len].fnptr) len++; /* calc len */
	if (gasnetc_reghandlers(etable, len, 63, 127, 0, &numreg) != GASNET_OK)
	    GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
	assert(numreg == len);
    }

    if (table) { /*  client handlers */
	int numreg1 = 0;
	int numreg2 = 0;

	/*  first pass - assign all fixed-index handlers */
	if (gasnetc_reghandlers(table, numentries, 128, 255, 0, &numreg1) != GASNET_OK)
	    GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

	/*  second pass - fill in dontcare-index handlers */
	if (gasnetc_reghandlers(table, numentries, 128, 255, 1, &numreg2) != GASNET_OK)
	    GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

	assert(numreg1 + numreg2 == numentries);
    }

    /* ------------------------------------------------------------------------------------ */
    /*  register fatal signal handlers */

    /* catch fatal signals and convert to SIGQUIT */
    gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

    /*  (###) register any custom signal handlers required by your conduit 
     *        (e.g. to support interrupt-based messaging)
     */

    /* ------------------------------------------------------------------------------------ */
    /*  register segment  */

    /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
       hold/resume interrupts functions are operational yet */
    gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));

#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    if (segsize == 0) segbase = NULL; /* no segment */
    else {
	/* (###) add code here to choose and register a segment 
	   (ensuring alignment across all nodes if this conduit sets GASNET_ALIGNED_SEGMENTS==1) 
	   you can use gasneti_segmentAttach() here if you used gasneti_segmentInit() above
	*/
	assert(segsize % pagesize == 0);
	gasneti_segmentAttach(segsize,minheapoffset,gasnetc_seginfo,gasnetc_lapi_exchange);
	segbase = gasnetc_seginfo[gasnetc_mynode].addr;
	segsize = gasnetc_seginfo[gasnetc_mynode].size;
	assert(((uintptr_t)segbase) % pagesize == 0);
	assert(segsize % pagesize == 0);
    }
#else
    /* GASNET_SEGMENT_EVERYTHING */
    {
	int i;
	for (i=0;i<gasnetc_nodes;i++) {
	    gasnetc_seginfo[i].addr = (void *)0;
	    gasnetc_seginfo[i].size = (uintptr_t)-1;
	}
	segbase = gasnetc_seginfo[gasnetc_mynode].addr;
	segsize = gasnetc_seginfo[gasnetc_mynode].size;
    }
#endif

    /* ------------------------------------------------------------------------------------ */
    /*  primary attach complete */
    gasnetc_attach_done = 1;
    gasnetc_bootstrapBarrier();

    GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

    {
	int i;
	for (i = 0; i < gasnetc_nodes; i++) {
	    GASNETI_TRACE_PRINTF(C,("For node %d seginfo.addr = %x seginfo.size = %d",
				    i,gasnetc_seginfo[i].addr,
				    gasnetc_seginfo[i].size));
	}
    }

#if GASNET_ALIGNED_SEGMENTS == 1
    { int i; /*  check that segments are aligned */
    for (i=0; i < gasnetc_nodes; i++) {
        if (gasnetc_seginfo[i].size != 0 && gasnetc_seginfo[i].addr != segbase) 
	    gasneti_fatalerror("Failed to acquire aligned segments for GASNET_ALIGNED_SEGMENTS");
    }
    }
#endif

    /* Init the uhdr buffer free list used in GASNET AM calls */
    gasnetc_uhdr_init(GASNETC_UHDR_INIT_CNT);

    gasnete_init(); /* init the extended API */

    /* ensure extended API is initialized across nodes */
    gasnetc_bootstrapBarrier();

    return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_exit(int exitcode) {

    GASNETI_TRACE_PRINTF(C,("GASNETC_EXIT: UHDR_BUF HWM %d, numfree %d, numalloc %d",
			    gasnetc_uhdr_freelist.high_water_mark,
			    gasnetc_uhdr_freelist.numfree,
			    gasnetc_uhdr_freelist.numalloc));

    gasneti_trace_finish();

    /* (###) add code here to terminate the job across all nodes with exit(exitcode) */
    GASNETC_LCHECK(LAPI_Term(gasnetc_lapi_context));
    exit(exitcode);
    
    abort();
}

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
    GASNETC_CHECKATTACH();
    assert(gasnetc_seginfo && seginfo_table);
    if (!gasnetc_attach_done) GASNETI_RETURN_ERR(NOT_INIT);
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
    GASNETC_CHECKATTACH();
    if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
    if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

    /* (###) add code here to write the source index into sourceid */
    sourceid = ((gasnetc_token_t*)token)->sourceId;

    assert(sourceid < gasnetc_nodes);
    *srcindex = sourceid;
    return GASNET_OK;
}

extern int gasnetc_AMPoll() {
    int retval;
    GASNETC_CHECKATTACH();

    /* NOTE: a call to probe is not needed when LAPI is executing
     * in interrupt mode.  In that mode, polling can sometimes
     * decrease performance due to lock contention between the
     * notification thread and this thread.
     * If this funciton is called from a spinloop, LAPI should
     * be switched to POLLING mode before the AMPoll calls,
     * and switched back to the default mode afterwards.
     * We do this in the BLOCKUNTIL macro.
     */
    GASNETC_LCHECK(LAPI_Probe(gasnetc_lapi_context));

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
    lapi_cntr_t o_cntr;
    int         cur_cntr;
    gasnetc_uhdr_buf_t token_buf;
    gasnetc_token_t *token = &token_buf.token;
    int token_len;
    int i;
    uint target_node = (uint)dest;
    va_list argptr;

    GASNETC_CHECKATTACH();
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);

    token->handlerId = handler;
    token->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(token,1,gasnetc_Short,0,numargs);
    token->destLoc = (uintptr_t)NULL;
    token->dataLen = (size_t)0;
    token->uhdrLoc = (uintptr_t)NULL;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	token->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	RUN_HANDLER_SHORT(pfn,token,&token->args[0],numargs);
	GASNETI_RETURN(GASNET_OK);
    }
#endif
    
    /* only send as many much of token structure as necessary */
    token_len = offsetof(gasnetc_token_t,args) + numargs*sizeof(gasnet_handlerarg_t);
    
    /* issue the request for remote execution of the user handler */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, target_node,
			       gasnetc_remote_req_hh[target_node],
			       (void*)token, token_len, NULL, 0,
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestMediumM( 
    gasnet_node_t dest,      /* destination node */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    void *source_addr, size_t nbytes,   /* data payload */
    int numargs, ...) {
    int retval;
    lapi_cntr_t o_cntr;
    int         cur_cntr;
    gasnetc_uhdr_buf_t token_buf;
    gasnetc_token_t *token = &token_buf.token;
    int token_len;
    char *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    int i;
    uint target_node = (uint)dest;
    va_list argptr;

    GASNETC_CHECKATTACH();
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);

    token->handlerId = handler;
    token->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(token,1,gasnetc_Medium,0,numargs);
    token->destLoc = (uintptr_t)NULL;
    token->dataLen = (size_t)nbytes;
    token->uhdrLoc = (uintptr_t)NULL;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	token->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (char*)&token->args[numargs];
    token_len = offsetof(gasnetc_token_t,args) + numargs*sizeof(gasnet_handlerarg_t);
    udata_avail = sizeof(gasnetc_uhdr_buf_t) - token_len;

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,(char*)source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(token);
    }

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	char *destloc;
	if (nbytes > udata_avail) {
	    destloc = gasneti_malloc_inhandler(nbytes > 0 ? nbytes : 1);
	    memcpy(destloc,(char*)source_addr,nbytes);
	} else {
	    destloc = udata_start;
	}
	RUN_HANDLER_MEDIUM(pfn,token,&token->args[0],numargs,destloc,nbytes);
	if (nbytes > udata_avail) {
	    gasneti_free(destloc);
	}
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* issue the request for remote execution of the user handler */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, target_node,
			       gasnetc_remote_req_hh[target_node],
			       (void*)token, token_len,
			       (udata_packed ? NULL : (void*)source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
				   gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
				   void *source_addr, size_t nbytes,   /* data payload */
				   void *dest_addr,                    /* data destination on destination node */
				   int numargs, ...) {
    int retval;
    lapi_cntr_t o_cntr;
    int         cur_cntr;
    gasnetc_uhdr_buf_t token_buf;
    gasnetc_token_t *token = &token_buf.token;
    int token_len;
    char *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    int i;
    uint target_node = (uint)dest;
    va_list argptr;

    GASNETC_CHECKATTACH();
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

    GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);

    token->handlerId = handler;
    token->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(token,1,gasnetc_Long,0,numargs);
    token->destLoc = (uintptr_t)dest_addr;
    token->dataLen = (size_t)nbytes;
    token->uhdrLoc = (uintptr_t)NULL;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	token->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (char*)&token->args[numargs];
    token_len = offsetof(gasnetc_token_t,args) + numargs*sizeof(gasnet_handlerarg_t);
    udata_avail = sizeof(gasnetc_uhdr_buf_t) - token_len;

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	/* must do local copy of data from source to dest */
	memcpy((char*)dest_addr,(char*)source_addr,nbytes);
	RUN_HANDLER_LONG(pfn,token,&token->args[0],numargs,dest_addr,nbytes);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,(char*)source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(token);
    }

    /* issue the request for remote execution of the user handler */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, target_node,
			       gasnetc_remote_req_hh[target_node],
			       (void*)token, token_len,
			       (udata_packed ? NULL : (void*)source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
					gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
					void *source_addr, size_t nbytes,   /* data payload */
					void *dest_addr,                    /* data destination on destination node */
					int numargs, ...) {

    gasnetc_uhdr_buf_t *token_buf;
    gasnetc_token_t *token;
    int token_len;
    char *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    int i;
    uint target_node = (uint)dest;
    int retval;
    va_list argptr;
    GASNETC_CHECKATTACH();
  
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

    GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);

    token_buf = gasnetc_uhdr_alloc();
    token = &token_buf->token;
    token->handlerId = handler;
    token->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(token,1,gasnetc_AsyncLong,0,numargs);
    token->destLoc = (uintptr_t)dest_addr;
    token->dataLen = (size_t)nbytes;
    /* stash the location of token, so that it can be deallocated
     * in the completion handler of the corresponding GASNET reply
     * when it executes on this node.
     */
    token->uhdrLoc = (uintptr_t)token_buf;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	token->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (char*)&token->args[numargs];
    token_len = offsetof(gasnetc_token_t,args) + numargs*sizeof(gasnet_handlerarg_t);
    udata_avail = sizeof(gasnetc_uhdr_buf_t) - token_len;

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	/* must do local copy of data from source to dest */
	memcpy((char*)dest_addr,(char*)source_addr,nbytes);
	/* Note: we will deallocate the token below, just to be safe
	 * remove the address from the uhdrLoc field so that no-one
	 * else messes with it.
	 */
	token->uhdrLoc = (uintptr_t)NULL;
	RUN_HANDLER_LONG(pfn,token,&token->args[0],numargs,dest_addr,nbytes);
	gasnetc_uhdr_free(token_buf);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,(char*)source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(token);
    }
    
    /* issue the request for remote execution of the user handler */
    /* NOTE: no LAPI counters are used here, the token will be deallocated
     * later (by the completion handler when the reply handler is executed).
     * It is up to the client not to modify the source_addr data until his 
     * reply handler runs.
     */
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, target_node,
			       gasnetc_remote_req_hh[target_node],
			       (void*)token, token_len,
			       (udata_packed ? NULL : (void*)source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, NULL, NULL));
    
    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM( 
    gasnet_token_t token,       /* token provided on handler entry */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    int numargs, ...) {
    int retval;

    gasnetc_uhdr_buf_t *token_buf = (gasnetc_uhdr_buf_t *)token;
    gasnetc_token_t *t = &token_buf->token;
    uint requester = (uint)t->sourceId;
    lapi_cntr_t o_cntr;
    int token_len;
    int i, cur_cntr;

    va_list argptr;
    GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    /* we can re-use the token passed into us.  It was allocated in the
     * LAPI header handler to be large enough to contain the maximum
     * number of arguments.  Upon return from this funciton it will
     * no longer be used
     */
    GASNETC_MSG_SETFLAGS(t,0,gasnetc_Short,0,numargs);
    t->handlerId = handler;
    t->sourceId = gasnetc_mynode;
    t->destLoc = NULL;
    t->dataLen = 0;
    /* do NOT modify the contents of uhdrLoc... needed at origin */
    va_start(argptr, numargs); /*  pass in last argument */
    for (i = 0; i < numargs; i++) {
	t->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);

#if GASNETC_ENABLE_LOOPBACK
    if (requester == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	RUN_HANDLER_SHORT(pfn,token,&t->args[0],numargs);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* only send as many much of token structure as necessary */
    token_len = offsetof(gasnetc_token_t,args) + numargs*sizeof(gasnet_handlerarg_t);
    
    /* issue the request for remote execution of the user handler */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, requester,
			       gasnetc_remote_reply_hh[requester],
			       (void*)t, token_len, NULL, 0,
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyMediumM( 
    gasnet_token_t token,       /* token provided on handler entry */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    void *source_addr, size_t nbytes,   /* data payload */
    int numargs, ...) {

    int retval;
    gasnetc_uhdr_buf_t *token_buf = (gasnetc_uhdr_buf_t *)token;
    gasnetc_token_t *t = &token_buf->token;
    uint requester = (uint)t->sourceId;
    lapi_cntr_t o_cntr;
    int token_len;
    int i, cur_cntr;
    char *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    
    va_list argptr;
    GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);

    /* we can re-use the token passed into us.  It was allocated in the
     * LAPI header handler to be large enough to contain the maximum
     * number of arguments.  Upon return from this funciton it will
     * no longer be used
     */
    GASNETC_MSG_SETFLAGS(t,0,gasnetc_Medium,0,numargs);
    t->handlerId = handler;
    t->sourceId = gasnetc_mynode;
    t->destLoc = (uintptr_t)NULL;
    t->dataLen = nbytes;
    /* do NOT modify the contents of uhdrLoc... needed at origin */
    va_start(argptr, numargs); /*  pass in last argument */
    for (i = 0; i < numargs; i++) {
	t->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (char*)&t->args[numargs];
    token_len = offsetof(gasnetc_token_t,args) + numargs*sizeof(gasnet_handlerarg_t);
    udata_avail = sizeof(gasnetc_uhdr_buf_t) - token_len;

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,(char*)source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(t);
    }

#if GASNETC_ENABLE_LOOPBACK
    if (requester == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	char *destloc;
	if (nbytes > udata_avail) {
	    destloc = gasneti_malloc_inhandler(nbytes > 0 ? nbytes : 1);
	    memcpy(destloc,(char*)source_addr,nbytes);
	} else {
	    destloc = udata_start;
	}
	RUN_HANDLER_MEDIUM(pfn,token,&t->args[0],numargs,(void*)destloc,nbytes);
	if (nbytes > udata_avail) {
	    gasneti_free(destloc);
	}
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* issue the request for remote execution of the user handler */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, requester,
			       gasnetc_remote_reply_hh[requester],
			       (void*)t, token_len,
			       (udata_packed ? NULL : (void*)source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyLongM( 
    gasnet_token_t token,       /* token provided on handler entry */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    void *source_addr, size_t nbytes,   /* data payload */
    void *dest_addr,                    /* data destination on destination node */
    int numargs, ...) {
    int retval;

    gasnetc_uhdr_buf_t *token_buf = (gasnetc_uhdr_buf_t *)token;
    gasnetc_token_t *t = &token_buf->token;
    uint requester = (uint)t->sourceId;
    lapi_cntr_t o_cntr;
    int token_len;
    char *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    int i, cur_cntr;
    gasnet_node_t dest = t->sourceId;;
    va_list argptr;
  
    retval = gasnet_AMGetMsgSource(token, &dest);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

    GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);

    GASNETC_MSG_SETFLAGS(t,0,gasnetc_Long,0,numargs);
    t->handlerId = handler;
    t->sourceId = gasnetc_mynode;
    t->destLoc = (uintptr_t)dest_addr;
    t->dataLen = nbytes;
    /* do NOT modify the contents of uhdrLoc... needed at origin */
    va_start(argptr, numargs); /*  pass in last argument */
    for (i = 0; i < numargs; i++) {
	t->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (char*)&t->args[numargs];
    token_len = offsetof(gasnetc_token_t,args) + numargs*sizeof(gasnet_handlerarg_t);
    udata_avail = sizeof(gasnetc_uhdr_buf_t) - token_len;


#if GASNETC_ENABLE_LOOPBACK
    if (requester == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	/* copy from source to dest, then execute handler */
	memcpy((char*)dest_addr,(char*)source_addr,nbytes);
	RUN_HANDLER_LONG(pfn,token,&t->args[0],numargs,dest_addr,nbytes);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,(char*)source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(t);
    }
    
    /* issue the request for remote execution of the user handler */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, requester,
			       gasnetc_remote_reply_hh[requester],
			       (void*)t, token_len,
			       (udata_packed ? NULL : (void*)source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;

    GASNETI_RETURN(retval);
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

/* ============================================================================
 * LAPI Clarification:
 * These calls should not have to be used or defined.  Even when LAPI is executing
 * in "interrupt" mode, the resulting handler is not executed in a traditional
 * signal handler.  A seperate thread, called the notification thread created at
 * LAPI init time, executes the LAPI dispatcher to make progress on a new
 * incoming packet and other outstanding communications.  No client thread
 * will be interrupted to execute the handler.
 * In LAPI POLLING mode, all progress is made when the client explicitly
 * calls LAPI functions via GASNET calls.  Again, no interrupts will occur.
 * ============================================================================
 */
#if GASNETC_USE_IBH
extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKATTACH();

    /* Check to see of interrupts are already being held */
    {
	int id = pthread_self();
	assert(id >= 0 && id < GASNETC_MAX_THREAD);
	if (gasnetc_interrupt_held[id]) {
	    gasneti_fatalerror("gasnetc_hold_interrupts: already held in thread %d",id);
	}
	gasnetc_interrupt_held[id] = 1;
    }

    /* (###) add code here to disable handler interrupts for _this_ thread */
}
extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKATTACH();

    /* Check to insure that interrupts are being held */
    {
	int id = pthread_self();
	assert(id >= 0 && id < GASNETC_MAX_THREAD);
	if (gasnetc_interrupt_held[id] != 0) {
	    gasneti_fatalerror("gasnetc_resume_interrupts: Not held in thread %d",id);
	}
	gasnetc_interrupt_held[id] = 0;
    }

    /* (###) add code here to re-enable handler interrupts for _this_ thread */
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
 * Handler-safe locks
 * ==================
 *
 * In the LAPI GASNET implementation, handlers are executed in exactly
 * two contexts:
 *
 * (1) In the LAPI completion handler thread.  Here, only one
 *     completion handler is executed at a time.
 * (2) By the client thread issuing an AM request call
 *     in which the target node is the same as the origin node
 *     and only if the 'GASNETC_ENABLE_LOOPBACK' macro is
 *     set to 1.
 *
 * In both of these cases, posix mutexes are sufficient for HSLs.
 * NOTE: removed the GASNETI_THREADS ifdefs because we need the
 *       mutex code even in the GASNET_SEQ case.  A handler
 *       can be executing in the LAPI completion handler thread
 *       and modifying data being used by the client thread.
 *
 * Note that we SHOULD add error checking....
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

    { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
	gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
    }

    /* (###) add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

    { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
	gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
    }

    /* (###) add code here to cleanup conduit-specific HSL state */
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

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
	gasneti_fatalerror("In gasnetc_hsl_lock(), pthread_mutex_lock()=%s",strerror(retval));
#if defined(STATS) || defined(TRACE)
    hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
    GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
#endif
    }

    /* (###) conduits with interrupt-based handler dispatch need to add code here to 
       disable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock acquire and we're not inside an enclosing no-interrupt section)
    */
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

    /* (###) conduits with interrupt-based handler dispatch need to add code here to 
       re-enable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock release and we're not inside an enclosing no-interrupt section)
    */

    GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

    { int retval = pthread_mutex_unlock(&(hsl->lock));
    if (retval) 
	gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%s",strerror(retval));
    }
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

/* ==========================================================================
 * MLW: Functions that could be in gasnet_core_internal.c
 * ==========================================================================
 */

/* --------------------------------------------------------------------------
 * This function will be involked in the event of an async error
 * from LAPI.
 * --------------------------------------------------------------------------
 */
static char* err_type_str[] = {
    "GET_ERR",
    "PUT_ERR",
    "RMW_ERR",
    "AM_ERR",
    "INTERRUPT_ERR"
};
void gasnetc_lapi_err_handler(lapi_handle_t *context, int *error_code,
			      lapi_err_t  *error_type, int *taskid, int *src)
{
    char msg[LAPI_MAX_ERR_STRING];

    LAPI_Msg_string(*error_code,msg);
    gasneti_fatalerror("Async LAPI Error on node %d from task %d of type %s code %d [%s]\n",
		       *src,*taskid,err_type_str[*error_type],*error_code,msg);
}

/* --------------------------------------------------------------------------
 * This function used to as a form of AllGather operation.  It is
 * collective across all the gasnet nodes.
 * Src is a pointer to a data structure of size len.
 * Dest is an array of the same kind of data structures, one element
 * for each gasnet node.
 * 
 * Each node puts a copy of its src into the dest[gasnetc_mynode]
 * on every other node.
 * --------------------------------------------------------------------------
 */
void gasnetc_lapi_exchange(void *src, size_t len, void *dest)
{
    void **dest_addr_tab;
    lapi_cntr_t c_cntr;
    int  node;
    int  num_nodes = (int)gasnetc_nodes;
    int  cur_val;

    /* First, need to determine address of dest on each node,
     * Note that this is a collective operation.
     */
    dest_addr_tab = gasneti_malloc_inhandler(num_nodes*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,dest,dest_addr_tab));
    
    /* Now, put my src value into all remote dest arrays */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&c_cntr,0));
    for (node = 0; node < num_nodes; node++) {
	void *ra = (void*)((char*)(dest_addr_tab[node]) + gasnetc_mynode*len);
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context,node,len,ra,
				src,NULL,NULL,&c_cntr));
    }

    /* Wait for all puts to complete locally and at targets */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&c_cntr,num_nodes,&cur_val));
    assert(cur_val == 0);

    /* Must barrier to insure all nodes have completed
     * puts to my dest array */
    GASNETC_LCHECK(LAPI_Gfence(gasnetc_lapi_context));

    /* free up array used for remote address locations */
    gasneti_free(dest_addr_tab);
}

/* --------------------------------------------------------------------------
 * The LAPI Active Message Header Handler function that gets involked
 * when the target node receives an active message request that
 * originated as a GASNET CORE Active Message.
 *
 * The LAPI header handler executes in a restricted manner by
 * a thread already holding the LAPI lock.  This function cannot
 * block and no communication can be performed in the header handler.
 * It executes when the first LAPI packet of a message arrives and informs
 * the LAPI dispatcher how to deal with this message.
 * In particular, it should:
 * (1) inform the dispatcher where to place the incoming data payload
 *     (if any exists)
 * (2) inform the dispatcher whether or not a completion handler should
 *     be executed when the entire message has arrived.  If so, it
 *     provides the dispatcher with a pointer to the completion handler
 *     function and, optionally, data that will be available to the
 *     completion handler function when it executes.
 * Note that the completion handler can issue communication calls.
 *
 * The "udata" argument provided to this function contains information
 * about what kind of GASNET AM was called (Short, Med, Long, Async)
 * the number of GASNET AM arguments, the index of the GASNET AM handler
 * function to involk, the GASNET source node, and possibly other data.
 *
 * --------------------------------------------------------------------------
 */
void* gasnetc_lapi_AMreq_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			    ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
    gasnetc_token_t *token = (gasnetc_token_t*)uhdr;
    gasnetc_category_t cat;
    unsigned int is_packed;
    void* destloc = NULL;
    gasnetc_uhdr_buf_t *token_buf;
    char *udata_start = NULL;
    unsigned int numargs;
    int token_len = *uhdr_len;

    /* All AM request handlers MUST run in the LAPI completion handler
     * since they will issue an AM reply
     */
    *comp_h = gasnetc_lapi_AMch;

    is_packed = GASNETC_MSG_ISPACKED(token);
    numargs = GASNETC_MSG_NUMARGS(token);
    cat = GASNETC_MSG_CATEGORY(token);

    { /* For dubugging */
	unsigned int is_req;
	is_req = GASNETC_MSG_ISREQUEST(token);
	assert(is_req);
    
	GASNETI_TRACE_PRINTF(C,("Req_HH received %s from %d is_req %d is_packed %d numargs %d uhdr_len %d msg_len %d destLoc %x dataLen %d",
				gasnetc_catname[cat],token->sourceId,is_req,
				is_packed,numargs,*uhdr_len,*msg_len,
				(void*)token->destLoc,token->dataLen));
    }
    
    switch (cat) {
    case gasnetc_Short:
	break;
    case gasnetc_Medium:
	if (is_packed) {
	    /* will use token space as local buffer */
	    assert( *msg_len == 0 );
	    destloc = NULL;
	    token->destLoc = NULL;
	} else {
	    assert( token->dataLen == *msg_len);
	    destloc = gasneti_malloc_inhandler( *msg_len > 0 ? *msg_len : 1 );
	    token->destLoc = (uintptr_t)destloc;
	}
	break;
    case gasnetc_AsyncLong:
    case gasnetc_Long:
	destloc = (void*)token->destLoc;
	if (is_packed) {
	    /* copy packed data to destination and report to LAPI
	     * dispatcher there is no more data coming
	     */
	    udata_start = (char*)&token->args[numargs];
	    memcpy((char*)destloc,udata_start,token->dataLen);
	    token_len -= token->dataLen;
	    destloc = NULL;
	}
	break;
    default:
	gasneti_fatalerror("Req_HH: invalid message Category %d",(int)cat);
    }

    /* Must copy uhdr to allocated uhdr buffer */
    token_buf = gasnetc_uhdr_alloc();
    *uinfo = (void*)token_buf;
    memcpy((char*)token_buf,(char*)uhdr,token_len);

    return destloc;
}


/* --------------------------------------------------------------------------
 * This is the LAPI Header Handler that gets executed by the LAPI dispatcher
 * when the first packet of an AM Reply message is received.
 * Reply handler are guaranteed not to block and will make no communication
 * calls.  As an optimization, if all the data is available in the uhdr,
 * we will execute the AM handler directly.
 *
 * If not all data has arrived:
 *   - Direct the LAPI dispatcher where to place the incoming data
 *     by providing a non-NULL return argument.
 *   - Schedule the completion handler to run the AM handler when all
 *     the data has arrived.
 *   - Copy the uhdr data into an allocated uhdr buffer for use by
 *     the completion handler and AM handlers.
 * --------------------------------------------------------------------------
 */
void* gasnetc_lapi_AMreply_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			      ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
    gasnetc_token_t *token = (gasnetc_token_t*)uhdr;
    gasnetc_category_t cat;
    unsigned int is_packed;
    void* destloc = NULL;
    gasnetc_uhdr_buf_t *token_buf;
    char *udata_start = NULL;
    unsigned int numargs;
    int token_len = *uhdr_len;
    gasnet_handler_t func_ix = token->handlerId;
    gasnetc_handler_fn_t am_func = gasnetc_handler[func_ix];
    gasnet_handlerarg_t *am_args = &token->args[0];
    int done = 0;

    /* All AM request handlers MUST run in the LAPI completion handler
     * since they will issue an AM reply
     */

    is_packed = GASNETC_MSG_ISPACKED(token);
    numargs = GASNETC_MSG_NUMARGS(token);
    cat = GASNETC_MSG_CATEGORY(token);

    { /* For dubugging */
	unsigned int is_req;
	is_req = GASNETC_MSG_ISREQUEST(token);
	assert(!is_req);
    
	GASNETI_TRACE_PRINTF(C,("Reply_HH received %s from %d is_req %d is_packed %d numargs %d uhdr_len %d msg_len %d destLoc %x dataLen %d",
				gasnetc_catname[cat],token->sourceId,is_req,
				is_packed,numargs,*uhdr_len,*msg_len,
				(void*)token->destLoc,token->dataLen));
    }
    
    /* This is a reply. If the uhdrLoc field of the token is set
     * that means the origional GASNET call was an AsyncLong and we
     * should deallocate the origional uhdr memory.
     */
    if (token->uhdrLoc != (uintptr_t)NULL) {
	gasnetc_uhdr_buf_t* loc = (gasnetc_uhdr_buf_t*)(token->uhdrLoc);
	gasnetc_uhdr_free(loc);
    }

    switch (cat) {
    case gasnetc_Short:
	/* can run the AM handler in-line */
	RUN_HANDLER_SHORT(am_func,token,am_args,numargs);
	done = 1;
	break;
    case gasnetc_Medium:
	if (is_packed) {
	    /* can run the AM handler in-line, data payload is packed in uhdr */
	    void *srcloc = (void*)&token->args[numargs];
	    RUN_HANDLER_MEDIUM(am_func,token,am_args,numargs,srcloc,token->dataLen);
	    done = 1;
	    *comp_h = NULL;
	    *uinfo = NULL;
	} else {
	    /* data payload not in uhdr, alloc space of it */
	    assert( token->dataLen == *msg_len);
	    destloc = gasneti_malloc_inhandler( *msg_len > 0 ? *msg_len : 1 );
	    token->destLoc = (uintptr_t)destloc;
	}
	break;
    case gasnetc_AsyncLong:
    case gasnetc_Long:
	destloc = (void*)token->destLoc;
	if (is_packed) {
	    /* copy packed data to destination and report to LAPI
	     * dispatcher there is no more data coming
	     */
	    char* udata_start = (char*)&token->args[numargs];
	    memcpy((char*)destloc,udata_start,token->dataLen);
	    RUN_HANDLER_LONG(am_func,token,am_args,numargs,destloc,token->dataLen);
	    done = 1;
	}
	break;
    default:
	gasneti_fatalerror("Reply_HH: invalid message Category %d",(int)cat);
    }

    if (done) {
	*comp_h = NULL;
	*uinfo = NULL;
	destloc = NULL;
    } else {
	/* Must copy uhdr to allocated uhdr buffer */
	token_buf = gasnetc_uhdr_alloc();
	*uinfo = (void*)token_buf;
	memcpy((char*)token_buf,(char*)uhdr,*uhdr_len);
	*comp_h = gasnetc_lapi_AMch;
    }

    return destloc;
}


/* --------------------------------------------------------------------------
 * This is the LAPI Completion Handler function that gets involked after
 * the entire contents of the message have been received on the target
 * host.  This function is executed by a special thread created by the
 * LAPI library at start-up time.  No locks are held, the function may
 * block and it can perform LAPI communication.
 *
 * The main purpose of this function is to execute the GASNET AM Request
 * or Reply handler specified in the originating GASNET AM call.
 * --------------------------------------------------------------------------
 */
void gasnetc_lapi_AMch(lapi_handle_t *context, void *uinfo)
{
    gasnetc_uhdr_buf_t *token_buf = (gasnetc_uhdr_buf_t*)uinfo;
    gasnetc_token_t *token = &token_buf->token;
    
    /* extract the token and paramater from the uinfo structure */
    gasnetc_category_t msg_type = GASNETC_MSG_CATEGORY(token);
    unsigned int numargs = GASNETC_MSG_NUMARGS(token);
    unsigned int is_request = GASNETC_MSG_ISREQUEST(token);
    unsigned int is_packed = GASNETC_MSG_ISPACKED(token);
    void *dataptr = (void*)(token->destLoc);
    size_t datalen = token->dataLen;
    gasnet_handler_t func_ix = token->handlerId;
    gasnetc_handler_fn_t am_func = gasnetc_handler[func_ix];
    gasnet_handlerarg_t *am_args = &token->args[0];

    GASNETI_TRACE_PRINTF(C,("CH received %s from %d is_req %d is_packed %d numargs %d handlerix %d dataptr %x datalen %d",
			    gasnetc_catname[msg_type],token->sourceId,is_request,
			    is_packed,numargs,func_ix,dataptr,datalen));

    if (am_func == NULL) {
	gasneti_fatalerror("lapi_AMch: node %d, invalid handler index %d",
			   gasnetc_mynode,func_ix);
    }

    /* run the GASNET handler */
    switch (msg_type) {
    case gasnetc_Short:
	RUN_HANDLER_SHORT(am_func,token,am_args,numargs);
	break;
	
    case gasnetc_Medium:
	if (is_packed) {
	    /* data is cached in this uhdr */
	    dataptr = (void*)&token->args[numargs];
	}
	RUN_HANDLER_MEDIUM(am_func,token,am_args,numargs,dataptr,datalen);
	/* need to free this data memory (allocated in header handler) */
	if (! is_packed) {
	    /* we allocated a buffer for the payload in the header handler */
	    gasneti_free(dataptr);
	}
	break;

    case gasnetc_Long:
    case gasnetc_AsyncLong:
	RUN_HANDLER_LONG(am_func,token,am_args,numargs,dataptr,datalen);
	/* Note that the memory specified by dataptr and datalen must
	 * be in the segment registered on this node.
	 */
	break;
	
    }

    /* deallocate the uinfo structure that was allocated by
     * the header handler
     */
    gasnetc_uhdr_free(token_buf);
}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_init:
 *
 * Init the LAPI uhdr free list.  Alloc and free called from within
 * LAPI header handler and completion handler.  Must not block
 * indefinately.
 * --------------------------------------------------------------------------
 */
void gasnetc_uhdr_init(int want)
{
    int got;
    gasnetc_uhdr_freelist_t *list = &gasnetc_uhdr_freelist;
    pthread_mutex_init(&list->lock, NULL);
    pthread_mutex_lock(&list->lock);

    got = gasnetc_uhdr_more(want);
    if (got == 0) {
	pthread_mutex_unlock(&list->lock);
	gasneti_fatalerror("Unable to alloc ANY uhdr buffers");
    }

    list->numfree = got;
    list->numalloc = list->high_water_mark = 0;
    pthread_mutex_unlock(&list->lock);
}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_alloc:
 *
 * return an element from the free list.  If freelist is empty,
 * attempt to alloc more.  If that fails, abort.
 * --------------------------------------------------------------------------
 */
gasnetc_uhdr_buf_t* gasnetc_uhdr_alloc(void)
{
    gasnetc_uhdr_buf_t *p = NULL;
    gasnetc_uhdr_freelist_t *list = &gasnetc_uhdr_freelist;

    pthread_mutex_lock(&list->lock);

    if (list->numfree == 0) {
	gasnetc_uhdr_more(GASNETC_UHDR_ADDITIONAL);
	if (list->numfree == 0) {
	    pthread_mutex_unlock(&list->lock);
	    gasneti_fatalerror("Unable to alloc additional uhdr buffers");
	}
    }

    assert(list->freelist != NULL);
    p = list->freelist;
    list->freelist = p->next;
    list->numfree--;
    list->numalloc++;
    if (list->numalloc > list->high_water_mark)
	list->high_water_mark = list->numalloc;
    pthread_mutex_unlock(&list->lock);
    return p;
}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_alloc:
 *
 * put this uhdr back onto the free list.
 * --------------------------------------------------------------------------
 */
void gasnetc_uhdr_free(gasnetc_uhdr_buf_t *p)
{
    gasnetc_uhdr_freelist_t *list = &gasnetc_uhdr_freelist;

    pthread_mutex_lock(&list->lock);
    p->next = list->freelist;
    list->freelist = p;
    list->numfree++;
    list->numalloc--;
    pthread_mutex_unlock(&list->lock);
}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_alloc:
 *
 * This is the real allocation function.  It attempts to allocate
 * the requested number of buffers, but decreases the size by a
 * factor of two each time it fails.
 * The allocated buffers are added to the freelist.
 * The return value is the actual number of buffers allocated.
 * --------------------------------------------------------------------------
 */
int gasnetc_uhdr_more(int want)
{
    /* NOTE: assumes lock already held */

    gasnetc_uhdr_freelist_t *list = &gasnetc_uhdr_freelist;
    gasnetc_uhdr_buf_t *free = (gasnetc_uhdr_buf_t*)gasneti_malloc_inhandler(want * sizeof(gasnetc_uhdr_buf_t));
    while (free == NULL) {
	if (want <= 1) {
	    return 0;
	} else {
	    want /= 2;
	}
	free = (gasnetc_uhdr_buf_t*)gasneti_malloc_inhandler(want * sizeof(gasnetc_uhdr_buf_t));
    }

    /* link them onto freelist */
    for (int i = 0; i < want; i++) {
	free->next = list->freelist;
	list->freelist = free;
	free++;
    }
    list->numfree += want;

    GASNETI_TRACE_PRINTF(C,("Allocated %d more UHDR BUFFERS: numalloc %d numfree %d",
			    want,list->numalloc,list->numfree));
    return want;
}
