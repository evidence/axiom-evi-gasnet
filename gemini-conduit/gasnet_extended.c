/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gemini-conduit/gasnet_extended.c,v $
 *     $Date: 2013/05/20 02:13:07 $
 * $Revision: 1.58 $
 * Description: GASNet Extended API over Gemini Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>
#include <gasnet_gemini.h>

static const gasnete_eopaddr_t EOPADDR_NIL = { { 0xFF, 0xFF } };
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }
/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
*/

/* Maximum length of an RDMA op with local address INside the segment.
   GNI_PostRdma() as a limit of 2^32-1, but we pick a 4MB aligned value
 */
#ifndef GC_MAXRDMA_IN 
#define GC_MAXRDMA_IN 0xFFC00000
#endif

/* Maximum length of an RDMA op with local address OUTside the segment.
   Choice determines length of dynamic memory registrations
 */
#ifndef GC_MAXRDMA_OUT 
#define GC_MAXRDMA_OUT 0x100000
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Extended API Common Code
  ========================
  Factored bits of extended API code common to most conduits, overridable when necessary
*/

/* ensure thread cleanup uses our custom for valget handles */
#define GASNETE_VALGET_FREEALL(thread) gasnete_valget_freeall(thread)
static void gasnete_valget_freeall(gasnete_threaddata_t *thread);

#include "gasnet_extended_common.c"

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnete_check_config(void) {
  gasneti_check_config_postattach();

  gasneti_assert_always(gasnete_eopaddr_isnil(EOPADDR_NIL));
}

extern void gasnete_init(void) {
  GASNETI_UNUSED_UNLESS_DEBUG static int firstcall = 1;
  GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
  gasneti_assert(firstcall); /*  make sure we haven't been called before */
  firstcall = 0;

  gasnete_check_config(); /*  check for sanity */

  gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

  { gasnete_threaddata_t *threaddata = NULL;
    gasnete_eop_t *eop = NULL;
    #if GASNETI_MAX_THREADS > 1
      /* register first thread (optimization) */
      threaddata = gasnete_mythread(); 
    #else
      /* register only thread (required) */
      threaddata = gasnete_new_threaddata();
    #endif

    /* cause the first pool of eops to be allocated (optimization) */
    eop = gasnete_eop_new(threaddata);
    gasnete_op_markdone((gasnete_op_t *)eop, 0);
    gasnete_op_free((gasnete_op_t *)eop);
  }

  /* Initialize barrier resources */
  gasnete_barrier_init();

  /* Initialize VIS subsystem */
  gasnete_vis_init();
}

/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/
/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eopaddr_t head = thread->eop_free;
  if_pt (!gasnete_eopaddr_isnil(head)) {
    gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
    thread->eop_free = eop->addr;
    eop->addr = head;
    gasneti_assert(!gasnete_eopaddr_equal(thread->eop_free,head));
    gasneti_assert(eop->threadidx == thread->threadidx);
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
    gasneti_assert(OPTYPE(eop) == OPSTATE_FREE);
    SET_OPSTATE(eop, OPSTATE_INFLIGHT);
    GASNETI_TRACE_PRINTF(C,("new eop = %lu",(unsigned long)eop));
    return eop;
  } else { /*  free list empty - need more eops */
    int bufidx = thread->eop_num_bufs;
    gasnete_eop_t *buf;
    int i;
    gasnete_threadidx_t threadidx = thread->threadidx;
    if (bufidx == 256) gasneti_fatalerror("GASNet Extended API: Ran out of explicit handles (limit=65535)");
    thread->eop_num_bufs++;
    buf = (gasnete_eop_t *)gasneti_calloc(256,sizeof(gasnete_eop_t));
    gasneti_leak(buf);
    for (i=0; i < 256; i++) {
      gasnete_eopaddr_t addr;
      addr.bufferidx = bufidx;
      #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
        #ifdef GASNETE_EOP_MOD
          addr.eopidx = (i+32) % 255;
        #else
          { int k = i+32;
            addr.eopidx = k > 255 ? k - 255 : k;
          }
        #endif
      #else
        addr.eopidx = i+1;
      #endif
      buf[i].threadidx = threadidx;
      buf[i].addr = addr;
      #if 0 /* these can safely be skipped when the values are zero */
        SET_OPSTATE(&(buf[i]),OPSTATE_FREE); 
        SET_OPTYPE(&(buf[i]),OPTYPE_EXPLICIT); 
      #endif
    }
     /*  add a list terminator */
    #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
      #ifdef GASNETE_EOP_MOD
        buf[223].addr.eopidx = 255; /* modular arithmetic messes up this one */
      #endif
      buf[255].addr = EOPADDR_NIL;
    #else
      buf[255].addr = EOPADDR_NIL;
    #endif
    thread->eop_bufs[bufidx] = buf;
    head.bufferidx = bufidx;
    head.eopidx = 0;
    thread->eop_free = head;

    #if GASNET_DEBUG
    { /* verify new free list got built correctly */
      int i;
      int seen[256];
      gasnete_eopaddr_t addr = thread->eop_free;

      #if 0
      if (gasneti_mynode == 0)
        for (i=0;i<256;i++) {                                   
          fprintf(stderr,"%i:  %i: next=%i\n",gasneti_mynode,i,buf[i].addr.eopidx);
          fflush(stderr);
        }
        sleep(5);
      #endif

      gasneti_memcheck(thread->eop_bufs[bufidx]);
      memset(seen, 0, 256*sizeof(int));
      for (i=0;i<(bufidx==255?255:256);i++) {                                   
        gasnete_eop_t *eop;                                   
        gasneti_assert(!gasnete_eopaddr_isnil(addr));                 
        eop = GASNETE_EOPADDR_TO_PTR(thread,addr);            
        gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);               
        gasneti_assert(OPSTATE(eop) == OPSTATE_FREE);                 
        gasneti_assert(eop->threadidx == threadidx);                  
        gasneti_assert(addr.bufferidx == bufidx);
        gasneti_assert(!seen[addr.eopidx]);/* see if we hit a cycle */
        seen[addr.eopidx] = 1;
        addr = eop->addr;                                     
      }                                                       
      gasneti_assert(gasnete_eopaddr_isnil(addr)); 
    }
    #endif

    return gasnete_eop_new(thread); /*  should succeed this time */
  }
}

gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t * const thread) {
  gasnete_iop_t *iop;
  if_pt (thread->iop_free) {
    iop = thread->iop_free;
    thread->iop_free = iop->next;
    gasneti_memcheck(iop);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(iop->threadidx == thread->threadidx);
  } else {
    iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
    gasneti_leak(iop);
    #if GASNET_DEBUG
      memset(iop, 0, sizeof(gasnete_iop_t)); /* set pad to known value */
    #endif
    SET_OPTYPE((gasnete_op_t *)iop, OPTYPE_IMPLICIT);
    iop->threadidx = thread->threadidx;
  }
  iop->next = NULL;
  iop->initiated_get_cnt = 0;
  iop->initiated_put_cnt = 0;
  gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
  gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
  gasnete_iop_check(iop);
  return iop;
}

