/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2002/06/14 00:27:55 $
 * $Revision: 1.5 $
 * Description: GASNet <conduitname> conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>

extern const char gasnetc_IdentString_Version[];
const char gasnetc_IdentString_Version[] = "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $";

gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = -1;
gasnet_node_t gasnetc_nodes = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;

int gasnetc_init_done = 0; /*  true after init */
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
      GASNETI_RETURN_ERRR(RESOURCE, "client requested GASNET_SEGSIZE_EVERYTHING, but !allowFaults"
        "(asking us to mmap entire VA space - don't even try to do that)");

  /* (###) add code here to bootstrap the system for your conduit */

  gasnetc_mynode = ###;
  gasnetc_nodes = ###;

  /* ------------------------------------------------------------------------------------ */
  /*  register handlers */
  memset(checkuniqhandler, 0, 256);
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
      /* (###) add code here to register ctable[i].fnptr on index (gasnet_handler_t)ctable[i].index */
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
      /* (###) add code here to register etable[i].fnptr on index (gasnet_handler_t)etable[i].index */
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
        /* (###) add code here to register table[i].fnptr on index (gasnet_handler_t)table[i].index */
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
        /* (###) add code here to register table[i].fnptr at index tmp */
        table[i].index = tmp;
        /* discover duplicates */
        assert(checkuniqhandler[table[i].index] == 0);
        checkuniqhandler[table[i].index] = 1;
      }
    }
  }

  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /*  (###) catch fatal signals and convert to SIGQUIT */

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  if (segbase == GASNET_SEGBASE_ANY) {
    /*  (###) choose a segment for the user of size segsize and allocate/register it,
              save base in segbase
    */
  } else {
    /*  (###) allocate/register the client requested segment in (segbase,segsize) */
  }

  assert(segbase % pagesize == 0);
  assert(segsize % pagesize == 0);

  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));
  /* (###) add code here to fill in gasnetc_seginfo with segment info from all the nodes */

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

  /* (###) add a barrier here */

  /*  init complete */
  gasnetc_init_done = 1;

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
  /* (###) add code here to terminate the job across all nodes */
  abort();
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
  GASNETC_CHECKINIT();
  if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  /* (###) add code here to write the source index into sourceid */
  sourceid = ###;

  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  int retval;
  GASNETC_CHECKINIT();

  /* (###) add code here to run your AM progress engine */

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
  GASNETC_CHECKINIT();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = ###;
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
  GASNETC_CHECKINIT();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = ###;
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
  GASNETC_CHECKINIT();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

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
#if ###
  extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKINIT();
    /* (###) add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKINIT();
    /* (###) add code here to re-enable handler interrupts for _this_ thread */
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

  /* (###) add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%i",strerror(retval));
  }
  #endif

  /* (###) add code here to cleanup conduit-specific HSL state */
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

  /* (###) conduits with interrupt-based handler dispatch need to add code here to 
           disable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock acquire and we're not inside an enclosing no-interrupt section)
   */
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();

  /* (###) conduits with interrupt-based handler dispatch need to add code here to 
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
