/*  $Archive:: /Ti/GASNet/mpi-conduit/gasnet_core.c                       $
 *     $Date: 2002/08/30 03:17:58 $
 * $Revision: 1.9 $
 * Description: GASNet MPI conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <ammpi_spmd.h>

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
static gasneti_atomic_t segsrecvd = gasneti_atomic_init(0); /*  used for bootstrapping */
eb_t gasnetc_bundle;
ep_t gasnetc_endpoint;

/* TODO: fix this stupid, temporary hack */
#define GASNETC_MAXSHAREDSEG_SZ (10*1048576)
uint8_t gasnetc_uglyevilhack[GASNETC_MAXSHAREDSEG_SZ+(16*1024)];

static int gasnetc_init_done = 0; /*  true after init */
static int gasnetc_attach_done = 0; /*  true after attach */

#ifdef GASNET_PAR
  pthread_mutex_t gasnetc_AMlock = PTHREAD_MUTEX_INITIALIZER; /*  protect access to AMMPI */
#endif

#ifdef GASNETC_HSL_ERRCHECK
  extern void gasnetc_enteringHandler_hook();
  extern void gasnetc_leavingHandler_hook();

  /* check a call is legally outside an NIS or HSL */
  void gasnetc_checkcallNIS();
  void gasnetc_checkcallHSL();
  #define CHECKCALLNIS() gasnetc_checkcallNIS()
  #define CHECKCALLHSL() gasnetc_checkcallHSL()
#else
  #define CHECKCALLNIS()
  #define CHECKCALLHSL()
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  assert(GASNET_MAXNODES <= AMMPI_MAX_SPMDPROCS);
  assert(AMMPI_MAX_NUMHANDLERS >= 256);
  assert(AMMPI_MAX_SEGLENGTH == (uintptr_t)-1);
}

#define INITERR(type, reason) do {                                      \
   if (gasneti_VerboseErrors) {                                         \
     fprintf(stderr, "GASNet initialization encountered an error: %s\n" \
      "  at %s\n",                                                      \
      #reason, gasneti_current_loc);                                    \
   }                                                                    \
   retval = GASNET_ERR_ ## type;                                        \
   goto done;                                                           \
 } while (0)

static int gasnetc_init(int *argc, char ***argv) {
  int retval = GASNET_OK;
  int networkdepth = 0;

  AMLOCK();
    if (gasnetc_init_done) 
      INITERR(NOT_INIT, "GASNet already initialized");

    /*  check system sanity */
    gasnetc_check_config();

    #if DEBUG_VERBOSE
      /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
      fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
    #endif

    /*  choose network depth */
    if (getenv("GASNET_NETWORKDEPTH")) { 
       networkdepth = atoi(getenv("GASNET_NETWORKDEPTH"));
    }

    AMMPI_VerboseErrors = gasneti_VerboseErrors;

    /*  perform job spawn */
    retval = AMMPI_SPMDStartup(argc, argv, networkdepth, NULL, &gasnetc_bundle, &gasnetc_endpoint);
    if (retval != AM_OK) INITERR(RESOURCE, "AMMPI_SPMDStartup() failed");

    gasnetc_mynode = AMMPI_SPMDMyProc();
    gasnetc_nodes = AMMPI_SPMDNumProcs();

    #if DEBUG_VERBOSE
      fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
        gasnetc_mynode, gasnetc_nodes); fflush(stderr);
    #endif

    #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
      { /* TODO: a real algorithm for determining segment availability */
        gasnetc_MaxLocalSegmentSize = GASNETC_MAXSHAREDSEG_SZ;
        gasnetc_MaxGlobalSegmentSize = GASNETC_MAXSHAREDSEG_SZ;
      }
    #elif defined(GASNET_SEGMENT_EVERYTHING)
      gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
      gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
    #else
      #error Bad segment config
    #endif

    gasnetc_init_done = 1;  
  AMUNLOCK();

  assert(retval == GASNET_OK);
  return retval;

