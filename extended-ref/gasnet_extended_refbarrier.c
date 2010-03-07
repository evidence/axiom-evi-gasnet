/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_extended_refbarrier.c,v $
 *     $Date: 2010/03/07 09:53:16 $
 * $Revision: 1.39 $
 * Description: Reference implemetation of GASNet Barrier, using Active Messages
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_REFBARRIER_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

#include <limits.h>
#include <gasnet_coll_internal.h>

/*  TODO: add more reference barrier implementation options (bug 264) */

/* ------------------------------------------------------------------------------------ */
/* state shared between barrier implementations */

#if GASNETI_STATS_OR_TRACE
  gasneti_tick_t gasnete_barrier_notifytime; /* for statistical purposes */ 
#endif

/*eventually this has to be changed so that all outstanding barriers are polled*/
/*keep a list of active barriers across all the teams. The poller walks the list and then kicks
 each one of them*/
/*XXX: for now only team all registers their pollers*/
gasneti_progressfn_t gasnete_barrier_pf= NULL;



/* ------------------------------------------------------------------------------------ */
/* the AM-based Dissemination implementation of barrier */

/*  an AM-based Dissemination barrier implementation:
     With N nodes, the barrier takes ceil(lg(N)) steps (lg = log-base-2).
     At step i (i=0..):
	node n first sends to node ((n + 2^i) mod N)
	then node n waits to receive (from node ((n + N - 2^i) mod N))
	once we receive for step i, we can move the step i+1 (or finish)
    The distributed nature makes this barrier more scalable than a centralized
     barrier, but also more sensitive to any lack of attentiveness to the
     network.
    We use a static allocation, limiting us to 2^GASNETE_AMBARRIER_MAXSTEP nodes.

    Algorithm is described in section 3.3 of
    John M. Mellor-Crummey and Michael L. Scott. "Algorithms for scalable synchronization
    on shared-memory multiprocessors." ACM ToCS, 9(1):21 65, 1991.
 */

typedef struct {
  gasnet_hsl_t amdbarrier_lock;
  gasnet_node_t *amdbarrier_peers; /* precomputed list of peers to communicate with */
  int volatile amdbarrier_value; /*  local ambarrier value */
  int volatile amdbarrier_flags; /*  local ambarrier flags */
  int volatile amdbarrier_step;  /*  local ambarrier step */
  int volatile amdbarrier_size;  /*  ceil(lg(nodes)) */
  int volatile amdbarrier_phase; /*  2-phase operation to improve pipelining */
  int volatile amdbarrier_step_done[2][GASNETE_AMDBARRIER_MAXSTEP]; /* non-zero when a step is complete */
  int volatile amdbarrier_mismatch[2];   /*  non-zero if we detected a mismatch */
  int volatile amdbarrier_recv_value[2]; /*  consensus ambarrier value */
  int volatile amdbarrier_recv_value_present[2]; /*  consensus ambarrier value is present */
} gasnete_coll_amdbarrier_t;
  
static void gasnete_amdbarrier_notify_reqh(gasnet_token_t token, 
                                           gasnet_handlerarg_t teamid, gasnet_handlerarg_t phase, gasnet_handlerarg_t step, gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  gasnete_coll_team_t team = gasnete_coll_team_lookup((uint32_t)teamid);
  gasnete_coll_amdbarrier_t *barrier_data = team->barrier_data;

  gasnet_hsl_lock(&barrier_data->amdbarrier_lock);
  { 
    /* Note we might not receive the steps in the numbered order.
     * We record the value received on the first one to actually arrive.
     * In subsequent steps we check for mismatch of received values.
     * The local value is compared in the kick function.
     */
    if (!(flags & (GASNET_BARRIERFLAG_ANONYMOUS|GASNET_BARRIERFLAG_MISMATCH)) && 
        !barrier_data->amdbarrier_recv_value_present[phase]) {  /* first named value we've seen */
      barrier_data->amdbarrier_recv_value[phase] = (int)value;
      gasneti_sync_writes();
      barrier_data->amdbarrier_recv_value_present[phase] = 1;
    } else if ((flags & GASNET_BARRIERFLAG_MISMATCH) || /* explicit mismatch */
               (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && /* 2nd+ named value and mismatch */
                 barrier_data->amdbarrier_recv_value[phase] != (int)value)) {
      barrier_data->amdbarrier_mismatch[phase] = 1;
    }
    
    gasneti_assert(barrier_data->amdbarrier_step_done[phase][step] == 0);
    
    gasneti_sync_writes();
    barrier_data->amdbarrier_step_done[phase][step] = 1;
  }
  gasnet_hsl_unlock(&barrier_data->amdbarrier_lock);
}