/*  query an op for completeness - for iop this means both puts and gets */
int gasnete_op_isdone(gasnete_op_t *op) {
  gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
  if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
    gasnete_eop_check((gasnete_eop_t *)op);
    return OPSTATE(op) == OPSTATE_COMPLETE;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;
    gasnete_iop_check(iop);
    return (gasneti_weakatomic_read(&(iop->completed_get_cnt), 0) == iop->initiated_get_cnt) &&
           (gasneti_weakatomic_read(&(iop->completed_put_cnt), 0) == iop->initiated_put_cnt);
  }
}

/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT);
    gasnete_eop_check(eop);
    SET_OPSTATE(eop, OPSTATE_COMPLETE);
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    if (isget) gasneti_weakatomic_increment(&(iop->completed_get_cnt), 0);
    else gasneti_weakatomic_increment(&(iop->completed_put_cnt), 0);
  }
}

/*  free an op */
void gasnete_op_free(gasnete_op_t *op) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[op->threadidx];
  gasneti_assert(thread == gasnete_mythread());
  GASNETI_TRACE_PRINTF(C,("free eop = %lu",(unsigned long)op));
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasnete_eopaddr_t addr = eop->addr;
    gasneti_assert(OPSTATE(eop) == OPSTATE_COMPLETE);
    gasnete_eop_check(eop);
    SET_OPSTATE(eop, OPSTATE_FREE);
    eop->addr = thread->eop_free;
    thread->eop_free = addr;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    gasneti_assert(iop->next == NULL);
    iop->next = thread->iop_free;
    thread->iop_free = iop;
  }
}
/* ------------------------------------------------------------------------------------ */
/* GASNET-Internal OP Interface */
gasneti_eop_t *gasneti_eop_create(GASNETE_THREAD_FARG_ALONE) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
  return (gasneti_eop_t *)op;
}
gasneti_iop_t *gasneti_iop_register(unsigned int noperations, int isget GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  gasnete_iop_check(op);
  if (isget) op->initiated_get_cnt += noperations;
  else       op->initiated_put_cnt += noperations;
  gasnete_iop_check(op);
  return (gasneti_iop_t *)op;
}
void gasneti_eop_markdone(gasneti_eop_t *eop) {
  gasnete_op_markdone((gasnete_op_t *)eop, 0);
}
void gasneti_iop_markdone(gasneti_iop_t *iop, unsigned int noperations, int isget) {
  gasnete_iop_t *op = (gasnete_iop_t *)iop;
  gasneti_weakatomic_t * const pctr = (isget ? &(op->completed_get_cnt) : &(op->completed_put_cnt));
  gasnete_iop_check(op);
  if (noperations == 1) gasneti_weakatomic_increment(pctr, 0);
  else {
    #if defined(GASNETI_HAVE_WEAKATOMIC_ADD_SUB)
      gasneti_weakatomic_add(pctr, noperations, 0);
    #else /* yuk */
      while (noperations) {
        gasneti_weakatomic_increment(pctr, 0);
        noperations--;
      }
    #endif
  }
  gasnete_iop_check(op);
}
/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_memset_reqh_inner)
void gasnete_memset_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t val, void *nbytes_arg, void *dest, void *op) {
  size_t nbytes = (uintptr_t)nbytes_arg;
  memset(dest, (int)(uint32_t)val, nbytes);
  gasneti_sync_writes();
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
SHORT_HANDLER(gasnete_memset_reqh,4,7,
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_markdone_reph_inner)
void gasnete_markdone_reph_inner(gasnet_token_t token, 
  void *op) {
  gasnete_op_markdone((gasnete_op_t *)op, 0); /*  assumes this is a put or explicit */
}
SHORT_HANDLER(gasnete_markdone_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */

/* Gemini requires 4-byte alignment of local address, while Aries doesn't.
   However, intial testing shows that Aries performance is poor w/o alignment */
#if defined(GASNET_CONDUIT_GEMINI) || 1
  #define GASNETE_GET_IS_UNALIGNED(_nbytes, _src, _dest) \
      (3 & ((uintptr_t)(_nbytes) | (uintptr_t)(_src) | (uintptr_t)(_dest)))
#else
  #define GASNETE_GET_IS_UNALIGNED(_nbytes, _src, _dest) \
      (3 & ((uintptr_t)(_nbytes) | (uintptr_t)(_src)))
#endif

static void /* XXX: Inlining left to compiler's discression */
gasnete_get_bulk_unaligned(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
  const size_t max_chunk = gasnetc_max_get_unaligned;
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const iop = mythread->current_iop;
  gasnetc_post_descriptor_t *gpd;

#ifdef GASNET_CONDUIT_GEMINI
  /* Upto 1300 bytes or so, larger alignment helps */
  const size_t mask = (nbytes <= 1300) ? 63 : 3;
#else
  /* Larger alignment always helps */
  const size_t mask = 63;
#endif
  const size_t src_offset = mask & (uintptr_t) src;

  /* first chunk achieves alignment to as much as 64-bytes if necessary */
  gasneti_assert(src_offset < max_chunk);
  if (src_offset != 0) {
    const size_t chunksz = MIN(nbytes, (max_chunk - src_offset));
    gpd = gasnetc_alloc_post_descriptor();
    gpd->flags = GC_POST_COMPLETION_OP;
    gpd->gpd_completion = (uintptr_t) iop;
    iop->initiated_get_cnt++;
    gasnetc_rdma_get_unaligned(node, dest, src, chunksz, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  }
  if (!nbytes) return;
  gasneti_assert(0 == (3 & (uintptr_t)src));
  
  if (! GASNETE_GET_IS_UNALIGNED(0,0,dest) && (nbytes > max_chunk)) {
    /* dest address is sufficiently aligned - may use zero-copy (if applicable) 
       however, must exclude any "tail" of unaligned length */
    const size_t tailsz = (nbytes & 3) ? (nbytes % GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) : 0;
    const size_t chunksz = nbytes - tailsz;
    if (chunksz) {
      gasneti_assert(0 == (3 & chunksz));
      /* TODO: gasnete_get_nbi_bulk includes duplicate PSHM and alignment checks */
      gasneti_assert(! GASNETE_GET_IS_UNALIGNED(chunksz,src,dest));
      gasnete_get_nbi_bulk(dest, node, src, chunksz GASNETE_THREAD_PASS);
      dest = (char *) dest + chunksz;
      src  = (char *) src  + chunksz;
      nbytes = tailsz;
    }
  }

  /* dest address and/or nbytes is unaligned - must use bounce buffers for remainder */
  while (nbytes) {
    const size_t chunksz = MIN(nbytes, max_chunk);
    gpd = gasnetc_alloc_post_descriptor();
    gpd->flags = GC_POST_COMPLETION_OP;
    gpd->gpd_completion = (uintptr_t) iop;
    iop->initiated_get_cnt++;
    gasnetc_rdma_get_unaligned(node, dest, src, chunksz, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  }
}

static void /* XXX: Inlining left to compiler's discression */
gasnete_get_bulk_chunked(void *dest, gasnet_node_t node, void *src, size_t nbytes,
                         gasnete_iop_t * const iop, const size_t chunksz)
{
  gasnetc_post_descriptor_t *gpd;

  gasneti_assert(nbytes > chunksz); /* always 2 or more chunks */

  /* If need more than 2 chunks, then size first one to achieve alignment of subsequent chunks */
  if (nbytes > 2*chunksz) {
    const size_t align_to = GASNETI_PAGESIZE; /* Any power-of-two <= chunksz */
    size_t chunk_len = chunksz - ((uintptr_t)src & (align_to-1));
    gasneti_assert(chunk_len != 0);
    gpd = gasnetc_alloc_post_descriptor();
    gpd->flags = GC_POST_COMPLETION_OP;
    gpd->gpd_completion = (uintptr_t) iop;
    iop->initiated_get_cnt++;
    gasnetc_rdma_get(node, dest, src, chunk_len, gpd);
    dest = (char *) dest + chunk_len;
    src  = (char *) src  + chunk_len;
    nbytes -= chunk_len;
  }

  /* 1 or more full chunks */
  gasneti_assert (nbytes > chunksz);
  do {
    gpd = gasnetc_alloc_post_descriptor();
    gpd->flags = GC_POST_COMPLETION_OP;
    gpd->gpd_completion = (uintptr_t) iop;
    iop->initiated_get_cnt++;
    gasnetc_rdma_get(node, dest, src, chunksz, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  } while (nbytes > chunksz);

  /* final chunk (could be either full or partial) */
  gasneti_assert(nbytes != 0);
  gpd = gasnetc_alloc_post_descriptor();
  gpd->flags = GC_POST_COMPLETION_OP;
  gpd->gpd_completion = (uintptr_t) iop;
  iop->initiated_get_cnt++;
  gasnetc_rdma_get(node, dest, src, nbytes, gpd);
}

extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  size_t chunksz;

  GASNETI_CHECKPSHM_GET(UNALIGNED,H);

  if_pf (GASNETE_GET_IS_UNALIGNED(nbytes, src, dest)) {
    /* unaligned xfer - handled separately, and always via an iop */
    GASNETI_UNUSED_UNLESS_THREADS
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
    gasnete_get_bulk_unaligned(dest, node, src, nbytes GASNETE_THREAD_PASS);
    return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
  }

  chunksz = gasneti_in_segment(gasneti_mynode, dest, nbytes) ? GC_MAXRDMA_IN : GC_MAXRDMA_OUT;
  if_pt (nbytes <= chunksz) {
    /* xfer is small-enough: use a single op */
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
    gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);
    gpd->gpd_completion = (uintptr_t) eop;
    gpd->flags = GC_POST_COMPLETION_OP;
    gasnetc_rdma_get(node, dest, src, nbytes, gpd);
    return((gasnet_handle_t) eop);
  } else {
    /* "too-large" xfer is chunked into multiple ops, each no larger than chunksz */
    gasnete_iop_t *iop = gasnete_iop_new(GASNETE_MYTHREAD);
    gasnete_get_bulk_chunked(dest, node, src, nbytes, iop, chunksz);
    return (gasnet_handle_t) iop;
  }
}


/* TODO: could still be improved if we can separate sub-ops based on the lc status */
extern gasnet_handle_t gasnete_put_nb (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnetc_post_descriptor_t *gpd;
  gasnet_handle_t head_op = GASNET_INVALID_HANDLE;
  gasnete_eop_t *tail_op;
  const size_t max_tail = gasnetc_max_put_lc;
  GASNETI_UNUSED_UNLESS_DEBUG int lc;

  GASNETI_CHECKPSHM_PUT(ALIGNED,H);

  /* Non-blocking bulk put of "head" portion */
  if (nbytes > max_tail) {
    /* TODO: gasnete_put_nb_bulk includes duplicate PSHM check */
    const size_t head_len = nbytes - max_tail;
    head_op = gasnete_put_nb_bulk(node, dest, src, head_len GASNETE_THREAD_PASS);
    dest = (char *) dest + head_len;
    src  = (char *) src  + head_len;
    nbytes = max_tail;
  }

  /* Non-blocking non-bulk put of "tail" portion */
  gpd = gasnetc_alloc_post_descriptor();
  tail_op = gasnete_eop_new(GASNETE_MYTHREAD);
  gpd->gpd_completion = (uintptr_t) tail_op;
  gpd->flags = GC_POST_COMPLETION_OP;
  lc = gasnetc_rdma_put(node, dest, src, nbytes, gpd);
  gasneti_assert(lc);

  /* Block for completion of head, if any */
  gasnete_wait_syncnb(head_op);

  /* return the tail_op */
  return (gasnet_handle_t)tail_op;
}

static void /* XXX: Inlining left to compiler's discression */
gasnete_put_bulk_chunked(gasnet_node_t node, void *dest, void *src, size_t nbytes,
                         gasnete_iop_t * const iop, const size_t chunksz)
{
  gasnetc_post_descriptor_t *gpd;

  gasneti_assert(nbytes > chunksz); /* always 2 or more chunks */

  /* If need more than 2 chunks, then size first one to achieve alignment of subsequent chunks */
  if (nbytes > 2*chunksz) {
    const size_t align_to = GASNETI_PAGESIZE; /* Any power-of-two <= chunksz */
    size_t chunk_len = chunksz - ((uintptr_t)src & (align_to-1));
    gasneti_assert(chunk_len != 0);
    gpd = gasnetc_alloc_post_descriptor();
    gpd->flags = GC_POST_COMPLETION_OP;
    gpd->gpd_completion = (uintptr_t) iop;
    iop->initiated_put_cnt++;
    gasnetc_rdma_put_bulk(node, dest, src, chunk_len, gpd);
    dest = (char *) dest + chunk_len;
    src  = (char *) src  + chunk_len;
    nbytes -= chunk_len;
  }

  /* 1 or more full chunks */
  gasneti_assert (nbytes > chunksz);
  do {
    gpd = gasnetc_alloc_post_descriptor();
    gpd->flags = GC_POST_COMPLETION_OP;
    gpd->gpd_completion = (uintptr_t) iop;
    iop->initiated_put_cnt++;
    gasnetc_rdma_put_bulk(node, dest, src, chunksz, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  } while (nbytes > chunksz);

  /* final chunk (could be either full or partial) */
  gasneti_assert(nbytes != 0);
  gpd = gasnetc_alloc_post_descriptor();
  gpd->flags = GC_POST_COMPLETION_OP;
  gpd->gpd_completion = (uintptr_t) iop;
  iop->initiated_put_cnt++;
  gasnetc_rdma_put_bulk(node, dest, src, nbytes, gpd);
}

extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  size_t chunksz;

  GASNETI_CHECKPSHM_PUT(UNALIGNED,H);

  chunksz = gasneti_in_segment(gasneti_mynode, src, nbytes) ? GC_MAXRDMA_IN : GC_MAXRDMA_OUT;
  if_pt (nbytes <= chunksz) {
    /* xfer is small-enough: use a single op */
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
    gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);
    gpd->gpd_completion = (uintptr_t) eop;
    gpd->flags = GC_POST_COMPLETION_OP;
    gasnetc_rdma_put_bulk(node, dest, src, nbytes, gpd);
    return((gasnet_handle_t) eop);
  } else {
    /* "too-large" xfer is chunked into multiple ops, each no larger than chunksz */
    gasnete_iop_t *iop = gasnete_iop_new(GASNETE_MYTHREAD);
    gasnete_put_bulk_chunked(node, dest, src, nbytes, iop, chunksz);
    return (gasnet_handle_t) iop;
  }
}

extern gasnet_handle_t gasnete_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
 GASNETI_CHECKPSHM_MEMSET(H);
 {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

  GASNETI_SAFE(
    SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, PACK(nbytes),
                 PACK(dest), PACK(op))));

  return (gasnet_handle_t)op;
 }
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
#if 0
  /* polling now takes place in callers which needed and NOT in those which don't */
  GASNETI_SAFE(gasneti_AMPoll());