done: /*  error return while locked */
  AMUNLOCK();
  GASNETI_RETURN(retval);
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
    if (AM_SetHandler(gasnetc_endpoint, (handler_t)newindex, table[i].fnptr) != AM_OK) 
      GASNETI_RETURN_ERRR(RESOURCE, "AM_SetHandler() failed while registering handlers");

    if (dontcare) table[i].index = newindex;
    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
  int retval = GASNET_OK;
  size_t pagesize = gasneti_getSystemPageSize();
  void *segbase = NULL;
  
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%i, minheapoffset=%i)",
                          numentries, (int)segsize, (int)minheapoffset));
  AMLOCK();
    if (!gasnetc_init_done) 
      INITERR(NOT_INIT, "GASNet attach called before init");
    if (gasnetc_attach_done) 
      INITERR(NOT_INIT, "GASNet already attached");

    /*  check argument sanity */
    #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
      if ((segsize % pagesize) != 0) 
        INITERR(BAD_ARG, "segsize not page-aligned");
      if (segsize > gasnetc_getMaxLocalSegmentSize()) 
        INITERR(BAD_ARG, "segsize too large");
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
      if (gasnetc_reghandlers(ctable, len, 1, 99, 0, &numreg) != GASNET_OK)
        INITERR(RESOURCE,"Error registering core API handlers");
      assert(numreg == len);
    }

    { /*  extended API handlers */
      gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
      int len = 0;
      int numreg = 0;
      assert(etable);
      while (etable[len].fnptr) len++; /* calc len */
      if (gasnetc_reghandlers(etable, len, 100, 199, 0, &numreg) != GASNET_OK)
        INITERR(RESOURCE,"Error registering extended API handlers");
      assert(numreg == len);
    }

    if (table) { /*  client handlers */
      int numreg1 = 0;
      int numreg2 = 0;

      /*  first pass - assign all fixed-index handlers */
      if (gasnetc_reghandlers(table, numentries, 200, 255, 0, &numreg1) != GASNET_OK)
        INITERR(RESOURCE,"Error registering fixed-index client handlers");

      /*  second pass - fill in dontcare-index handlers */
      if (gasnetc_reghandlers(table, numentries, 200, 255, 1, &numreg2) != GASNET_OK)
        INITERR(RESOURCE,"Error registering fixed-index client handlers");

      assert(numreg1 + numreg2 == numentries);
    }

    /* ------------------------------------------------------------------------------------ */
    /*  register fatal signal handlers */

    /*  TODO: catch fatal signals and convert to SIGQUIT */

    /* ------------------------------------------------------------------------------------ */
    /*  register segment  */

    #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
      if (segsize == 0) segbase = NULL; /* no segment */
      else {
        /* TODO: fix this to allocate dynamically using mmap fixed */
        segbase = gasnetc_uglyevilhack;

        /*  ensure page-alignment of base */
        if ((((uintptr_t)segbase) % pagesize) != 0) {
          uint8_t *mem = (uint8_t *)segbase;
          segbase = (void *) (mem + (pagesize - (((uintptr_t)mem) % pagesize)));
        }

        assert(((uintptr_t)segbase) % pagesize == 0);
        assert(segsize % pagesize == 0);
      }
    #else
      /* GASNET_SEGMENT_EVERYTHING */
      segbase = (void *)0;
      segsize = (uintptr_t)-1;
    #endif

    /*  AMMPI allows arbitrary registration with no further action  */
    if (segbase) {
      retval = AM_SetSeg(gasnetc_endpoint, segbase, segsize);
      if (retval != AM_OK) INITERR(RESOURCE, "AM_SetSeg() failed");
    }

    /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
       hold/resume interrupts functions are operational yet */
    gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));
    assert(gasneti_atomic_read(&segsrecvd) == 0);
    /* ------------------------------------------------------------------------------------ */
    /*  primary attach complete */
    gasnetc_attach_done = 1;
    retval = AMMPI_SPMDBarrier();
    if (retval != AM_OK) INITERR(RESOURCE, "AMMPI_SPMDBarrier() failed");
  AMUNLOCK();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete\n"));

  #ifdef GASNETC_HSL_ERRCHECK
    GASNETI_AM_SAFE(AMMPI_SetHandlerCallbacks(gasnetc_endpoint,
      gasnetc_enteringHandler_hook, gasnetc_leavingHandler_hook));
  #endif
  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  /*  everybody exchange segment info - not very scalable, but screw it, this is startup */
  /*  (here we also discover if AM is broken) */
  { int i;
    for (i=0; i < gasnetc_nodes; i++) {
      GASNETC_SAFE(SHORT_REQ(2,4,(i, gasneti_handleridx(gasnetc_get_seginfo_req), 
                              PACK(segbase), PACK(segsize))));
    }
  }

  while (gasneti_atomic_read(&segsrecvd) != gasnetc_nodes) {
    GASNETC_SAFE(gasnet_AMPoll());
  }

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
  GASNETI_AM_SAFE(AMMPI_SPMDBarrier()); 
  
  assert(retval == GASNET_OK);
  return retval;

