/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2002/07/03 19:41:24 $
 * $Revision: 1.1 $
 * Description: GASNet lapi conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <lapi.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

static gasent_handlerentry_t gasnetc_AMhandler[256];
gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = -1;
gasnet_node_t gasnetc_nodes = 0;
uintptr_t     gasnetc_max_local = 0;
uintptr_t     gasnetc_max_global = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;

int gasnetc_init_done = 0;   /*  true after gasnet_init */
int gasnetc_attach_done = 0; /*  true after gasnet_attach */

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
    /* (###) add code to do some sanity checks on the number of nodes, handlers
     * and/or segment sizes */ 
}

/* Core initialization.
 * -- start jobs
 * -- init enviromnemt
 * -- propogate/inspect/augment args
 */
static int gasnetc_init(int *argc, char ***argv)
{
    lapi_info_t  lapi_info;    
    int          task_id;
    int          num_tasks;
    lapi_cntr_t  c_cntr;
    int          rc;                      /* LAPI return value */
    int          grc = GASNET_SUCCESS;    /* gasnet return code */

    void* gmax_loc;
    size_t *gmax;

    /*  check system sanity */
    gasnetc_check_config();

    if (gasnetc_init_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

    /* On the IBM, POE has already started all the tasks.  
     * In addition, all tasks see the same environment and
     * get the same argument list.
     */

    /* Call LAPI_init and get task info */
    bzero(&info, sizeof(lapi_info_t));
    info.err_hndlr = gasnetc_lapi_err_handler;
    /* init is a collective operation */
    L_CHECK(LAPI_Init(&lapi_context, &info));
    if (rc != LAPI_SUCCESS) {
	GASNETI_RETURN_ERR(RESOURCE);
    }

    if (LAPI_Init(&lapi_context, &info) != LAPI_SUCCESS) {
	
    }

    /* get task number and number of tasks in job */
    L_CHECK(LAPI_Qenv(lapi_context, TASK_ID, &task_id));
    if (rc != LAPI_SUCCESS) {
	GASNETI_RETURN_ERR(RESOURCE);
    }
    L_CHECK(LAPI_Qenv(lapi_context, NUM_TASKS, &num_tasks));
    if (rc != LAPI_SUCCESS) {
	GASNETI_RETURN_ERR(RESOURCE);
    }

    gasnetc_mynode = (gasnet_node_t)task_id;
    gasnetc_nodes = (gasnet_node_t)num_tasks;

    /* Now process the arg list and environment to set any
     * LAPI-specific variables.
     * -- Possibly one for interrupt or polling mode?
     * XXX LAPI: Do something here.
     */

    /* Now determine limits on local and global memory */
    /* On AIX, we will use anon memory mapped pages for the
     * global shared segment.  AIX uses a segmented memory
     * system.  In 32 bit applications, the program data
     * segment is 256 MB and contains the stack, heap and
     * initialized and uninitialized static data.
     * This region can be extended at link time to include
     * additional segments, each 256 MB in size.
     * In addition, a range of segments are available for
     * mmaped data and shared memory regions.  The number
     * of 256MB segments allocated to this depends on a number
     * of factors, including the size of the data area, and
     * whether or not both MPI and LAPI are in use at the
     * same time.  If so, two of the 16 segments are used
     * for this.
     * I have found NO mechanism in AIX to discover this information
     * at run-time, so I have no idea how much space I can mmap without
     * trial and error.
     *
     * Note that malloc is done entirely in the heap area (sbrk)
     * and so the local allocator and global access segments will
     * never interfere.
     *
     * In 64 bit mode, there is a ton of address space and this
     * is not an issue.
     */

    /* adjust our soft limits up to something just below the hard limit */
    gasnetc_adjust_limits();
    
#ifdef GASNET_SEGMENT_EVERYTHING
    gasnetc_max_local  = (uintptr_t)-1;
    gasnetc_max_global = (uintptr_t)-1;
#else    

#ifdef __64BIT__
    /* In 64 bit mode, just decide on a value, we can always get it */
    /* XXX Can we determine the amount of phys memory avail on the node
     *     to use as an upper limit?
     */
    gasnetc_max_local = 64L * GASNETC_GBYTE;
#else
    /* In 32 BIT mode, try for 3GB, but take what we can get */
    gasnetc_max_local = 3L * GASNETC_GBYTE;
    gasnetc_max_local = gasnetc_compute_maxlocal(gasnetc_max_local);
#endif

    /* Now, exchange sizes so that we can compute the global max, which
     * is the MIN of the local maxes.
     *
     * First, malloc space for the results and for the cross-task location
     * table.
     */
    gmax_loc = (void**)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(void*));
    gmax = (uintptr_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(uintptr_t));
    if (gmax == NULL || gmax_loc == NULL) {
	
    /* exchange locations of gmax arrays on all tasks */
    L_CHECK(LAPI_Address_init(lapi_context,gmax_loc,gmax));
    /* now, have each task put their value into all others remote locations */
    LAPI_Setcntr(lapi_context,&c_cntr,0);
    for (i = 0; i < gasnetc_nodes; i++) {
	void* raddr = gmax_loc[i] + sizeof(uintptr_t)*gasnetc_mynode;
	if (i == gasnetc_mynode) {
	    gmax[i] = gasnetc_max_local;
	} else {
	    L_CHECK(LAPI_Put(lapi_context,i,sizeof(uintptr_t),raddr,
			     &gasnetc_max_local,NULL,NULL,&c_cntr));
	    assert(rc = LAPI_SUCCESS);
	}
    }
    /* wait until data is at all remote nodes */
    if (gasnetc_nodes > 1) {
	int cur_val;
	L_CHECK(LAPI_Waitcntr(lapi_context,&c_cntr,gasnetc_nodes-1,&cur_val));
	if (rc != LAPI_SUCCESS) {
	    /* free memory and fail */
	    grc = GASNET_ERR_RESOURCE;
	    goto out;
	}
	assert(cur_val == 0);
    }
    /* wait until all nodes have finished */
    L_CHECK(LAPI_Gfence(lapi_context));
    if (rc != LAPI_SUCCESS) {
	/* free memory and fail */
	grc = GASNET_ERR_RESOURCE;
	goto out;
    }

    /* Finally, compute the min of the maxes */
    gasnetc_max_global = gmax[0];
    for (i = 1; i < gasnetc_nodes; i++) {
	gasnetc_max_global = MIN(gasnetc_max_global,gmax[i]);
    }

    gasnetc_init_done = 1;

    out:
    /* free the space */
    gasneti_free_inhandler(gmax_loc);
    gasneti_free_inhandler(gmax);

#endif

    return GASNETI_RETURN(grc);
}

static int gasnetc_attach(gasnet_handlerentry_t *table, int numentries, 
			  uintptr_t segsize, uintptr_t minheapoffset)
{
    int retval = GASNET_OK;
    size_t pagesize = gasneti_getSystemPageSize();
    char checkuniqhandler[256];
    lapi_cntr_t  c_cntr;

    int  rc;      /* lapi return code from L_CHECK */

    /* NOTE: On AIX, heap and mmap regions are in different segments
     * so cannot control minheapoffset.  If not large enough, must
     * specify the size of the heap area at link time.
     */

    /*  check system sanity */
    gasnetc_check_config();

    if (!gasnetc_init_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet NOT initialized");
    if (gasnetc_attach_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet Attach already initialized");

    /* LAPI: Exchange addresses for the various LAPI header handlers */
    gasnetc_AMRequest_HH = (void**)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(void*));
    gasnetc_AMReply_HH = (void**)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(void*));
    L_CHECK(LAPI_Address_init(lapi_context,gasnetc_request_header_handler,
			      gasnetc_AMRequest_HH));
    L_CHECK(LAPI_Address_init(lapi_context,gasnetc_reply_header_handler,
			      gasnetc_AMReply_HH));
    

    /* ------------------------------------------------------------------------------ */
    /*  register AM handlers */
    memset(gasnetc_AMhandlers, 0, 256*sizeof(gasnet_handlerentry_t));
    memset(checkuniqhandler, 0, 256);
    { /*  core API handlers */
	gasnet_handlerentry_t const *ctable = gasnetc_get_handlertable();
	int i;
	assert(ctable);
	for (i = 0; ctable[i].fnptr; i++) {
	    assert(ctable[i].index);
	    /*  ensure all core API handlers have pre-assigned index 1..99 */
	    assert(ctable[i].index >= 1 && ctable[i].index <= 99);
	    /* discover duplicates */
	    assert(checkuniqhandler[ctable[i].index] == 0);
	    checkuniqhandler[ctable[i].index] = 1;

	    /* Put it into the AM handler table */
	    gasnetc_AMhandler[i] = ctable[i];
	}
    }

    { /*  extended API handlers */
	gasnet_handlerentry_t const *etable = gasnete_get_handlertable();
	int i;
	assert(etable);
	for (i = 0; etable[i].fnptr; i++) {
	    int ix = etable[i].index;
	    assert(etable[i].index);
	    /*  ensure all extended API handlers have pre-assigned index 100..199 */
	    assert(etable[i].index >= 100 && etable[i].index <= 199);
	    /* discover duplicates */
	    assert(checkuniqhandler[etable[i].index] == 0);
	    checkuniqhandler[etable[i].index] = 1;
	    /* Put it into the AM handler table */
	    gasnetc_AMhandler[ix] = etable[i];
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
		gasnetc_AMhandler[table[i].index] = table[i];
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
		/* given numentries < 56, this should never happen, but check.. */
		assert(checkuniqhandler[tmp] == 0);
		checkuniqhandler[tmp] = 1;
		/* Put it into the table and mod the return table index */
		gasnetc_AMhandler[tmp] = table[i];
		table[i].index = tmp;
	    }
	}
    }

    /* ------------------------------------------------------------------------------- */
    /*  register fatal signal handlers */

    /*  (###) catch fatal signals and convert to SIGQUIT */

    /* ------------------------------------------------------------------------------- */
    /*  register RMA segment  */

    gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));
    glob_segaddr = (void**)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(void*));
    L_CHECK(LAPI_Address_init(lapi_context,gasnetc_seginfo,glob_segaddr));