/* For a rmb() between unlocked reads of _recv_value_present and _recv_value
 * Equivalent to ``(gasneti_sync_reads(), ambarrier_recv_value[phase])'',
 * except w/o assuming gasneti_sync_reads() to be valid in expression context.
 */
GASNETI_INLINE(amdbarrier_recv_value_synced)
int amdbarrier_recv_value_synced(gasnete_coll_team_t team, int phase) {
  gasnete_coll_amdbarrier_t *barrier_data = team->barrier_data;
  gasneti_sync_reads();
  return barrier_data->amdbarrier_recv_value[phase];
}

void gasnete_amdbarrier_kick(gasnete_coll_team_t team) {
  gasnete_coll_amdbarrier_t *barrier_data = team->barrier_data;
  int phase = barrier_data->amdbarrier_phase;
  int step = barrier_data->amdbarrier_step;
  int numsteps = 0;
  gasnet_handlerarg_t flags, value;

  if (step == barrier_data->amdbarrier_size || !barrier_data->amdbarrier_step_done[phase][step]) 
    return; /* nothing to do */

  gasneti_assert(team->total_ranks > 1);

  gasnet_hsl_lock(&barrier_data->amdbarrier_lock);
    phase = barrier_data->amdbarrier_phase;
    step = barrier_data->amdbarrier_step;
    /* count steps we can take while holding the lock - must release before send,
       so coalesce as many as possible in one acquisition */
    while (step+numsteps < barrier_data->amdbarrier_size && barrier_data->amdbarrier_step_done[phase][step+numsteps]) {
      numsteps++;
    }

    if (numsteps) { /* completed one or more steps */
      gasneti_sync_reads(); /* between unlocked reads of _step_done and _mismatch */
      if_pf (barrier_data->amdbarrier_mismatch[phase] ||
	     ((barrier_data->amdbarrier_flags == 0) && 
	      barrier_data->amdbarrier_recv_value_present[phase] &&
	      (amdbarrier_recv_value_synced(team, phase) != barrier_data->amdbarrier_value))) {
        barrier_data->amdbarrier_flags = GASNET_BARRIERFLAG_MISMATCH;
        barrier_data->amdbarrier_mismatch[phase] = 1;
      }
      if (step+numsteps == barrier_data->amdbarrier_size) { /* We got the last recv - barrier locally complete */
        GASNETI_PROGRESSFNS_DISABLE(gasneti_pf_barrier,BOOLEAN);
        gasneti_sync_writes(); /* flush state before the write to ambarrier_step below */
      } 
      if (step + 1 < barrier_data->amdbarrier_size) {
        /* we will send at least one message - so calculate args */
        if ((barrier_data->amdbarrier_flags & GASNET_BARRIERFLAG_ANONYMOUS) &&
	    barrier_data->amdbarrier_recv_value_present[phase]) {
	  /* If we are on an node with an anonymous barrier invocation we
	   * may have received a barrier name from another node.  If so we
	   * must forward it to allow for matching tests.
	   */
	  gasneti_sync_reads(); /* Between unlocked reads of _recv_value_present and _recv_value */
	  flags = 0;
	  value = barrier_data->amdbarrier_recv_value[phase];
        } else {
	  value = barrier_data->amdbarrier_value;
	  flags = barrier_data->amdbarrier_flags;
        }
      }
      /* notify all threads of the step increase - 
         this may allow other local threads to proceed on the barrier and even indicate
         barrier completion while we overlap outgoing notifications to other nodes
      */
      barrier_data->amdbarrier_step = step+numsteps;
    } 
  gasnet_hsl_unlock(&barrier_data->amdbarrier_lock);

  for ( ; numsteps; numsteps--) {
    step++;
    if (step == barrier_data->amdbarrier_size) { /* no send upon reaching last step */
      gasneti_assert(numsteps == 1);
      break;
    }

    GASNETI_SAFE(
      gasnet_AMRequestShort5(barrier_data->amdbarrier_peers[step],
                             gasneti_handleridx(gasnete_amdbarrier_notify_reqh), 
                             team->team_id, phase, step, value, flags));
  }
}