done: /*  error return while locked */
  AMUNLOCK();
  GASNETI_RETURN(retval);
}
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnetc_get_seginfo_req_inner)
void gasnetc_get_seginfo_req_inner(gasnet_token_t token, 
  void *segbase, void *_segsize) {
  uintptr_t segsize = (uintptr_t)_segsize;
  gasnet_node_t srcindex;
  int retval = gasnetc_AMGetMsgSource(token, &srcindex);
  assert(retval == GASNET_OK);
  gasnetc_seginfo[srcindex].addr = segbase;
  gasnetc_seginfo[srcindex].size = segsize;
  gasneti_atomic_increment(&segsrecvd);
}
SHORT_HANDLER(gasnetc_get_seginfo_req,2,4, 
              (token, UNPACK(a0),     UNPACK(a1)    ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_exit(int exitcode) {
  gasneti_trace_finish();
  AMMPI_SPMDExit(exitcode);
  abort();
}

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
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
  ammpi_buf_t *p;
  gasnet_node_t sourceid;
  GASNETC_CHECKATTACH();
  if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");
  p = (ammpi_buf_t *)token;
  *srcindex = GASNET_MAXNODES;
  if (!p->status.handlerRunning) GASNETI_RETURN_ERRR(RESOURCE,"handler not running");
  sourceid = p->status.sourceId;
  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  int retval;
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
  AMLOCK();
    retval = GASNETI_AM_SAFE_NORETURN(AM_Poll(gasnetc_bundle));
  AMUNLOCK();
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
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
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMMPI_RequestVA(gasnetc_endpoint, dest, handler, 
                               numargs, argptr));
    AMUNLOCK();
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
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMMPI_RequestIVA(gasnetc_endpoint, dest, handler, 
                                source_addr, nbytes, 
                                numargs, argptr));
    AMUNLOCK();
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
  uintptr_t dest_offset;
  va_list argptr;
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();

  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
  dest_offset = ((uintptr_t)dest_addr) - ((uintptr_t)gasnetc_seginfo[dest].addr);

  GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMMPI_RequestXferVA(gasnetc_endpoint, dest, handler, 
                                   source_addr, nbytes, 
                                   dest_offset, 0,
                                   numargs, argptr));
    AMUNLOCK();
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
  CHECKCALLHSL();
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    retval = GASNETI_AM_SAFE_NORETURN(
              AMMPI_ReplyVA(token, handler, numargs, argptr));
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
  CHECKCALLHSL();
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    retval = GASNETI_AM_SAFE_NORETURN(
              AMMPI_ReplyIVA(token, handler, source_addr, nbytes, numargs, argptr));
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
  uintptr_t dest_offset;
  gasnet_node_t dest;
  va_list argptr;
  
  CHECKCALLHSL();
  retval = gasnet_AMGetMsgSource(token, &dest);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
  dest_offset = ((uintptr_t)dest_addr) - ((uintptr_t)gasnetc_seginfo[dest].addr);

  GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    retval = GASNETI_AM_SAFE_NORETURN(
              AMMPI_ReplyXferVA(token, handler, source_addr, nbytes, dest_offset, numargs, argptr));
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
*/
/* AMMPI does not use interrupts, but we provide an optional error-checking implementation of 
   handler-safe locks to assist in debugging client code
 */