#ifdef GASNET_SEGSIZE_EVERYTHING
    
    gasnetc_seginfo[gasnetc_mynode].addr = 0;
    gasnetc_seginfo[gasnetc_mynode].size = (uintptr_t)-1;

#else

    if (segsize == 0) {
	/* cant use AM long or RMA operations to this node */
	gasnetc_seginfo[gasnetc_mynode].addr = 0;
	gasnetc_seginfo[gasnetc_mynode].size = 0;
    } else {

	/* compute the largest region <= segsize we can get */
	canget = gasnetc_compute_maxlocal(segsize);
	/* now get it */
	if (! gasnetc_get_map(canget,&segbase)) {
	    /* Crap.. this should not happen */
	    assert(0);
	}
	
	gasnetc_seginfo[gasnetc_mynode].addr = segbase;
	gasnetc_seginfo[gasnetc_mynode].size = canget;
    }

#endif

    /* ------------------------------------------------------------------------------- */
    /*  gather segment information ... */

    /* Distribute my info to all other nodes */
    L_CHECK(LAPI_Setcntr(lapi_context,&c_cntr,0));
    for (node = 0; node < num_nodes; node++) {
	void* laddr = &gasnetc_seginfo[gasnetc_mynode];
	void* raddr = glob_segaddr[node] + gasnetc_mynode*sizeof(gasnet_seginfo_t);
	if (node == my_node) continue;
	L_CHECK(LAPI_Put(lapi_context,node,sizeof(gasnet_seginfo_t),raddr,laddr,
			 NULL,NULL,&c_cntr));
    }
    /* wait until all communication from this node is complete at target */
    L_CHECK(LAPI_Waitcntr(lapi_context,&c_cntr,gasnetc_nodes-1,&val));
    assert(val == 0);
    /* now wait for everyone to complete. */
    L_CHECK(LAPI_GFence(lapi_context));

    /* release space used to communicate segment info */
    gasneti_free_inhandler(glob_segaddr);