static void gasnete_amdbarrier_notify(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_amdbarrier_t *barrier_data = team->barrier_data;
  int phase;
  
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(team->barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

  /* If we are on an ILP64 platform, this cast will ensure we truncate the same
   * bits locally as we do when passing over the network.
   */
  barrier_data->amdbarrier_value = (gasnet_handlerarg_t)id;

  phase = !barrier_data->amdbarrier_phase; /*  enter new phase */
  if_pf (flags & GASNET_BARRIERFLAG_MISMATCH) {
    barrier_data->amdbarrier_mismatch[phase] = 1;
    flags = GASNET_BARRIERFLAG_MISMATCH;
  }
  barrier_data->amdbarrier_flags = flags;
  barrier_data->amdbarrier_step = 0;
  gasneti_sync_writes(); 
  barrier_data->amdbarrier_phase = phase;

  if (team->total_ranks > 1) {
    /*  send notify msg to peer */
    GASNETI_SAFE(
      gasnet_AMRequestShort5(barrier_data->amdbarrier_peers[0],
                             gasneti_handleridx(gasnete_amdbarrier_notify_reqh), 
                             team->team_id, phase, 0, id, flags));
    GASNETI_PROGRESSFNS_ENABLE(gasneti_pf_barrier,BOOLEAN);
  } else {
    barrier_data->amdbarrier_recv_value[phase] = id;	/* to simplify checking in _wait */
  }

  /*  update state */
  team->barrier_splitstate = INSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}


static int gasnete_amdbarrier_wait(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_amdbarrier_t *barrier_data = team->barrier_data;
  int retval = GASNET_OK;
  int i;

  int phase;
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  phase = barrier_data->amdbarrier_phase;
  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");

  if (barrier_data->amdbarrier_step == barrier_data->amdbarrier_size) { /* completed asynchronously before wait (via progressfns or try) */
    GASNETI_TRACE_EVENT_TIME(B,BARRIER_ASYNC_COMPLETION,GASNETI_TICKS_NOW_IFENABLED(B)-gasnete_barrier_notifytime);
  } else { /*  wait for response */
    GASNET_BLOCKUNTIL((gasnete_amdbarrier_kick(team), barrier_data->amdbarrier_step == barrier_data->amdbarrier_size));
  }

  /* determine return value */
  if_pf((!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && (gasnet_handlerarg_t)id != barrier_data->amdbarrier_value) || 
        flags != barrier_data->amdbarrier_flags ||
	barrier_data->amdbarrier_mismatch[phase]) {
        barrier_data->amdbarrier_mismatch[phase] = 0;
	retval = GASNET_ERR_BARRIER_MISMATCH;
  }

  /*  update state */
  team->barrier_splitstate = OUTSIDE_BARRIER;
  for (i=0; i < barrier_data->amdbarrier_size; ++i) {
    barrier_data->amdbarrier_step_done[phase][i] = 0;
  }
  barrier_data->amdbarrier_recv_value_present[phase] = 0;
  gasneti_sync_writes(); /* ensure all state changes committed before return */

  return retval;
}

static int gasnete_amdbarrier_try(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_amdbarrier_t *barrier_data = team->barrier_data;
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */

  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  GASNETI_SAFE(gasneti_AMPoll());
  gasnete_amdbarrier_kick(team);

  if (barrier_data->amdbarrier_step == barrier_data->amdbarrier_size) return gasnete_amdbarrier_wait(team, id, flags);
  else return GASNET_ERR_NOT_READY;
}

void gasnete_amdbarrier_kick_team_all(void) {
  gasnete_amdbarrier_kick(GASNET_TEAM_ALL);
}

static void gasnete_amdbarrier_init(gasnete_coll_team_t team) {
  gasnete_coll_amdbarrier_t *barrier_data = gasneti_calloc(1,sizeof(gasnete_coll_amdbarrier_t));
  int steps;
  int64_t j;

  team->barrier_data = barrier_data;
  gasnet_hsl_init(&barrier_data->amdbarrier_lock);
  team->barrier_splitstate = OUTSIDE_BARRIER;

  /* determine barrier size (number of steps) */
  for (steps=0, j=1; j < team->total_ranks; ++steps, j*=2) ;

  barrier_data->amdbarrier_size = steps;
  gasneti_assert(barrier_data->amdbarrier_size <= GASNETE_AMDBARRIER_MAXSTEP);

  if (steps) {
    int step;

    barrier_data->amdbarrier_peers = gasneti_calloc(steps, sizeof(gasnet_node_t));
  
    for (step = 0; step < steps; ++step) {
      /* No need for a full mod because worst case is < 2*team->total_ranks.
       * However, we must take care for overflow if we try to do the
       * arithmetic in gasnet_node_t.  An example is gasnet_node_t
       * of uint8_t and team->total_ranks=250 nodes.  The largest value of
       * gasnet_mynode is 249 and the largest value of 2^step is 128.
       * We can't compute (249 + 128) mod 250 in 8-bit arithmetic.
       * If we are using GASNET_MAXNODES <= INT_MAX then we can
       * fit the arithmetic into unsigned integers (32-bit example is
       * 0x7ffffffe + 0x40000000 = 0xbffffffe).  Otherwise we are
       * confident that 64-bit integers are ALWAYS large enough.
       */
    #if (GASNET_MAXNODES <= INT_MAX)
      unsigned int tmp;
    #else
      uint64_t tmp;
    #endif
      gasnet_node_t peer;

      tmp = (1 << step) + team->myrank;
      peer = (tmp >= team->total_ranks) ? (tmp - team->total_ranks) : tmp;
      gasneti_assert(peer < team->total_ranks);

      barrier_data->amdbarrier_peers[step] = GASNETE_COLL_REL2ACT(team, peer);
    }
  }

  team->barrier_notify = &gasnete_amdbarrier_notify;
  team->barrier_wait =   &gasnete_amdbarrier_wait;
  team->barrier_try =    &gasnete_amdbarrier_try;
  if(team == GASNET_TEAM_ALL) gasnete_barrier_pf = &gasnete_amdbarrier_kick_team_all;
}

#define GASNETE_AMDBARRIER_HANDLERS()                                 \
  gasneti_handler_tableentry_no_bits(gasnete_amdbarrier_notify_reqh)

/* ------------------------------------------------------------------------------------ */
/* AM-based centralized implementation of barrier */

/*  a silly, centralized barrier implementation:
     everybody sends notifies to a single node, where we count them up
     central node eventually notices the barrier is complete (probably
     when it calls wait) and then it broadcasts the completion to all the nodes
    The main problem is the need for the master to call wait before the barrier can
     make progress - we really need a way for the "last thread" to notify all 
     the threads when completion is detected, but AM semantics don't provide a 
     simple way to do this.
    The centralized nature also makes it non-scalable - we really want to use 
     a tree-based barrier or pairwise exchange algorithm for scalability
     (but these impose even greater potential delays due to the lack of attentiveness to
     barrier progress)
 */

typedef struct {
  int volatile amcbarrier_value; /*  local ambarrier value */
  int volatile amcbarrier_flags; /*  local ambarrier flags */
  int volatile amcbarrier_phase; /*  2-phase operation to improve pipelining */
  int volatile amcbarrier_response_done[2];     /*  non-zero when ambarrier is complete */
  int volatile amcbarrier_response_mismatch[2]; /*  non-zero if we detected a mismatch */
  
  /*  global state on master */
  gasnet_hsl_t amcbarrier_lock;
  int volatile amcbarrier_consensus_value[2]; /*  consensus ambarrier value */
  int volatile amcbarrier_consensus_value_present[2]; /*  consensus ambarrier value found */
  int volatile amcbarrier_consensus_mismatch[2]; /*  non-zero if we detected a mismatch */
  int volatile amcbarrier_count[2];/*  count of how many remotes have notified (on master) */
} gasnete_coll_amcbarrier_t;

#ifndef GASNETE_AMCBARRIER_MASTER
#define GASNETE_AMCBARRIER_MASTER (team->total_ranks-1)
#endif

static void gasnete_amcbarrier_notify_reqh(gasnet_token_t token, 
                                           gasnet_handlerarg_t teamid, gasnet_handlerarg_t phase, gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  gasnete_coll_team_t team = gasnete_coll_team_lookup((uint32_t)teamid);
  gasnete_coll_amcbarrier_t *barrier_data = team->barrier_data;

  gasneti_assert(team->myrank == GASNETE_AMCBARRIER_MASTER);
  
  gasnet_hsl_lock(&barrier_data->amcbarrier_lock);
  { int count = barrier_data->amcbarrier_count[phase];
    if (!(flags & (GASNET_BARRIERFLAG_ANONYMOUS|GASNET_BARRIERFLAG_MISMATCH)) && 
        !barrier_data->amcbarrier_consensus_value_present[phase]) {
      barrier_data->amcbarrier_consensus_value[phase] = (int)value;
      barrier_data->amcbarrier_consensus_value_present[phase] = 1;
    } else if ((flags & GASNET_BARRIERFLAG_MISMATCH) ||
               (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && 
                barrier_data->amcbarrier_consensus_value[phase] != (int)value)) {
      barrier_data->amcbarrier_consensus_mismatch[phase] = 1;
    }
    count++;
    if (count == team->total_ranks) gasneti_sync_writes(); /* about to signal, ensure we flush state */
    barrier_data->amcbarrier_count[phase] = count;
  }
  gasnet_hsl_unlock(&barrier_data->amcbarrier_lock);
}

static void gasnete_amcbarrier_done_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t teamid, gasnet_handlerarg_t phase,  gasnet_handlerarg_t mismatch) {
  gasnete_coll_team_t team = gasnete_coll_team_lookup((uint32_t)teamid);
  gasnete_coll_amcbarrier_t *barrier_data = team->barrier_data;

  gasneti_assert(phase == barrier_data->amcbarrier_phase);

  barrier_data->amcbarrier_response_mismatch[phase] = mismatch;
  gasneti_sync_writes();
  barrier_data->amcbarrier_response_done[phase] = 1;
}