#endif

  if (gasnete_op_isdone(handle)) {
    gasneti_sync_reads();
    gasnete_op_free(handle);
    return GASNET_OK;
  }
  else return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;
#if 0
  /* polling for syncnb now happens in header file to avoid duplication */
  GASNETI_SAFE(gasneti_AMPoll());
#endif

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnete_op_t *op = phandle[i];
      if (op != GASNET_INVALID_HANDLE) {
        empty = 0;
        if (gasnete_op_isdone(op)) {
	  gasneti_sync_reads();
          gasnete_op_free(op);
          phandle[i] = GASNET_INVALID_HANDLE;
          success = 1;
        }  
      }
    }
  }

  if (success || empty) return GASNET_OK;
  else return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 1;
#if 0
  /* polling for syncnb now happens in header file to avoid duplication */
  GASNETI_SAFE(gasneti_AMPoll());
#endif

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnete_op_t *op = phandle[i];
      if (op != GASNET_INVALID_HANDLE) {
        if (gasnete_op_isdone(op)) {
	  gasneti_sync_reads();
          gasnete_op_free(op);
          phandle[i] = GASNET_INVALID_HANDLE;
        } else success = 0;
      }
    }
  }

  if (success) return GASNET_OK;
  else return GASNET_ERR_NOT_READY;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const iop = mythread->current_iop;
  size_t chunksz;

  GASNETI_CHECKPSHM_GET(UNALIGNED,V);

  if_pf (GASNETE_GET_IS_UNALIGNED(nbytes, src, dest)) {
    /* unaligned xfer - handled separately, and always via an iop */
    gasnete_get_bulk_unaligned(dest, node, src, nbytes GASNETE_THREAD_PASS);
    return;
  }

  chunksz = gasneti_in_segment(gasneti_mynode, dest, nbytes) ? GC_MAXRDMA_IN : GC_MAXRDMA_OUT;
  if_pt (nbytes <= chunksz) {
    /* xfer is small-enough: use a single op */
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
    gpd->gpd_completion = (uintptr_t) iop;
    gpd->flags = GC_POST_COMPLETION_OP;
    iop->initiated_get_cnt++;
    gasnetc_rdma_get(node, dest, src, nbytes, gpd);
  } else {
    /* "too-large" xfer is chunked into multiple ops, each no larger than chunksz */
    gasnete_get_bulk_chunked(dest, node, src, nbytes, iop, chunksz);
  }
}