#if GASNET_ALIGNED_SEGMENTS == 1
    {  /*  need to check that segments are aligned */
	int i;
	segbase = gasnetc_seginfo[0].addr;
	for (i=0; i < gasnetc_nodes; i++) {
	    if (gasnetc_seginfo[i].addr != segbase) {
		/*  we grabbed unaligned memory */
		/*  TODO: need a recovery mechanism here ... */
		assert(0);
	    }
	}
    }
#endif

    /* (###) add a barrier here */
    LAPI_Gfence();

  /*  init complete */
    gasnetc_attach_done = 1;

    return GASNET_OK;
}


extern int gasnet_init(int *argc, char ***argv, 
		       gasnet_handlerentry_t *table, int numentries, 
		       void *segbase, uintptr_t segsize,
		       int allowFaults) {
    int retval = gasnetc_init(argc, argv, table, numentries, segbase, segsize, allowFaults);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasnete_init();
    gasneti_trace_init();
    return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern void gasnetc_exit(int exitcode) {
    gasneti_trace_finish();
    /* Note that this is not a collective operation in LAPI */
    LAPI_Term(lapi_context);
    exit(exitcode);
}

/* ------------------------------------------------------------------------------------ */
extern uintptr_t gasnet_getMaxLocalSegmentSize(void) {
    GASNETC_CHECKINIT();
    /* NOTE: this should already be -1 in the case of GASNET_SEGMENT_EVERYTHING */
    return gasnetc_max_local;
}

/* ------------------------------------------------------------------------------------ */
extern uintptr_t gasnet_getMaxGlobalSegmentSize(void) {
    GASNETC_CHECKINIT();
    /* NOTE: this should already be -1 in the case of GASNET_SEGMENT_EVERYTHING */
    return gasnetc_max_global;
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
    gasnet_token_rec *gtr;
    GASNETC_CHECKINIT();
    if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
    if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

    gtr = (gasnet_token_rec*)token;
    sourceid = gtr->src_node;

    assert(sourceid < gasnetc_nodes);
    *srcindex = sourceid;
    return GASNET_OK;
}

extern int gasnetc_AMPoll() {
    int retval;
    GASNETC_CHECKINIT();

    /* In LAPI, AM handlers are run by the lapi completion_handler
     * in the completion_handler thread.  Just probe LAPI to
     * insure progress is being made there.
     */
    LAPI_Probe(lapi_context);

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
    gasnet_token_rec token;
    lapi_cntr_t o_cntr;
    
    GASNETC_CHECKINIT();
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    memset(&token,0,sizeof(gasnet_token_rec);
    token.src_node = gasnetc_mynode;
    token.dest_node = dest;
    token.num_args = numargs;
    for (i = 0; i < numargs; i++) {
	token.args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    token.msgtype = AM_SHORT;
    token.data_size = 0;
    
    LAPI_CHECK(LAPI_Setcntr(lapi_context,&o_cntr,0));
    LAPI_CHECK(LAPI_Amsend(lapi_context, (int)dest, (void*)gasnetc_AMRequest_HH[dest],
			   (void*)&token, sizeof(gasnet_token_rec), NULL, 0,
			   NULL, &o_cntr, NULL));

    /* wait for origin counter to pop */
    LAPI_CHECK(LAPI_Waitcntr(lapi_context,&o_cntr,1,&cur_cntr));
    assert(cur_cntr == 1);

    retval = GASNET_OK;
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

    gasnet_token_rec token;
    lapi_cntr_t o_cntr;

    GASNETC_CHECKINIT();
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    memset(&token,0,sizeof(gasnet_token_rec);
    token.src_node = gasnetc_mynode;
    token.dest_node = dest;
    token.num_args = numargs;
    for (i = 0; i < numargs; i++) {
	token.args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    token.msgtype = AM_MEDIUM;
    token.data_size = nbytes;
    
    LAPI_CHECK(LAPI_Setcntr(lapi_context,&o_cntr,0));
    LAPI_CHECK(LAPI_Amsend(lapi_context, (int)dest, (void*)gasnetc_AMRequest_HH[dest],
			   (void*)&token, sizeof(gasnet_token_rec), source_addr, nbytes,
			   NULL, &o_cntr, NULL));

    /* wait for origin counter to pop */
    LAPI_CHECK(LAPI_Waitcntr(lapi_context,&o_cntr,1,&cur_cntr));
    assert(cur_cntr == 1);

    /* XXX check lapi return value and convert */
    retval = GASNET_OK;
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

    gasnet_token_rec token;
    lapi_cntr_t o_cntr;

    GASNETC_CHECKINIT();
  
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
    GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    memset(&token,0,sizeof(gasnet_token_rec);
    token.src_node = gasnetc_mynode;
    token.dest_node = dest;
    token.num_args = numargs;
    for (i = 0; i < numargs; i++) {
	token.args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    token.msgtype = AM_LONG;
    token.data_size = nbytes;
    token.data_loc = dest_addr;
    
    LAPI_CHECK(LAPI_Setcntr(lapi_context,&o_cntr,0));
    LAPI_CHECK(LAPI_Amsend(lapi_context, (int)dest, (void*)gasnetc_AMRequest_HH[dest],
			   (void*)&token, sizeof(gasnet_token_rec), source_addr, nbytes,
			   NULL, &o_cntr, NULL));


    retval = ###;
    va_end(argptr);
    if (retval) return GASNET_OK;
    else GASNETI_RETURN_ERR(RESOURCE);
}

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
    GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);

    va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = ###;
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
    GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = ###;
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
    GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = ###;
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
  
    retval = gasnet_AMGetMsgSource(token, &dest);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

    GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = ###;
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
#if 0
extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKINIT();
    /* (###) add code here to disable handler interrupts for _this_ thread */
}
extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKINIT();
    /* (###) add code here to re-enable handler interrupts for _this_ thread */
}
#else
/* In LAPI, the comm layer does not use UNIX signals in interrupt mode.
 * When a packet arrives, the HAL wakes the notification thread, which
 * enters the LAPI dispatcher to make progress.
 * So, for LAPI, just define as NULL macros
 */
#define gasnetc_hold_interrupts()
#define gasnetc_resume_interrupts() 
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
    GASNETC_CHECKINIT();

#ifdef GASNETI_THREADS
    {
	int retval = pthread_mutex_init(&(hsl->lock), NULL);
	if (retval) 
	    gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%i",strerror(retval));
    }
#endif

  /* (###) add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
    GASNETC_CHECKINIT();
#ifdef GASNETI_THREADS
    {
	int retval = pthread_mutex_destroy(&(hsl->lock));
	if (retval) 
	    gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%i",strerror(retval));
    }
#endif

    /* (###) add code here to cleanup conduit-specific HSL state */
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
    GASNETC_CHECKINIT();

#ifdef GASNETI_THREADS
    {
	int retval; 
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

    /* (###) conduits with interrupt-based handler dispatch need to
     * add code here to disable handler interrupts on _this_ thread,
     * (if this is the outermost HSL lock acquire and we're not
     * inside an enclosing no-interrupt section)
     */
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
    GASNETC_CHECKINIT();

    /* (###) conduits with interrupt-based handler dispatch need to
     * add code here to re-enable handler interrupts on _this_
     * thread, (if this is the outermost HSL lock release and we're
     * not inside an enclosing no-interrupt section)
     */

    GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

#ifdef GASNETI_THREADS
    {
	int retval = pthread_mutex_unlock(&(hsl->lock));
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
/* In LAPI... Will use LAPI for all communication between nodes.  We don't
 * need core-specific handlers */
static gasnet_handlerentry_t const gasnetc_handlers[] = {
    /* ptr-width independent handlers */

    /* ptr-width dependent handlers */

    { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
    return gasnetc_handlers;
}


/* ------------------------------------------------------------------------------------ */
/* LAPI Header and completion handlers that implement the AM core */

/* ------------------------------------------------------------------------
 * Completion handler function for Lapi_Amsend implementing AM Request
 * and AM Reply calls.
 * ------------------------------------------------------------------------
 */
void gasnetc_AMrr_completion_handler(lapi_handle_t *context, void *user_info)
{
    gasnet_token_rec *token = (gasnet_token_rec*)user_info;
    void *addr = NULL;
    gasnet_handler_t  h_ix = token->AM_handler;


    /* Call the user-defined handler specified in the token */
    switch (token->msg_type) {
    case AM_SHORT:
	RUN_HANDLER_SHORT((gasnetc_AMhandler[h_ix].func),token,(token->args),
			  (token->num_args));
	break;
    case AM_MEDIUM:
	addr = token->data_loc;
	RUN_HANDLER_MEDIUM((gasnetc_AMhandler[h_ix].func),token,(token->args),
			   (token->num_args),addr,(token->data_size));
	/* free space used in temp buffer */
	free(addr);
	break;
    case AM_LONG:
    case AM_LONG_ASYNC:
	addr = token->data_loc;
	RUN_HANDLER_LONG((gasnetc_AMhandler[h_ix].func),token,(token->args),
			 (token->num_args),addr,(token->data_size));
	break;
    default:
	abort();
    }

    /* dealloc user_info space allocated in header_handler */
    free(user_info);
}

/* ------------------------------------------------------------------------
 * Header handler function for Lapi_Amsend implementing AM Request call
 * ------------------------------------------------------------------------
 */
void* gasnetc_request_header_handler(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				     uint *msg_len, compl_hndlr_t **compl_handler,
				     vpid **user_info)
{
    /* This gets called in the dispatcher when an AM_Request
     * packet arrives.  Determine the type of the message,
     * alloc space for data if necessary and schedule the
     * completion handler to run when all the data arrives
     */
    gasnet_token_rec *token = (gasnet_token_rec*)uhdr;
    void *addr = NULL;
    
    /* copy the uhdr (token) to the user_info for the completion handler to use */
    *user_info = (void*)malloc(*uhdr_len);
    assert (*user_info != NULL);
    memcpy((char*)(*user_info),(char*)uhdr,*uhdr_len);
    token = (gasnet_token_rec*)(*user_info);
    *compl_handler = gasnetc_AMrr_completion_handler;
    
    switch (token->msg_type) {
    case AM_SHORT:
	break;
    case AM_MEDIUM:
	/* need to malloc space for incoming medium message */
	addr = token->data_loc = malloc(*msg_len);
	assert(addr != NULL);
	assert(token->data_size == *msg_len);
	break;
    case AM_LONG:
    case AM_LONG_ASYNC:
	/* use space in shared segment requested by sender */
	addr = token->data_loc;
	assert(token->data_size == *msg_len);
	break;
    default:
	abort();
    }

    return addr;

}

/* ------------------------------------------------------------------------
 * Header handler function for Lapi_Amsend implementing AM Request call
 * ------------------------------------------------------------------------
 */
void* gasnetc_reply_header_handler(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				   uint *msg_len, compl_hndlr_t **compl_handler,
				   vpid **user_info)
{
    /* This gets called in the dispatcher when an AM_Reply
     * packet arrives.  
     *
     * Note that for short messages, we can run the client AM reply
     * handler in-line.  For medium and long messages, we may
     * have to wait for data to arrive in additional LAPI packets
     * and thus must schedule a completion handler to execute
     * the AM handler
     */
    
    gasnet_token_rec *token = (gasnet_token_rec*)uhdr;
    void *addr = NULL;
    int hndlr_ix = token->AM_Handler;
    
    /* make sure the requested AM handler is valid */
    assert(hndlr_ix >=0 && hndlr < 256);
    assert(gasnetc_AMhandler[hndlr_ix].index > 0);

    *compl_handler = NULL;
    *user_info = NULL;

    /* if a short message, run immediately and return */
    if (token->msg_type == AM_SHORT) {
	RUN_HANDLER_SHORT((gasnetc_AMhandler[hndlr_ix].func),token,
			  token->args,token->num_args);
	return NULL;
    }
    
    /* Question: does the space allocated to hold uhdr live until
     * after the completion handler runs?  Probably not, but ff so,
     * we can just pass a pointer to this in user_info.  If not, we must
     * malloc space for the user_info, copy the token and
     * then free it in the completion handler.
     * Lets do it the safe way for now.
     */
    *user_info = (void*)malloc(*uhdr_len);
    assert (*user_info != NULL);
    memcpy((char*)(*user_info),(char*)uhdr,*uhdr_len);
    token = (gasnet_token_rec*)(*user_info);
    *compl_handler = gasnetc_AMrr_completion_handler;
    
    switch (token->msg_type) {
    case AM_MEDIUM:
	/* need to malloc space for incoming medium message */
	addr = token->data_loc = malloc(*msg_len);
	assert(addr != NULL);
	assert(token->data_size == *msg_len);
	break;
    case AM_LONG:
    case AM_LONG_ASYNC:
	/* use space in shared segment requested by sender */
	addr = token->data_loc;
	assert(token->data_size == *msg_len);
	break;
    default:
	abort();
    }

    return addr;

}