/*  make some progress on the ambarrier */
void gasnete_amcbarrier_kick(gasnete_coll_team_t team) {
  gasnete_coll_amcbarrier_t *barrier_data = team->barrier_data;
  int phase = barrier_data->amcbarrier_phase;

  if (team->myrank != GASNETE_AMCBARRIER_MASTER) return;

  /*  master does all the work */
  if (barrier_data->amcbarrier_count[phase] == team->total_ranks) {
    int gotit = 0;
    gasnet_hsl_lock(&barrier_data->amcbarrier_lock);
      if (barrier_data->amcbarrier_count[phase] == team->total_ranks) {
        barrier_data->amcbarrier_count[phase] = 0;
        gotit = 1;
      }
    gasnet_hsl_unlock(&barrier_data->amcbarrier_lock);

    if (gotit) { /*  ambarrier is complete */
      int i;
      int mismatch = barrier_data->amcbarrier_consensus_mismatch[phase];

      GASNETI_PROGRESSFNS_DISABLE(gasneti_pf_barrier,BOOLEAN);

      /*  inform the nodes */
      for (i=0; i < team->total_ranks; i++) {
        GASNETI_SAFE(
          gasnet_AMRequestShort3(GASNETE_COLL_REL2ACT(team, i), gasneti_handleridx(gasnete_amcbarrier_done_reqh), 
                                 team->team_id, phase, mismatch));
      }

      /*  reset state */
      barrier_data->amcbarrier_consensus_mismatch[phase] = 0;
      barrier_data->amcbarrier_consensus_value_present[phase] = 0;
    }
  }
}

static void gasnete_amcbarrier_notify(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_amcbarrier_t *barrier_data = team->barrier_data;
  int phase;

  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(team->barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

  /* If we are on an ILP64 platform, this cast will ensure we truncate the same
   * bits locally as we do when passing over the network.
   */
  barrier_data->amcbarrier_value = (gasnet_handlerarg_t)id;

  barrier_data->amcbarrier_flags = flags;
  phase = !barrier_data->amcbarrier_phase; /*  enter new phase */
  barrier_data->amcbarrier_phase = phase;

  if (team->total_ranks > 1) {
    /*  send notify msg to master */
    GASNETI_SAFE(
      gasnet_AMRequestShort4(GASNETE_COLL_REL2ACT(team, GASNETE_AMCBARRIER_MASTER), gasneti_handleridx(gasnete_amcbarrier_notify_reqh), 
                             team->team_id, phase, barrier_data->amcbarrier_value, flags));
    if (team->myrank == GASNETE_AMCBARRIER_MASTER) GASNETI_PROGRESSFNS_ENABLE(gasneti_pf_barrier,BOOLEAN);
  } else {
    barrier_data->amcbarrier_response_mismatch[phase] = (flags & GASNET_BARRIERFLAG_MISMATCH);
    barrier_data->amcbarrier_response_done[phase] = 1;
  }

  /*  update state */
  team->barrier_splitstate = INSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

static int gasnete_amcbarrier_wait(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_amcbarrier_t *barrier_data = team->barrier_data;
  int phase;

  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  phase = barrier_data->amcbarrier_phase;
  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");


  if (barrier_data->amcbarrier_response_done[phase]) { /* completed asynchronously before wait (via progressfns or try) */
    GASNETI_TRACE_EVENT_TIME(B,BARRIER_ASYNC_COMPLETION,GASNETI_TICKS_NOW_IFENABLED(B)-gasnete_barrier_notifytime);
  } else { /*  wait for response */
    GASNET_BLOCKUNTIL((gasnete_amcbarrier_kick(team), barrier_data->amcbarrier_response_done[phase]));
  }

  /*  update state */
  team->barrier_splitstate = OUTSIDE_BARRIER;
  barrier_data->amcbarrier_response_done[phase] = 0;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
  if_pf((!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && (gasnet_handlerarg_t)id != barrier_data->amcbarrier_value) || 
        flags != barrier_data->amcbarrier_flags || 
        barrier_data->amcbarrier_response_mismatch[phase]) {
        barrier_data->amcbarrier_response_mismatch[phase] = 0;
        return GASNET_ERR_BARRIER_MISMATCH;
  }
  else return GASNET_OK;
}

static int gasnete_amcbarrier_try(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_amcbarrier_t *barrier_data = team->barrier_data;

  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  GASNETI_SAFE(gasneti_AMPoll());
  gasnete_amcbarrier_kick(team);

  if (barrier_data->amcbarrier_response_done[barrier_data->amcbarrier_phase]) return gasnete_amcbarrier_wait(team, id, flags);
  else return GASNET_ERR_NOT_READY;
}

void gasnete_amcbarrier_kick_team_all(void) {
  gasnete_amcbarrier_kick(GASNET_TEAM_ALL);
}

static void gasnete_amcbarrier_init(gasnete_coll_team_t team) {
  gasnete_coll_amcbarrier_t *barrier_data = gasneti_calloc(1,sizeof(gasnete_coll_amcbarrier_t));

  gasnet_hsl_init(&barrier_data->amcbarrier_lock);
  team->barrier_splitstate = OUTSIDE_BARRIER;

  team->barrier_data =   barrier_data;
  team->barrier_notify = &gasnete_amcbarrier_notify;
  team->barrier_wait =   &gasnete_amcbarrier_wait;
  team->barrier_try =    &gasnete_amcbarrier_try;
  if(team == GASNET_TEAM_ALL) gasnete_barrier_pf = &gasnete_amcbarrier_kick_team_all;
}

#define GASNETE_AMCBARRIER_HANDLERS()                                 \
  gasneti_handler_tableentry_no_bits(gasnete_amcbarrier_notify_reqh), \
  gasneti_handler_tableentry_no_bits(gasnete_amcbarrier_done_reqh)  

/* ------------------------------------------------------------------------------------ */
/* Initialization and barrier mechanism selection */

static gasnete_coll_barrier_type_t gasnete_coll_default_barrier_type=GASNETE_COLL_BARRIER_ENVDEFAULT;

GASNETI_INLINE(gasnete_coll_barrier_notify_internal)
void gasnete_coll_barrier_notify_internal(gasnete_coll_team_t team, int id, int flags GASNETE_THREAD_FARG) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  gasneti_assert(team->barrier_notify);
#if GASNET_PAR
  if(flags & GASNET_BARRIERFLAG_IMAGES) {
    if(team->total_ranks >1) smp_coll_barrier(td->smp_coll_handle, 0);
    if(td->my_local_image == 0) (*team->barrier_notify)(team, id, flags);
  }  else 
#endif
    (*team->barrier_notify)(team, id, flags);  
}

GASNETI_INLINE(gasnete_coll_barrier_try_internal)
int gasnete_coll_barrier_try_internal(gasnete_coll_team_t team, int id, int flags GASNETE_THREAD_FARG) {
  int ret;
  gasneti_assert(team->barrier_try);
  

  /* currently there's no try version of the smp_coll_barriers*/
  /* so the try is not yet supported over the images*/
  gasneti_assert(!(flags & GASNET_BARRIERFLAG_IMAGES));
#if GASNET_PAR && 0
  {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
    if(td->my_local_image == 0) ret =  (*team->barrier_try)(team, id, flags);
    /*if the barrier has succeeded then call the local smp barrier on the way out*/
    /*if there is exactly one gasnet_node then the barrier on the notify is sufficient*/
    if(flags & GASNET_BARRIERFLAG_IMAGES && team->total_ranks > 1 && ret == GASNET_OK) {
      smp_coll_barrier(td->smp_coll_handle, 0);
    } 
    return ret;
  }
#else
  return (*team->barrier_try)(team, id, flags);
#endif

  
}

GASNETI_INLINE(gasnete_coll_barrier_wait_internal)
int gasnete_coll_barrier_wait_internal(gasnete_coll_team_t team, int id, int flags GASNETE_THREAD_FARG) {
  int ret;
  gasneti_assert(team->barrier_wait);
  
#if GASNET_PAR 
  if(flags & GASNET_BARRIERFLAG_IMAGES){
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
    if(td->my_local_image == 0) ret = (*team->barrier_wait)(team, id, flags);
    else ret = GASNET_OK;
    /*if the barrier has succeeded then call the local smp barrier on the way out*/
    /*if there is exactly one gasnet_node then the barrier on the notify is sufficient*/
    if(ret == GASNET_OK) smp_coll_barrier(td->smp_coll_handle, 0);
    return ret;
  } else
#endif
    return (*team->barrier_wait)(team, id, flags);
  
}

void gasnete_coll_barrier_notify(gasnete_coll_team_t team, int id, int flags GASNETE_THREAD_FARG) {
  gasnete_coll_barrier_notify_internal(team, id, flags GASNETE_THREAD_PASS);
}

int gasnete_coll_barrier_try(gasnete_coll_team_t team, int id, int flags GASNETE_THREAD_FARG) {
  return gasnete_coll_barrier_try_internal(team, id, flags GASNETE_THREAD_PASS);
}

int gasnete_coll_barrier_wait(gasnete_coll_team_t team, int id, int flags GASNETE_THREAD_FARG) {
  return gasnete_coll_barrier_wait_internal(team, id, flags GASNETE_THREAD_PASS);
}

/*the default gasnet_barrier_* as defined by the spec must only be called amongst the nodes by ONE representative image
   client is responsible for synchronizing images
 */
void gasnet_barrier_notify(int id, int flags) {
  GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(team=GASNET_TEAM_ALL,id=%i,flags=%i)", id, flags));
  #if GASNETI_STATS_OR_TRACE
    gasnete_barrier_notifytime = GASNETI_TICKS_NOW_IFENABLED(B);
  #endif

  gasneti_assert(GASNET_TEAM_ALL->barrier_notify);
  gasneti_assert(!(flags & GASNET_BARRIERFLAG_IMAGES));
  gasnete_coll_barrier_notify_internal(GASNET_TEAM_ALL, id, flags GASNETE_THREAD_GET);
}


int gasnet_barrier_wait(int id, int flags) {
  #if GASNETI_STATS_OR_TRACE
    gasneti_tick_t wait_start = GASNETI_TICKS_NOW_IFENABLED(B);
  #endif
  int retval;
  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_TICKS_NOW_IFENABLED(B)-gasnete_barrier_notifytime);
  
  gasneti_assert(GASNET_TEAM_ALL->barrier_wait);
  gasneti_assert(!(flags & GASNET_BARRIERFLAG_IMAGES));
  retval = gasnete_coll_barrier_wait_internal(GASNET_TEAM_ALL, id, flags GASNETE_THREAD_GET);
 
  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_TICKS_NOW_IFENABLED(B)-wait_start);
  return retval;
}

