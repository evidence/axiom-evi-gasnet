/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2003/08/26 05:02:32 $
 * $Revision: 1.7 $
 * Description: GASNet smp conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();
static void gasnetc_atexit(void);

gasnet_node_t gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnetc_nodes = 0;

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

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

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  /* add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 
}

void gasnetc_bootstrapExchange(void *src, size_t len, void *dest) {
  assert(gasnetc_nodes == 1); /* trivial because we only have one node */
  memmove(dest, src, len);
}
void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  assert(gasnetc_nodes == 1); /* trivial because we only have one node */
  assert(rootnode == 0);
  memmove(dest, src, len);
}

static void gasnetc_bootstrapBarrier() {
  /* add code here to implement an external barrier 
      this barrier should not rely on AM or the GASNet API because it's used 
      during bootstrapping before such things are fully functional
     It need not be particularly efficient, because we only call it a few times
      and only during bootstrapping - it just has to work correctly
     If your underlying spawning or batch system provides barrier functionality,
      that would probably be a good choice for this
   */
  assert(gasnetc_nodes == 1); /* trivial because we only have one node */
}

static int gasnetc_init(int *argc, char ***argv) {
  /*  check system sanity */
  gasnetc_check_config();

  if (gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");
  gasnetc_init_done = 1; /* enable early to allow tracing */

  if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

  #if DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  /* add code here to bootstrap the nodes for your conduit */

  gasnetc_mynode = 0;
  gasnetc_nodes = 1;

  /* enable tracing */
  gasneti_trace_init();

  #if DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    { 
      gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize, 
                          &gasnetc_MaxGlobalSegmentSize,
                          (uintptr_t)-1,
                          gasnetc_nodes,
                          &gasnetc_bootstrapExchange);

    }
  #elif defined(GASNET_SEGMENT_EVERYTHING)
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
  #else
    #error Bad segment config
  #endif

  #if 0
    /* Enable this if you wish to use the default GASNet services for broadcasting 
        the environment from one compute node to all the others (for use in gasnet_getenv(),
        which needs to return environment variable values from the "spawning console").
        You need to provide two functions (gasnetc_bootstrapExchange and gasnetc_bootstrapBroadcast)
        which the system can safely and immediately use to broadcast and exchange information 
        between nodes (gasnetc_bootstrapBroadcast is optional but highly recommended).
       This system assumes that at least one of the compute nodes has a copy of the 
        full environment from the "spawning console" (if this is not true, you'll need to
        implement something yourself to get the values from the spawning console)
       If your job system already always propagates environment variables to all the compute
        nodes, then you probably don't need this.
     */
    gasneti_setupGlobalEnvironment(gasnetc_nodes, gasnetc_mynode, 
                                   gasnetc_bootstrapExchange, gasnetc_bootstrapBroadcast);
  #endif

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* called within gasnet_init to allow init tracing */
    gasneti_trace_init();
  #endif
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
    /*  add code here to register table[i].fnptr 
             on index (gasnet_handler_t)newindex */
    gasnetc_handler[(gasnet_handler_t)newindex] = (gasnetc_handler_fn_t)table[i].fnptr;

    if (dontcare) table[i].index = newindex;
    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
  void *segbase = NULL;
  
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
                          numentries, (unsigned long)segsize, (unsigned long)minheapoffset));

  if (!gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
  if (gasnetc_attach_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

  /*  check argument sanity */
  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    if ((segsize % GASNET_PAGESIZE) != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize not page-aligned");
    if (segsize > gasnetc_getMaxLocalSegmentSize()) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize too large");
    if ((minheapoffset % GASNET_PAGESIZE) != 0) /* round up the minheapoffset to page sz */
      minheapoffset = ((minheapoffset / GASNET_PAGESIZE) + 1) * GASNET_PAGESIZE;
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
    if (gasnetc_reghandlers(etable, len, 64, 127, 0, &numreg) != GASNET_OK)
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

  /*   register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  atexit(gasnetc_atexit);

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
     hold/resume interrupts functions are operational yet */
  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    gasneti_segmentAttach(segsize, minheapoffset, gasnetc_seginfo, &gasnetc_bootstrapExchange);
  #else
    /* GASNET_SEGMENT_EVERYTHING */
    { int i;
      for (i=0;i<gasnetc_nodes;i++) {
        gasnetc_seginfo[i].addr = (void *)0;
        gasnetc_seginfo[i].size = (uintptr_t)-1;
      }
    }
  #endif
  segbase = gasnetc_seginfo[gasnetc_mynode].addr;
  segsize = gasnetc_seginfo[gasnetc_mynode].size;
  assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
  assert(segsize % GASNET_PAGESIZE == 0);

  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  /*  add code here to gather the segment assignment info into 
           gasnetc_seginfo on each node (may be possible to use AMShortRequest here)
   */

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasnetc_attach_done = 1;
  gasnetc_bootstrapBarrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

  assert(gasnetc_seginfo[gasnetc_mynode].addr == segbase &&
         gasnetc_seginfo[gasnetc_mynode].size == segsize);

  #if GASNET_ALIGNED_SEGMENTS == 1
    { int i; /*  check that segments are aligned */
      for (i=0; i < gasnetc_nodes; i++) {
        if (gasnetc_seginfo[i].size != 0 && gasnetc_seginfo[i].addr != segbase) 
          gasneti_fatalerror("Failed to acquire aligned segments for GASNET_ALIGNED_SEGMENTS");
      }
    }
  #endif

  gasnete_init(); /* init the extended API */

  /* ensure extended API is initialized across nodes */
  gasnetc_bootstrapBarrier();

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
static void gasnetc_atexit(void) {
    gasnetc_exit(0);
}

extern void gasnetc_exit(int exitcode) {
  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  gasneti_reghandler(SIGQUIT, SIG_IGN);

  {  /* ensure only one thread ever continues past this point */
    static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&exit_lock);
  }

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  gasneti_trace_finish();
  if (fflush(stdout)) 
    gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
  if (fflush(stderr)) 
    gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
  gasneti_sched_yield();
  sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */

  /*  add code here to terminate the job across _all_ nodes 
           with _exit(exitcode) (not regular exit()), preferably
           after raising a SIGQUIT to inform the client of the exit
  */
  _exit(exitcode);
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
  memcpy(seginfo_table, gasnetc_seginfo, numentries*sizeof(gasnet_seginfo_t));
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

  /* add code here to write the source index into sourceid */
  sourceid = 0;

  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  int retval;
  GASNETC_CHECKATTACH();

  /*  add code here to run your AM progress engine */

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
                         int dest, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {
  gasnetc_bufdesc_t _descbuf; 
  gasnetc_bufdesc_t *desc = &_descbuf;
  gasnet_handlerarg_t pargs[GASNETC_MAX_ARGS];

  assert(dest == gasnetc_mynode);
  assert(numargs >= 0 && numargs <= GASNETC_MAX_ARGS);

  { int i;
    for(i=0; i < numargs; i++) {
      pargs[i] = (gasnet_handlerarg_t)va_arg(argptr, int);
    }
  }

  desc->isReq = isReq;
  desc->handlerRunning = 1;
  desc->replyIssued = 0;
  switch (category) {
    case gasnetc_Short:
      { 
        if (isReq)
          GASNETI_TRACE_AMSHORT_REQHANDLER(handler, desc, numargs, pargs);
        else
          GASNETI_TRACE_AMSHORT_REPHANDLER(handler, desc, numargs, pargs);
        RUN_HANDLER_SHORT(gasnetc_handler[handler],desc,pargs,numargs);
      }
    break;
    case gasnetc_Medium:
      { 
        void **corethreadinfo = gasnetc_mythread();
        uint8_t *buf = NULL;
        assert(corethreadinfo);
        if (!*corethreadinfo) 
          *corethreadinfo = gasneti_malloc(sizeof(gasnetc_threadinfo_t));
        if (isReq) buf = ((gasnetc_threadinfo_t *)*corethreadinfo)->requestBuf;
        else       buf = ((gasnetc_threadinfo_t *)*corethreadinfo)->replyBuf;

        memcpy(buf, source_addr, nbytes);

        if (isReq)
          GASNETI_TRACE_AMMEDIUM_REQHANDLER(handler, desc, buf, nbytes, numargs, pargs);
        else
          GASNETI_TRACE_AMMEDIUM_REPHANDLER(handler, desc, buf, nbytes, numargs, pargs);
        RUN_HANDLER_MEDIUM(gasnetc_handler[handler],desc,pargs,numargs,buf,nbytes);
      }
    break;
    case gasnetc_Long:
      { 
        memmove(dest_ptr, source_addr, nbytes);

        if (isReq)
          GASNETI_TRACE_AMLONG_REQHANDLER(handler, desc, dest_ptr, nbytes, numargs, pargs);
        else
          GASNETI_TRACE_AMLONG_REPHANDLER(handler, desc, dest_ptr, nbytes, numargs, pargs);
        RUN_HANDLER_LONG(gasnetc_handler[handler],desc,pargs,numargs,dest_ptr,nbytes);
      }
    break;
    default: abort();
  }
  desc->handlerRunning = 0;

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
static int gasnetc_RequestGeneric(gasnetc_category_t category, 
                         int dest, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {

  gasnetc_AMPoll(); /* ensure progress */

  return gasnetc_ReqRepGeneric(category, 1, dest, handler, 
                               source_addr, nbytes, dest_ptr, 
                               numargs, argptr); 
}
/* ------------------------------------------------------------------------------------ */
static int gasnetc_ReplyGeneric(gasnetc_category_t category, 
                         gasnet_token_t token, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {
  gasnetc_bufdesc_t *reqdesc = (gasnetc_bufdesc_t *)token;
  int retval;
  gasnet_node_t sourceid = 0;

  assert(reqdesc->handlerRunning);
  assert(!reqdesc->replyIssued);
  assert(reqdesc->isReq);
  
  retval = gasnetc_ReqRepGeneric(category, 0, sourceid, handler, 
                                 source_addr, nbytes, dest_ptr, 
                                 numargs, argptr); 

  reqdesc->replyIssued = 1;
  return retval;
}
/* ------------------------------------------------------------------------------------ */

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETC_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Short, 
                                  dest, handler, 
                                  0, 0, 0,
                                  numargs, argptr);
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETC_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Medium, 
                                  dest, handler, 
                                  source_addr, nbytes, 0,
                                  numargs, argptr);
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETC_CHECKATTACH();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxLongRequest()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Long, 
                                  dest, handler, 
                                  source_addr, nbytes, dest_addr,
                                  numargs, argptr);
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETC_CHECKATTACH();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxLongRequest()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Long, 
                                  dest, handler, 
                                  source_addr, nbytes, dest_addr,
                                  numargs, argptr);
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_ReplyGeneric(gasnetc_Short, 
                                  token, handler, 
                                  0, 0, 0,
                                  numargs, argptr);
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_ReplyGeneric(gasnetc_Medium, 
                                  token, handler, 
                                  source_addr, nbytes, 0,
                                  numargs, argptr);
  va_end(argptr);
  GASNETI_RETURN(retval);
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
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxLongReply()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_ReplyGeneric(gasnetc_Long, 
                                  token, handler, 
                                  source_addr, nbytes, dest_addr,
                                  numargs, argptr);
  va_end(argptr);
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
#if 0
  extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKATTACH();
    /* add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKATTACH();
    /* add code here to re-enable handler interrupts for _this_ thread */
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
  }
  #endif

  /*  add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
  }
  #endif

  /*  add code here to cleanup conduit-specific HSL state */
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

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
      gasneti_fatalerror("In gasnetc_hsl_lock(), pthread_mutex_lock()=%s",strerror(retval));
    #if defined(STATS) || defined(TRACE)
      hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
      GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
    #endif
  }
  #elif defined(STATS) || defined(TRACE)
    hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
    GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, 0);
  #endif

  /*  conduits with interrupt-based handler dispatch need to add code here to 
           disable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock acquire and we're not inside an enclosing no-interrupt section)
   */
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

  /*  conduits with interrupt-based handler dispatch need to add code here to 
           re-enable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock release and we're not inside an enclosing no-interrupt section)
   */

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_unlock(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%s",strerror(retval));
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