extern void gasnete_put_nbi      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const tail_op = mythread->current_iop;
  gasnetc_post_descriptor_t *gpd;
  gasnet_handle_t head_op = GASNET_INVALID_HANDLE;
  const size_t max_tail = gasnetc_max_put_lc;
  GASNETI_UNUSED_UNLESS_DEBUG int lc;

  GASNETI_CHECKPSHM_PUT(ALIGNED,V);

  /* Non-blocking bulk put of "head" portion */
  if (nbytes > max_tail) {
    /* TODO: gasnete_put_nb_bulk includes duplicate PSHM check */
    const size_t head_len = nbytes - max_tail;
    head_op = gasnete_put_nb_bulk(node, dest, src, head_len GASNETE_THREAD_PASS);
    dest = (char *) dest + head_len;
    src  = (char *) src  + head_len;
    nbytes = max_tail;
  }

  /* Non-blocking non-bulk put of "tail" portion */
  gpd = gasnetc_alloc_post_descriptor();
  gpd->gpd_completion = (uintptr_t) tail_op;
  gpd->flags = GC_POST_COMPLETION_OP;
  tail_op->initiated_put_cnt++;
  lc = gasnetc_rdma_put(node, dest, src, nbytes, gpd);
  gasneti_assert(lc);

  /* Block for completion of head, if any */
  gasnete_wait_syncnb(head_op);
}

extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const iop = mythread->current_iop;
  size_t chunksz;

  GASNETI_CHECKPSHM_PUT(UNALIGNED,V);

  chunksz = gasneti_in_segment(gasneti_mynode, src, nbytes) ? GC_MAXRDMA_IN : GC_MAXRDMA_OUT;
  if_pt (nbytes <= chunksz) {
    /* xfer is small-enough: use a single op */
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
    gpd->gpd_completion = (uintptr_t) iop;
    gpd->flags = GC_POST_COMPLETION_OP;
    iop->initiated_put_cnt++;
    gasnetc_rdma_put_bulk(node, dest, src, nbytes, gpd);
  } else {
    /* "too-large" xfer is chunked into multiple ops, each no larger than chunksz */
    gasnete_put_bulk_chunked(node, dest, src, nbytes, iop, chunksz);
  }
}

extern void gasnete_memset_nbi   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *op = mythread->current_iop;
  GASNETI_CHECKPSHM_MEMSET(V);

  op->initiated_put_cnt++;

  GASNETI_SAFE(
    SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, PACK(nbytes),
                 PACK(dest), PACK(op))));
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
    #endif

    if (gasneti_weakatomic_read(&(iop->completed_get_cnt), 0) == iop->initiated_get_cnt) {
      if_pf (iop->initiated_get_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
        iop->initiated_get_cnt = 0;
      }
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
  }
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
    #endif


    if (gasneti_weakatomic_read(&(iop->completed_put_cnt), 0) == iop->initiated_put_cnt) {
      if_pf (iop->initiated_put_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
        iop->initiated_put_cnt = 0;
      }
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
/*  This implementation allows recursive access regions, although the spec does not require that */
/*  operations are associated with the most immediately enclosing access region */
extern void            gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread); /*  push an iop  */
  GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
  #if GASNET_DEBUG
    if (!allowrecursion && mythread->current_iop->next != NULL)
      gasneti_fatalerror("VIOLATION: tried to initiate a recursive NBI access region");
  #endif
  iop->next = mythread->current_iop;
  mythread->current_iop = iop;
}

extern gasnet_handle_t gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop; /*  pop an iop */
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,iop->initiated_get_cnt + iop->initiated_put_cnt);
  #if GASNET_DEBUG
    if (iop->next == NULL)
      gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion() outside access region");
  #endif
  mythread->current_iop = iop->next;
  iop->next = NULL;
  return (gasnet_handle_t)iop;
}
/* ------------------------------------------------------------------------------------ */
/*
  Blocking and non-blocking register-to-memory Puts
  =================================================
*/
/* ------------------------------------------------------------------------------------ */

#define gasnete_val_assign(_dst, _val) \
    (*(gasnet_register_value_t *)(_dst) = (gasnet_register_value_t)(_val))

extern void gasnete_put_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUTVAL(V);
  {
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
    volatile int done = 0;
    void * const src = GASNETE_STARTOFBITS(gpd->u.immediate, nbytes);
    gpd->gpd_completion = (uintptr_t) &done;
    gpd->flags = GC_POST_COMPLETION_FLAG;
    gasnete_val_assign(gpd->u.immediate, value);
    gasnetc_rdma_put_buff(node, dest, src, nbytes, gpd);
    gasneti_polluntil(done);
  }
}

extern gasnet_handle_t gasnete_put_nb_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUTVAL(H);
  {
    gasnetc_post_descriptor_t * const gpd = gasnetc_alloc_post_descriptor();
    gasnete_eop_t * const eop = gasnete_eop_new(GASNETE_MYTHREAD);
    void * const src = GASNETE_STARTOFBITS(gpd->u.immediate, nbytes);
    gpd->gpd_completion = (uintptr_t) eop;
    gpd->flags = GC_POST_COMPLETION_OP;
    gasnete_val_assign(gpd->u.immediate, value);
    gasnetc_rdma_put_buff(node, dest, src, nbytes, gpd);
    return((gasnet_handle_t) eop);
  }
}

extern void gasnete_put_nbi_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUTVAL(V);
  {
    gasnetc_post_descriptor_t * const gpd = gasnetc_alloc_post_descriptor();
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const iop = mythread->current_iop;
    void * const src = GASNETE_STARTOFBITS(gpd->u.immediate, nbytes);
    gpd->gpd_completion = (uintptr_t) iop;
    iop->initiated_put_cnt++;
    gpd->flags = GC_POST_COMPLETION_OP;
    gasnete_val_assign(gpd->u.immediate, value);
    gasnetc_rdma_put_buff(node, dest, src, nbytes, gpd);
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Blocking and non-blocking memory-to-register Gets
  =================================================
*/
/* ------------------------------------------------------------------------------------ */

GASNETI_INLINE(gasnete_get_val_help)
gasnet_register_value_t gasnete_get_val_help(void *src, size_t nbytes) {
#if PLATFORM_ARCH_LITTLE_ENDIAN
  /* Note that this is OK only on little-endian and when unaligned loads are "OKAY" */
  return *(gasnet_register_value_t *)src & (~0UL >> (8*(SIZEOF_VOID_P-nbytes)));
#else
  /* XXX: could do load+shift but don't care given the lack of big-endian GNI systems */
  GASNETE_VALUE_RETURN(src, nbytes);
#endif
}
 
extern gasnet_register_value_t gasnete_get_val(gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_GETVAL();
  {
    gasnet_register_value_t result;
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
    volatile int done = 0;
    uint8_t *buffer = gpd->u.immediate;
    gpd->gpd_completion = (uintptr_t) &done;
    gpd->flags = GC_POST_COMPLETION_FLAG | GC_POST_KEEP_GPD;
    buffer += gasnetc_rdma_get_buff(node, buffer, src, nbytes, gpd);
    gasneti_polluntil(done);
    result = gasnete_get_val_help(buffer, nbytes);
    gasnetc_free_post_descriptor(gpd);
    return result;
  }
}

/* Following implementation of valget_handle_t and associated operations
   is cloned from gasnet_extended_common.c, and then:
   The 'op' field has been flattened to a 'done' flag.
   The order fileds had been reordered to minimize padding.
   The underlying get has also been customized.
*/

typedef struct _gasnete_valget_op_t {
  struct _gasnete_valget_op_t* next; /* for free-list only */
  gasnet_register_value_t val;
  volatile int done;
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
} gasnete_valget_op_t;

extern gasnet_valget_handle_t gasnete_get_nb_val(gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnet_valget_handle_t retval;
  gasneti_assert(nbytes > 0 && nbytes <= sizeof(gasnet_register_value_t));
  gasneti_boundscheck(node, src, nbytes);
  if (mythread->valget_free) {
    retval = mythread->valget_free;
    mythread->valget_free = retval->next;
    gasneti_memcheck(retval);
  } else {
    retval = (gasnete_valget_op_t*)gasneti_malloc(sizeof(gasnete_valget_op_t));
    gasneti_leak(retval);
    retval->threadidx = mythread->threadidx;
  }

  retval->val = 0;
#if GASNET_PSHM
  if (gasneti_pshm_in_supernode(node)) {
    /* Assume that addr2local on local node is cheaper than an extra branch */
    GASNETE_FAST_ALIGNED_MEMCPY(GASNETE_STARTOFBITS(&(retval->val),nbytes),
                                gasneti_pshm_addr2local(node, src), nbytes);
    retval->done = 1;
  }
#else
  if (gasnete_islocal(node)) {
    GASNETE_FAST_ALIGNED_MEMCPY(GASNETE_STARTOFBITS(&(retval->val),nbytes), src, nbytes);
    retval->done = 1;
  }
#endif
  else {
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
    gpd->gpd_completion = (uintptr_t) &retval->done;
    retval->done = 0;
    gpd->flags = GC_POST_COMPLETION_FLAG;
    gasnetc_rdma_get_unaligned(node, GASNETE_STARTOFBITS(&(retval->val),nbytes), src, nbytes, gpd);
  }
  return retval;
}

extern gasnet_register_value_t gasnete_wait_syncnb_valget(gasnet_valget_handle_t handle) {
  gasnete_assert_valid_threadid(handle->threadidx);
  { gasnete_threaddata_t * const thread = gasnete_threadtable[handle->threadidx];
    gasnet_register_value_t val;
    gasneti_assert(thread == gasnete_mythread());
    handle->next = thread->valget_free; /* free before the wait to save time after the wait, */
    thread->valget_free = handle;       /*  safe because this thread is under our control */
    gasneti_polluntil(handle->done);
    val = handle->val;
    return val;
  }
}

static void gasnete_valget_freeall(gasnete_threaddata_t *thread) {
  gasnete_valget_op_t *vg = thread->valget_free;
  while (vg) {
    gasnete_valget_op_t *next = vg->next;
    gasneti_free(vg);  
    vg = next;
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
  "gd" = GNI Dissemination
*/

#define GASNETE_BARRIER_DEFAULT "GNIDISSEM"

/* Forward decls for init function(s): */
static void gasnete_gdbarrier_init(gasnete_coll_team_t team);

#define GASNETE_BARRIER_READENV() do {                                   \
  if (GASNETE_ISBARRIER("GNIDISSEM"))                                    \
    gasnete_coll_default_barrier_type = GASNETE_COLL_BARRIER_GNIDISSEM;  \
} while (0)

#define GASNETE_BARRIER_INIT(TEAM, BARRIER_TYPE) do {                                  \
    if ((BARRIER_TYPE) == GASNETE_COLL_BARRIER_GNIDISSEM && (TEAM)==GASNET_TEAM_ALL) { \
      gasnete_gdbarrier_init(TEAM);                              \
    }                                                            \
  } while (0)

/* Can use the auxseg allocation from the generic implementation: */
static int gasnete_conduit_rdmabarrier(const char *barrier, gasneti_auxseg_request_t *result);
#define GASNETE_CONDUIT_RDMABARRIER gasnete_conduit_rdmabarrier

/* use reference implementation of barrier */
#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C

static int gasnete_conduit_rdmabarrier(const char *barrier, gasneti_auxseg_request_t *result) {
  if (0 == strcmp(barrier, "GNIDISSEM")) {
    size_t request = gasnetc_log2_remote * GASNETE_RDMABARRIER_INBOX_SZ * 2;
    gasneti_assert_always(GASNETE_RDMABARRIER_INBOX_SZ >= sizeof(uint64_t));
    gasneti_assert_always(request <= result->optimalsz);
    result->minsz = request;
    result->optimalsz = request;
    return 1;
  }

  return 0;
}

/* ------------------------------------------------------------------------------------ */
/* GNI-specific RDMA-based Dissemination implementation of barrier
 * This is an adaptation of the "rmd" barrier in exteneded-ref.
 * Key differences:
 *  + no complications due to thread-specific handles
 *  + simple 64-bit put since (aligned) 64-bit puts are atomic
 */

#if !GASNETI_THREADS
  #define GASNETE_GDBARRIER_LOCK(_var)		/* empty */
  #define gasnete_gdbarrier_lock_init(_var)	((void)0)
  #define gasnete_gdbarrier_trylock(_var)	(0/*success*/)
  #define gasnete_gdbarrier_unlock(_var)	((void)0)
#elif GASNETI_HAVE_SPINLOCK
  #define GASNETE_GDBARRIER_LOCK(_var)		gasneti_atomic_t _var;
  #define gasnete_gdbarrier_lock_init(_var)	gasneti_spinlock_init(_var)
  #define gasnete_gdbarrier_trylock(_var)	gasneti_spinlock_trylock(_var)
  #define gasnete_gdbarrier_unlock(_var)	gasneti_spinlock_unlock(_var)
#else
  #define GASNETE_GDBARRIER_LOCK(_var)		gasneti_mutex_t _var;
  #define gasnete_gdbarrier_lock_init(_var)	gasneti_mutex_init(_var)
  #define gasnete_gdbarrier_trylock(_var)	gasneti_mutex_trylock(_var)
  #define gasnete_gdbarrier_unlock(_var)	gasneti_mutex_unlock(_var)
#endif

typedef struct {
  GASNETE_GDBARRIER_LOCK(barrier_lock) /* no semicolon */
  struct {
    gasnet_node_t node;
    uint64_t      *addr;
  } *barrier_peers;           /*  precomputed list of peers to communicate with */
#if GASNETI_PSHM_BARRIER_HIER
  gasnete_pshmbarrier_data_t *barrier_pshm; /* non-NULL if using hierarchical code */
  int barrier_passive;        /*  2 if some other node makes progress for me, 0 otherwise */
#endif
  int barrier_size;           /*  ceil(lg(nodes)) */
  int barrier_goal;           /*  (ceil(lg(nodes)) << 1) */
  int volatile barrier_slot;  /*  (step << 1) | phase */
  int volatile barrier_value; /*  barrier value (evolves from local value) */
  int volatile barrier_flags; /*  barrier flags (evolves from local value) */
  volatile uint64_t *barrier_inbox; /*  in-segment memory to recv notifications */
} gasnete_coll_gdbarrier_t;

/* Unlike the extended-ref version we CAN assume that a 64-bit write
 * will be atomic, and so can use a "present" bit in flags.
 */
#define GASNETE_GDBARRIER_PRESENT 0x80000000
#define GASNETE_GDBARRIER_VALUE(_u64) GASNETI_HIWORD(_u64)
#define GASNETE_GDBARRIER_FLAGS(_u64) GASNETI_LOWORD(_u64)
#define GASNETE_GDBARRIER_BUILD(_value,_flags) \
                GASNETI_MAKEWORD((_value),GASNETE_GDBARRIER_PRESENT|(_flags))
  
/* Pad struct to a specfic size and interleave */
#define GASNETE_GDBARRIER_INBOX_WORDS (GASNETE_RDMABARRIER_INBOX_SZ/sizeof(uint64_t))
#define GASNETE_GDBARRIER_INBOX(_bd,_slot)     \
            (((_bd)->barrier_inbox) \
                       + (unsigned)(_slot) * GASNETE_GDBARRIER_INBOX_WORDS)
#define GASNETE_GDBARRIER_INBOX_REMOTE(_bd,_step,_slot)  \
            (((_bd)->barrier_peers[(unsigned)(_step)].addr \
                       + (unsigned)(_slot) * GASNETE_GDBARRIER_INBOX_WORDS))
#define GASNETE_GDBARRIER_INBOX_NEXT(_addr)    \
            ((_addr) + 2U * GASNETE_GDBARRIER_INBOX_WORDS)

GASNETI_INLINE(gasnete_gdbarrier_send)
void gasnete_gdbarrier_send(gasnete_coll_gdbarrier_t *barrier_data,
                             int numsteps, unsigned int slot,
                             gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  unsigned int step = slot >> 1;
  const uint64_t payload = GASNETE_GDBARRIER_BUILD(value, flags);
  int i;

  gasneti_assert(sizeof(payload) <= sizeof(gasnet_register_value_t));

  for (i = 0; i < numsteps; ++i, slot += 2, step += 1) {
    const gasnet_node_t node = barrier_data->barrier_peers[step].node;
    uint64_t * const dst = GASNETE_GDBARRIER_INBOX_REMOTE(barrier_data, step, slot);
    gasnetc_post_descriptor_t * const gpd = gasnetc_alloc_post_descriptor();
    uint64_t * const src = (uint64_t *)GASNETE_STARTOFBITS(gpd->u.immediate, sizeof(uint64_t));

    gpd->flags = 0; /* fire and forget */
    *src = payload;
    gasnetc_rdma_put_buff(node, dst, src, sizeof(*src), gpd);
  }
}

void gasnete_gdbarrier_kick(gasnete_coll_team_t team) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
  volatile uint64_t *inbox;
  uint64_t result;
  int numsteps = 0;
  int slot, cursor;
  int flags, value;

  /* early unlocked read: */
  slot = barrier_data->barrier_slot;
  inbox = GASNETE_GDBARRIER_INBOX(barrier_data, slot);

  if (slot >= barrier_data->barrier_goal || !(*inbox))
    return; /* nothing to do */

  gasneti_assert(team->total_ranks > 1); /* singleton should have matched (slot >= goal), above */

#if GASNETI_THREADS
  if (gasnete_gdbarrier_trylock(&barrier_data->barrier_lock))
    return; /* another thread is currently in kick */

  /* reread w/ lock held: */
  slot = barrier_data->barrier_slot;
#endif

  if_pf (slot < 2) {/* need to pick up value/flags from notify */
    gasneti_sync_reads(); /* value/flags were written by the non-locked notify */
  }

  value = barrier_data->barrier_value;
  flags = barrier_data->barrier_flags;

  /* process all consecutive steps which have arrived since we last ran */
  inbox = GASNETE_GDBARRIER_INBOX(barrier_data, slot);
  for (cursor = slot; cursor < barrier_data->barrier_goal && (0 != (result = *inbox)); cursor+=2) {
    const int step_value = GASNETE_GDBARRIER_VALUE(result);
    const int step_flags = GASNETE_GDBARRIER_FLAGS(result);
    *inbox = 0;

    if ((flags | step_flags) & GASNET_BARRIERFLAG_MISMATCH) {
      flags = GASNET_BARRIERFLAG_MISMATCH; 
    } else if (flags & GASNET_BARRIERFLAG_ANONYMOUS) {
      flags = step_flags; 
      value = step_value; 
    } else if (!(step_flags & GASNET_BARRIERFLAG_ANONYMOUS) && (step_value != value)) {
      flags = GASNET_BARRIERFLAG_MISMATCH; 
    }

    ++numsteps;
    inbox = GASNETE_GDBARRIER_INBOX_NEXT(inbox);
  }

  if (numsteps) { /* completed one or more steps */
    barrier_data->barrier_flags = flags; 
    barrier_data->barrier_value = value; 

    if (cursor >= barrier_data->barrier_goal) { /* We got the last recv - barrier locally complete */
      gasnete_barrier_pf_disable(team);
      gasneti_sync_writes(); /* flush state before the write to barrier_slot below */
      numsteps -= 1; /* no send at last step */
    } 
    /* notify all threads of the step increase - 
       this may allow other local threads to proceed on the barrier and even indicate
       barrier completion while we overlap outgoing notifications to other nodes
    */
    barrier_data->barrier_slot = cursor;
  } 

  gasnete_gdbarrier_unlock(&barrier_data->barrier_lock);

  if (numsteps) { /* need to issue one or more Puts */
    gasnete_gdbarrier_send(barrier_data, numsteps, slot+2, value, flags);
  }
}

static void gasnete_gdbarrier_notify(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
  int do_send = 1;
  int slot;

  if_pf(team->barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
    (void)gasnete_pshmbarrier_notify_inner(pshm_bdata, id, flags);
    do_send = !barrier_data->barrier_passive;
    id = pshm_bdata->shared->value;
    flags = pshm_bdata->shared->flags;
  }
#endif

  barrier_data->barrier_value = id;
  barrier_data->barrier_flags = flags;

  slot = ((barrier_data->barrier_slot & 1) ^ 1); /* enter new phase */
  gasneti_sync_writes();
  barrier_data->barrier_slot = slot;

  if (do_send) {
    gasnete_gdbarrier_send(barrier_data, 1, slot, id, flags);
    gasnete_barrier_pf_enable(team);
  }

  /*  update state */
  team->barrier_splitstate = INSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

/* Notify specialized to one (super)node case (reduced branches in BOTH variants) */
static void gasnete_gdbarrier_notify_singleton(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;

  if_pf(team->barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
    (void)gasnete_pshmbarrier_notify_inner(pshm_bdata, id, flags);
    id = pshm_bdata->shared->value;
    flags = pshm_bdata->shared->flags;
  }
#endif

  barrier_data->barrier_value = id;
  barrier_data->barrier_flags = flags;

  /*  update state */
  team->barrier_splitstate = INSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

static int gasnete_gdbarrier_wait(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
#if GASNETI_PSHM_BARRIER_HIER
  const PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
#endif
  int retval = GASNET_OK;

  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");

#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    const int passive_shift = barrier_data->barrier_passive;
    retval = gasnete_pshmbarrier_wait_inner(pshm_bdata, id, flags, passive_shift);
    if (passive_shift) {
      /* Once the active peer signals done, we can return */
      team->barrier_splitstate = OUTSIDE_BARRIER;
      gasneti_sync_writes(); /* ensure all state changes committed before return */
      return retval;
    }
  }
#endif

  if (barrier_data->barrier_slot >= barrier_data->barrier_goal) {
    /* completed asynchronously before wait (via progressfns or try) */
    GASNETI_TRACE_EVENT_TIME(B,BARRIER_ASYNC_COMPLETION,GASNETI_TICKS_NOW_IFENABLED(B)-gasnete_barrier_notifytime);
  } else {
    /* kick once, and if still necessary, wait for a response */
    gasnete_gdbarrier_kick(team);
    /* cannot BLOCKUNTIL since progess may occur on non-AM events */
    while (barrier_data->barrier_slot < barrier_data->barrier_goal) {
      GASNETI_WAITHOOK();
      GASNETI_SAFE(gasneti_AMPoll());
      gasnete_gdbarrier_kick(team);
    }
  }
  gasneti_sync_reads(); /* ensure correct barrier_flags will be read */

  /* determine return value */
  if_pf (barrier_data->barrier_flags & GASNET_BARRIERFLAG_MISMATCH) {
    retval = GASNET_ERR_BARRIER_MISMATCH;
  } else
  if_pf(/* try/wait value must match consensus value, if both are present */
        !((flags|barrier_data->barrier_flags) & GASNET_BARRIERFLAG_ANONYMOUS) &&
	 ((gasnet_handlerarg_t)id != barrier_data->barrier_value)) {
    retval = GASNET_ERR_BARRIER_MISMATCH;
  }

  /*  update state */
  team->barrier_splitstate = OUTSIDE_BARRIER;
#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    /* Signal any passive peers w/ the final result */
    pshm_bdata->shared->value = barrier_data->barrier_value;
    pshm_bdata->shared->flags = barrier_data->barrier_flags;
    PSHM_BSTATE_SIGNAL(pshm_bdata, retval, pshm_bdata->private.two_to_phase << 2); /* includes a WMB */
    gasneti_assert(!barrier_data->barrier_passive);
  } else
#endif
  gasneti_sync_writes(); /* ensure all state changes committed before return */

  return retval;
}

static int gasnete_gdbarrier_try(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */

  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  GASNETI_SAFE(gasneti_AMPoll());

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    const int passive_shift = barrier_data->barrier_passive;
    if (!gasnete_pshmbarrier_try_inner(barrier_data->barrier_pshm, passive_shift))
      return GASNET_ERR_NOT_READY;
    if (passive_shift)
      return gasnete_gdbarrier_wait(team, id, flags);
  }
  if (!barrier_data->barrier_passive)
#endif
    gasnete_gdbarrier_kick(team);

  if (barrier_data->barrier_slot >= barrier_data->barrier_goal) return gasnete_gdbarrier_wait(team, id, flags);
  else return GASNET_ERR_NOT_READY;
}

void gasnete_gdbarrier_kick_team_all(void) {
  gasnete_gdbarrier_kick(GASNET_TEAM_ALL);
}

static void gasnete_gdbarrier_init(gasnete_coll_team_t team) {
  gasnete_coll_gdbarrier_t *barrier_data;
  int steps;
  int total_ranks = team->total_ranks;
  int myrank = team->myrank;
#if GASNETI_PSHM_BARRIER_HIER
  gasnet_node_t *supernode_reps = NULL;
  PSHM_BDATA_DECL(pshm_bdata, gasnete_pshmbarrier_init_hier(team, &total_ranks, &myrank, &supernode_reps));
#endif
  int64_t j;

  barrier_data = gasneti_malloc_aligned(GASNETI_CACHE_LINE_BYTES, sizeof(gasnete_coll_gdbarrier_t));
  gasneti_leak_aligned(barrier_data);
  memset(barrier_data, 0, sizeof(gasnete_coll_gdbarrier_t));
  team->barrier_data = barrier_data;

#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    barrier_data->barrier_passive = (pshm_bdata->private.rank != 0) ? 2 : 0; /* precompute shift */
    barrier_data->barrier_pshm = pshm_bdata;
  }
#endif

  gasneti_assert(team == GASNET_TEAM_ALL); /* TODO: deal w/ in-segment allocation */

  gasnete_gdbarrier_lock_init(&barrier_data->barrier_lock);
  team->barrier_splitstate = OUTSIDE_BARRIER;

  /* determine barrier size (number of steps) */
  for (steps=0, j=1; j < total_ranks; ++steps, j*=2) ;

  barrier_data->barrier_size = steps;
  barrier_data->barrier_goal = steps << 1;

  if (steps) {
#if GASNETI_PSHM_BARRIER_HIER
    gasnet_node_t *nodes = supernode_reps ? supernode_reps : gasneti_pshm_firsts;
#endif
    int step;

    gasneti_assert(gasnete_rdmabarrier_auxseg);
    barrier_data->barrier_inbox = gasnete_rdmabarrier_auxseg[gasneti_mynode].addr;

    barrier_data->barrier_peers = gasneti_malloc(steps * sizeof(* barrier_data->barrier_peers));
    gasneti_leak(barrier_data->barrier_peers);
  
    for (step = 0; step < steps; ++step) {
      gasnet_node_t distance, tmp, peer, node;

      distance = (1 << step);
      tmp = total_ranks - myrank;
      peer = (distance < tmp) ? (distance + myrank) : (distance - tmp); /* mod N w/o overflow */
      gasneti_assert(peer < total_ranks);

#if GASNETI_PSHM_BARRIER_HIER
      if (pshm_bdata) {
        node = nodes[peer];
      } else
#endif
      {
        node = GASNETE_COLL_REL2ACT(team, peer);
      }

      barrier_data->barrier_peers[step].node = node;
      barrier_data->barrier_peers[step].addr = gasnete_rdmabarrier_auxseg[node].addr;
    }
  } else {
    barrier_data->barrier_slot = barrier_data->barrier_goal;
  }

  gasneti_free(gasnete_rdmabarrier_auxseg);

#if GASNETI_PSHM_BARRIER_HIER
  gasneti_free(supernode_reps);

  if (pshm_bdata && (pshm_bdata->shared->size == 1)) {
    /* With singleton proc on local supernode we can short-cut the PHSM code.
     * This does not require alteration of the barrier_peers[] contructed above
     */
    gasnete_pshmbarrier_fini_inner(pshm_bdata);
    barrier_data->barrier_pshm = NULL;
  }
#endif

  team->barrier_notify = steps ? &gasnete_gdbarrier_notify : &gasnete_gdbarrier_notify_singleton;
  team->barrier_wait =   &gasnete_gdbarrier_wait;
  team->barrier_try =    &gasnete_gdbarrier_try;
  team->barrier_pf =     (team == GASNET_TEAM_ALL) ? &gasnete_gdbarrier_kick_team_all : NULL;
}

/* ------------------------------------------------------------------------------------ */
/*
  Vector, Indexed & Strided:
  =========================
*/

/* use reference implementation of scatter/gather and strided */
#include "gasnet_extended_refvis.h"

/* ------------------------------------------------------------------------------------ */
/*
  Collectives:
  ============
*/

/* use reference implementation of collectives */
#include "gasnet_extended_refcoll.h"

/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
  #ifdef GASNETE_REFBARRIER_HANDLERS
    GASNETE_REFBARRIER_HANDLERS(),
  #endif
  #ifdef GASNETE_REFVIS_HANDLERS
    GASNETE_REFVIS_HANDLERS()
  #endif
  #ifdef GASNETE_REFCOLL_HANDLERS
    GASNETE_REFCOLL_HANDLERS()
  #endif

  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */
  gasneti_handler_tableentry_with_bits(gasnete_memset_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_markdone_reph),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable(void) {
  return gasnete_handlers;
}
/* ------------------------------------------------------------------------------------ */