int gasnet_barrier_try(int id, int flags) {
  int retval;

  gasneti_assert(GASNET_TEAM_ALL->barrier_try);
  gasneti_assert(!(flags & GASNET_BARRIERFLAG_IMAGES));
  
  retval = gasnete_coll_barrier_try_internal(GASNET_TEAM_ALL, id, flags GASNETE_THREAD_GET);

  GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,(retval != GASNET_ERR_NOT_READY));
  return retval;
}

extern void gasnete_coll_barrier_init(gasnete_coll_team_t team,  int barrier_type_in) {
#ifndef GASNETE_BARRIER_DEFAULT
  /* conduit plugin for default barrier mechanism */
#define GASNETE_BARRIER_DEFAULT "AMDISSEM"
#endif
  gasnete_coll_barrier_type_t barrier_type= (gasnete_coll_barrier_type_t) barrier_type_in;
  static int envdefault_set = 0;
  
  
  if(!envdefault_set) {
    /* fetch user barrier selection */
    const char *selection = gasneti_getenv_withdefault("GASNET_BARRIER",GASNETE_BARRIER_DEFAULT);
    char tmp[255];
    char options[255];
    int i;
    for (i = 0; selection[i] && i < sizeof(tmp)-1; i++) {
      tmp[i] = toupper(selection[i]); /* normalize to uppercase */
    }
    tmp[i] = '\0';
    selection = tmp;
    options[0] = '\0';
#define GASNETE_ISBARRIER(namestr) \
((options[0]?strcat(options, ", "),(void)0:(void)0),strcat(options, namestr), \
!strcmp(selection, namestr))
    
    if(GASNETE_ISBARRIER("AMDISSEM")) gasnete_coll_default_barrier_type = GASNETE_COLL_BARRIER_AMDISSEM;
    else if(GASNETE_ISBARRIER("AMCENTRAL")) gasnete_coll_default_barrier_type = GASNETE_COLL_BARRIER_AMCENTRAL;
#ifdef GASNETE_BARRIER_READENV
    else {
      GASNETE_BARRIER_READENV();
    }
#endif
    if(gasnete_coll_default_barrier_type==0) {
      gasneti_fatalerror("GASNET_BARRIER=%s is not a recognized barrier mechanism. "
                         "Available mechanisms are: %s", selection, options);
    }
    
  }
  if(team==NULL) { /*global barrier hasn't been initialized yet so take care of it*/
    team = GASNET_TEAM_ALL = (gasnete_coll_team_t) gasneti_malloc(sizeof(struct gasnete_coll_team_t_));
    team->team_id=0;
    team->myrank = gasneti_mynode;
    team->total_ranks = gasneti_nodes;
    team->team_id=0; 
  }
  
  
  if(barrier_type == 0) barrier_type = gasnete_coll_default_barrier_type;
  
  #ifndef GASNETE_BARRIER_INIT
  /* conduit plugin to select a barrier - 
     should use GASNETE_ISBARRIER("whatever") to check if enabled, and then set the
     barrier function pointers */
  #define GASNETE_BARRIER_INIT(team, barrier_type)
  #endif
  /*reset the barrier types*/
  team->barrier_data = NULL;
  team->barrier_notify = NULL;
  team->barrier_wait = NULL;
  team->barrier_try = NULL;
  GASNETE_BARRIER_INIT(team, barrier_type);
  if (team->barrier_notify) { /* conduit has identified a barrier mechanism */
    /*make sure that wait and try were also defined*/
    gasneti_assert(team->barrier_wait && team->barrier_try);
    return;
  } else if (barrier_type == GASNETE_COLL_BARRIER_AMDISSEM || !team->barrier_notify) {
    /*either we want an AM dissem barrier or the conduit couldn't define it's own barrier so fallback*/
    gasnete_amdbarrier_init(team);
  } else if (barrier_type == GASNETE_COLL_BARRIER_AMCENTRAL) {
    /*we explicitly specify that we want an AM CENTRAL Barrier*/
    gasnete_amcbarrier_init(team);
  } else {
    gasneti_fatalerror("NO BARREIRS SET for this team!!");
  }
}
/* ------------------------------------------------------------------------------------ */
#define GASNETE_REFBARRIER_HANDLERS() \
        GASNETE_AMDBARRIER_HANDLERS(), \
        GASNETE_AMCBARRIER_HANDLERS()