#ifdef GASNETC_HSL_ERRCHECK
  typedef struct { /* pre-thread HSL err-checking info */
    gasnet_hsl_t *locksheld;
    int interruptsdisabled;
    int inhandler;
    int64_t NIStimestamp;
  } gasnetc_hsl_errcheckinfo_t;
  static gasnetc_hsl_errcheckinfo_t _info_init = { NULL, 0, 0 };

  #ifdef GASNETI_THREADS
    static pthread_key_t gasnetc_hsl_errcheckinfo; /*  pthread thread-specific ptr to our info (or NULL for a thread never-seen before) */
    static int gasnetc_hsl_errcheckinfo_firsttime = 1;
    static gasnetc_hsl_errcheckinfo_t *gasnetc_get_errcheckinfo() {
      gasnetc_hsl_errcheckinfo_t *info;
      if (gasnetc_hsl_errcheckinfo_firsttime) { 
        int retval = pthread_key_create(&gasnetc_hsl_errcheckinfo, NULL);
        if (retval) gasneti_fatalerror("Failure in pthread_key_create()=%s",strerror(retval));
        gasnetc_hsl_errcheckinfo_firsttime = 0;
      }
      info = pthread_getspecific(gasnetc_hsl_errcheckinfo);
      if_pt (info) return info;

      /*  first time we've seen this thread - need to set it up */
      { int retval;
        info = (gasnetc_hsl_errcheckinfo_t *)malloc(sizeof(gasnetc_hsl_errcheckinfo_t));
        memcpy(info, &_info_init, sizeof(gasnetc_hsl_errcheckinfo_t));
        retval = pthread_setspecific(gasnetc_hsl_errcheckinfo, info);
        assert(!retval);
        return info;
      }
    }
  #else
    static gasnetc_hsl_errcheckinfo_t *gasnetc_get_errcheckinfo() {
      return &_info_init;
    }
  #endif


  extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKATTACH();
    { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
      if (info->inhandler)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to disable interrupts while running a handler");
      if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to disable interrupts while holding an HSL");
      if (info->interruptsdisabled)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to disable interrupts when they were already disabled");
      info->interruptsdisabled = 1;
      info->NIStimestamp = gasneti_getMicrosecondTimeStamp();
    }
  }
  extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKATTACH();
    { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
      if (info->inhandler)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to resume interrupts while running a handler");
      if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to resume interrupts while holding an HSL");
      if (!info->interruptsdisabled)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to resume interrupts when they were not disabled");
      { float NIStime = (float)(gasneti_getMicrosecondTimeStamp() - info->NIStimestamp);
        if (NIStime > GASNETC_NISTIMEOUT_WARNING_THRESHOLD) {
          fprintf(stderr,"HSL USAGE WARNING: held a no-interrupt section for a long interval (%8.3f sec)\n", NIStime/1000000.0);
          fflush(stderr);
        }
      }
      info->interruptsdisabled = 0;
    }
  }

  void gasnetc_checkinit() {
    if (!gasnetc_init_done)
      gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
  }
  void gasnetc_checkattach() {
    if (!gasnetc_attach_done)
      gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
  }
  void gasnetc_checkcallNIS() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    if (info->interruptsdisabled)
      gasneti_fatalerror("Illegal call to GASNet within a No-Interrupt Section");
    if (info->inhandler)
      gasneti_fatalerror("Illegal call to GASNet within a No-Interrupt Section (imposed by handler context)");
    gasnetc_checkcallHSL();
  }
  void gasnetc_checkcallHSL() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    if (info->locksheld)
      gasneti_fatalerror("Illegal call to GASNet while holding a Handler-Safe Lock");
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  {
    if (hsl->tag == GASNETC_HSL_ERRCHECK_TAGINIT)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_init() a statically-initialized HSL");
    if (hsl->tag == GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_init() a previously-initialized HSL (or one you forgot to destroy)");
    hsl->tag = GASNETC_HSL_ERRCHECK_TAGDYN;
    hsl->next = NULL;
    hsl->islocked = 0;
  }
  #endif
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
  }
  #endif
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  {
    if (hsl->tag != GASNETC_HSL_ERRCHECK_TAGINIT && hsl->tag != GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_destroy() an uninitialized HSL");
    if (hsl->islocked)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_destroy() a locked HSL");
    hsl->tag = 0;
    assert(!hsl->next);
  }
  #endif
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
  }
  #endif
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    gasnet_hsl_t *heldhsl = info->locksheld;
    if (hsl->tag != GASNETC_HSL_ERRCHECK_TAGINIT && hsl->tag != GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_lock() an uninitialized HSL");
    while (heldhsl) {
      if (heldhsl == hsl)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to recursively gasnetc_hsl_lock() an HSL");
      heldhsl = heldhsl->next;
    }
  }
  #endif

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


  #ifdef GASNETC_HSL_ERRCHECK
  { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    hsl->islocked = 1;
    hsl->next = info->locksheld;
    info->locksheld = hsl;
    hsl->timestamp = gasneti_getMicrosecondTimeStamp();
  }
  #endif
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    gasnet_hsl_t *heldhsl = info->locksheld;
    if (hsl->tag != GASNETC_HSL_ERRCHECK_TAGINIT && hsl->tag != GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_unlock() an uninitialized HSL");
    while (heldhsl) {
      if (heldhsl == hsl) break;
      heldhsl = heldhsl->next;
    }
    if (!heldhsl)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_unlock() an HSL I didn't own");
    if (info->locksheld != hsl)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnetc_hsl_unlock() an HSL out of order");
    { float NIStime = (float)(gasneti_getMicrosecondTimeStamp() - hsl->timestamp);
      if (NIStime > GASNETC_NISTIMEOUT_WARNING_THRESHOLD) {
        fprintf(stderr,"HSL USAGE WARNING: held an HSL for a long interval (%8.3f sec)\n", NIStime/1000000.0);
        fflush(stderr);
      }
    }
    hsl->islocked = 0;
    info->locksheld = hsl->next;
  }
  #endif

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_unlock(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%s",strerror(retval));
  }
  #endif
}

#ifdef GASNETC_HSL_ERRCHECK
  /* called when entering/leaving handler - also called when entering/leaving AM_Reply call */
  extern void gasnetc_enteringHandler_hook() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    assert(!info->inhandler);
    if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to make a GASNet network call while holding an HSL");
    if (info->interruptsdisabled)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to make a GASNet network call with interrupts disabled");
    info->inhandler = 1;
  }
  extern void gasnetc_leavingHandler_hook() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    assert(info->inhandler);
    assert(!info->interruptsdisabled);
    if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to exit a handler while holding an HSL");
    info->inhandler = 0;
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Private Handlers:
  ================
*/
static gasnet_handlerentry_t const gasnetc_handlers[] = {
  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */
  gasneti_handler_tableentry_with_bits(gasnetc_get_seginfo_req),

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */
