/*  $Archive:: /Ti/GASNet/gasnet_mmap.c                   $
 *     $Date: 2002/09/13 13:41:40 $
 * $Revision: 1.3 $
 * Description: GASNet memory-mapping utilities
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef GASNETI_GASNET_INTERNAL_C
  #error This file not meant to be compiled directly - included by gasnet_internal.c
#endif

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_MMAP
#include <sys/mman.h>
/* ------------------------------------------------------------------------------------ */
/* MMAP_INITIAL_SIZE controls the maz size segment attempted by the mmap binary search
 * MMAP_GRANULARITY is the minimum increment used by the mmap binary search
 */
#ifndef GASNETI_MMAP_MAX_SIZE
#define GASNETI_MMAP_MAX_SIZE	  (((size_t)2)<<30)  /* 2 GB */
#endif
#ifndef GASNETI_MMAP_GRANULARITY
#define GASNETI_MMAP_GRANULARITY  (((size_t)2)<<21)  /* 4 MB */
#endif

#if defined(IRIX)
  #define GASNETI_MMAP_FLAGS (MAP_PRIVATE | MAP_SGI_ANYADDR | MAP_AUTORESRV)
  #define GASNETI_MMAP_FILE "/dev/zero"
#elif defined(CRAYT3E)
  #error mmap not supported on Cray-T3E
#else
  #define GASNETI_MMAP_FLAGS (MAP_ANON | MAP_PRIVATE)
#endif

static int gasneti_mmapfd = -1;

/* ------------------------------------------------------------------------------------ */
extern void gasneti_mmap_fixed(void *segbase, size_t segsize) {
  void	*ptr;

  #ifdef GASNETI_MMAP_FILE
    if (gasneti_mmapfd == -1) {
      gasneti_mmapfd = open(GASNETI_MMAP_FILE, O_RDWR);
      if (gasneti_mmapfd == -1) 
        gasneti_fatalerror("failed to open "GASNETI_MMAP_FILE" for mmap : %s\n",strerror(errno));
    }
  #endif

  GASNETI_TRACE_PRINTF(C, 
      ("mmap fixed("GASNETI_LADDRFMT", %lu)\n", GASNETI_LADDRSTR(segbase), (unsigned long)segsize) );
  ptr = mmap(segbase, segsize, (PROT_READ|PROT_WRITE), 
	  (GASNETI_MMAP_FLAGS | MAP_FIXED), gasneti_mmapfd, 0);
  if (ptr == MAP_FAILED) {
      gasneti_fatalerror("mmap failed at "GASNETI_LADDRFMT" for size %lu: %s\n",
	      GASNETI_LADDRSTR(segbase), (unsigned long)segsize, strerror(errno));
  }
  if (segbase != ptr) {
    gasneti_fatalerror("mmap fixed moved from "GASNETI_LADDRFMT" to "GASNETI_LADDRFMT" for size %lu\n",
	    GASNETI_LADDRSTR(segbase), GASNETI_LADDRSTR(ptr), (unsigned long)segsize);
  }
}
/* ------------------------------------------------------------------------------------ */
extern void gasneti_munmap(void *segbase, size_t segsize) {
  assert(segsize > 0);
  if (munmap(segbase, segsize) != 0) 
    gasneti_fatalerror("munmap("GASNETI_LADDRFMT",%lu) failed: %s\n",
	    GASNETI_LADDRSTR(segbase), (unsigned long)segsize, strerror(errno));
}
/* ------------------------------------------------------------------------------------ */
static gasnet_seginfo_t gasneti_mmap_segsrch(size_t lowsz, size_t highsz) {
  gasnet_seginfo_t si;
  size_t pagesize = gasneti_getSystemPageSize();
  gasneti_stattime_t t1, t2;

  if (highsz - lowsz <= GASNETI_MMAP_GRANULARITY) {
    si.size = 0;
    si.addr = NULL;
    return si;
  }

  si.size = GASNETI_PAGE_ALIGN((lowsz + (highsz - lowsz) / 2), pagesize);
  assert(si.size > 0);

  #ifdef GASNETI_MMAP_FILE
    if (gasneti_mmapfd == -1) {
      gasneti_mmapfd = open(GASNETI_MMAP_FILE, O_RDWR);
      if (gasneti_mmapfd == -1) 
        gasneti_fatalerror("failed to open "GASNETI_MMAP_FILE" for mmap : %s\n",strerror(errno));
    }
  #endif

  t1 = GASNETI_STATTIME_NOW();
  si.addr = 
      mmap(NULL, si.size, PROT_READ|PROT_WRITE, GASNETI_MMAP_FLAGS, gasneti_mmapfd, 0);
  t2 = GASNETI_STATTIME_NOW();

  if (si.addr == MAP_FAILED) {
    if (errno != ENOMEM)
      gasneti_fatalerror("mmap failed for size %lu: %s\n", (unsigned long)si.size, strerror(errno));
    GASNETI_TRACE_PRINTF(D,("mmap(%lu) %dus FAILED: %s\n", (unsigned long)si.size,
       (unsigned int) GASNETI_STATTIME_TO_US(t2-t1), strerror(errno)) );
    return gasneti_mmap_segsrch(lowsz, si.size);
  } else {
    gasnet_seginfo_t si_temp;
    GASNETI_TRACE_PRINTF(D,("mmap(%lu) %dus = "GASNETI_LADDRFMT"\n", (unsigned long)si.size,
       (unsigned int) GASNETI_STATTIME_TO_US(t2-t1), GASNETI_LADDRSTR(si.addr)) );
    gasneti_munmap(si.addr, si.size);

    si_temp = gasneti_mmap_segsrch(si.size, highsz);
    if (si_temp.size) return si_temp;
    else return si;
  }
}

/* gasneti_mmap_segment_search allocates the largest possible page-aligned mmap 
 * with sz <= maxsz and returns the base address and size
 */
extern gasnet_seginfo_t gasneti_mmap_segment_search(uintptr_t maxsz) {
  size_t pagesize = gasneti_getSystemPageSize();
  gasnet_seginfo_t si = gasneti_mmap_segsrch(0, maxsz);

  /*  ensure page-alignment of base and size */
  { uintptr_t begin = (uintptr_t)si.addr;
    uintptr_t end = (uintptr_t)si.addr + si.size;
    begin = GASNETI_PAGE_ROUNDUP(begin, pagesize);
    end = GASNETI_PAGE_ALIGN(end, pagesize);
    si.addr = (void *)begin;
    si.size = end - begin;
    assert(((uintptr_t)si.addr) % pagesize == 0 && si.size % pagesize == 0);
  }

  gasneti_mmap_fixed(si.addr, si.size);
  return si;
}
/* ------------------------------------------------------------------------------------ */
#endif /* HAVE_MMAP */

static gasnet_node_t gasneti_nodes = 0;
static gasnet_seginfo_t gasneti_segment = {0,0}; /* local segment info */
static uintptr_t gasneti_myheapend = 0; /* top of my malloc heap */
static uintptr_t gasneti_maxheapend = 0; /* top of max malloc heap */
static uintptr_t gasneti_maxbase = 0; /* start of segment overlap region */
static uintptr_t gasneti_MaxLocalSegmentSize = 0;
static uintptr_t gasneti_MaxGlobalSegmentSize = 0;

typedef struct {
  gasnet_seginfo_t seginfo;
  uintptr_t heapend;
  uintptr_t segsize_request; /* during attach only */
} gasneti_segexch_t;
static gasneti_segexch_t *gasneti_segexch = NULL; /* exchanged segment information */

/* do the work necessary for initing a standard segment map in arbitrary memory 
     uses mmap if available, or malloc otherwise
   requires an exchange callback function that can be used to exchange data
   sets max local & global segment size
   localSegmentLimit provides an optional conduit-specific limit on max segment sz
    (for example, to limit size based on physical memory availability)
    pass (uintptr_t)-1 for unlimited
   keeps internal state for attach
 */
void gasneti_segmentInit(uintptr_t *MaxLocalSegmentSize, 
                         uintptr_t *MaxGlobalSegmentSize,
                         uintptr_t localSegmentLimit,
                         gasnet_node_t numnodes,
                         gasneti_bootstrapExchangefn_t exchangefn) {
  size_t pagesize = gasneti_getSystemPageSize();
  gasneti_segexch_t se;
  int i;

  assert(MaxLocalSegmentSize);
  assert(MaxGlobalSegmentSize);
  assert(exchangefn);
  assert(numnodes);
  gasneti_nodes = numnodes;

  gasneti_segexch = (gasneti_segexch_t *)gasneti_malloc_inhandler(gasneti_nodes*sizeof(gasneti_segexch_t));

  #ifdef HAVE_MMAP
    gasneti_segment = gasneti_mmap_segment_search(localSegmentLimit == (uintptr_t)-1 ?
                                                  GASNETI_MMAP_MAX_SIZE : localSegmentLimit);
    GASNETI_TRACE_PRINTF(C, ("My segment: addr="GASNETI_LADDRFMT"  sz=%lu",
      GASNETI_LADDRSTR(gasneti_segment.addr), (unsigned long)gasneti_segment.size));

    se.seginfo = gasneti_segment;
    gasneti_myheapend = (uintptr_t)sbrk(0);
    if (gasneti_myheapend == -1) gasneti_fatalerror("Failed to sbrk(0):%s",strerror(errno));
    gasneti_myheapend = GASNETI_PAGE_ROUNDUP(gasneti_myheapend, pagesize);
    se.heapend = gasneti_myheapend;
    se.segsize_request = 0;

    /* gather the sbrk info and mmap segment location */
    (*exchangefn)(&se, sizeof(gasneti_segexch_t), gasneti_segexch);

    /* compute bounding-box of segment location */
    { uintptr_t maxbase = 0;
      uintptr_t maxsize = 0;
      uintptr_t minsize = (uintptr_t)-1;
      uintptr_t minend = (uintptr_t)-1;
      uintptr_t maxheapend = 0;
      /* compute various stats across nodes */
      for (i=0;i < gasneti_nodes; i++) {
        if (gasneti_segexch[i].heapend > maxheapend)
          maxheapend = gasneti_segexch[i].heapend;
        if (((uintptr_t)gasneti_segexch[i].seginfo.addr) > maxbase)
          maxbase = (uintptr_t)gasneti_segexch[i].seginfo.addr;
        if (gasneti_segexch[i].seginfo.size > maxsize)
          maxsize = gasneti_segexch[i].seginfo.size;
        if (gasneti_segexch[i].seginfo.size < minsize)
          minsize = gasneti_segexch[i].seginfo.size;
        if ((uintptr_t)gasneti_segexch[i].seginfo.addr + gasneti_segexch[i].seginfo.size < minend)
          minend = (uintptr_t)gasneti_segexch[i].seginfo.addr + gasneti_segexch[i].seginfo.size;
      }
      GASNETI_TRACE_PRINTF(C, ("Segment stats: "
          "maxsize = %lu   "
          "minsize = %lu   "
          "maxbase = "GASNETI_LADDRFMT"   "
          "minend = "GASNETI_LADDRFMT"   "
          "maxheapend = "GASNETI_LADDRFMT"   ",
          (unsigned long)maxsize, (unsigned long)minsize,
          GASNETI_LADDRSTR(maxbase), GASNETI_LADDRSTR(minend), GASNETI_LADDRSTR(maxheapend)
          ));

      gasneti_maxheapend = maxheapend;
      gasneti_maxbase = maxbase;
      #if GASNET_ALIGNED_SEGMENTS
        if (maxbase >= minend) { /* no overlap - maybe should be a fatal error... */
          GASNETI_TRACE_PRINTF(I, ("WARNING: unable to locate overlapping mmap segments in gasneti_segmentInit()"));
          gasneti_MaxLocalSegmentSize = 0;
          gasneti_MaxGlobalSegmentSize = 0;
        } else {
          gasneti_MaxLocalSegmentSize = ((uintptr_t)gasneti_segment.addr + gasneti_segment.size) - maxbase;
          gasneti_MaxGlobalSegmentSize = minend - maxbase;
        }
      #else
        gasneti_MaxLocalSegmentSize = gasneti_segment.size;
        gasneti_MaxGlobalSegmentSize = minsize;
      #endif
    }
  #else /* !HAVE_MMAP */
    #if GASNET_ALIGNED_SEGMENTS
      #error bad config: dont know how to provide GASNET_ALIGNED_SEGMENTS when !HAVE_MMAP
    #endif
    /* TODO: T3E doesn't support mmap - find a way to determine a true max seg sz */
    if (localSegmentLimit < GASNETI_MAX_MALLOCSEGMENT_SZ) {
      gasneti_MaxLocalSegmentSize =  localSegmentLimit;
      gasneti_MaxGlobalSegmentSize = localSegmentLimit;
    } else {
      gasneti_MaxLocalSegmentSize =  GASNETI_MAX_MALLOCSEGMENT_SZ;
      gasneti_MaxGlobalSegmentSize = GASNETI_MAX_MALLOCSEGMENT_SZ;
    }
  #endif
  GASNETI_TRACE_PRINTF(C, ("MaxLocalSegmentSize = %lu   "
                     "MaxGlobalSegmentSize = %lu",
                     (unsigned long)gasneti_MaxLocalSegmentSize, 
                     (unsigned long)gasneti_MaxGlobalSegmentSize));
  assert(gasneti_MaxLocalSegmentSize % pagesize == 0);
  assert(gasneti_MaxGlobalSegmentSize % pagesize == 0);
  assert(gasneti_MaxGlobalSegmentSize <= gasneti_MaxLocalSegmentSize);
  assert(gasneti_MaxLocalSegmentSize <= localSegmentLimit);

  *MaxLocalSegmentSize = gasneti_MaxLocalSegmentSize;
  *MaxGlobalSegmentSize = gasneti_MaxGlobalSegmentSize;
}

/* ------------------------------------------------------------------------------------ */
/* do the work necessary to attach a segment that has been initted by gasneti_segmentInit()
   pass in the segsize (which must be <= MaxGlobalSegmentSize) and minheapoffset
     supplied by the user
   sets segbase and fills in seginfo */
void gasneti_segmentAttach(uintptr_t segsize, uintptr_t minheapoffset,
                           gasnet_seginfo_t *seginfo,
                           gasneti_bootstrapExchangefn_t exchangefn) {
  size_t pagesize = gasneti_getSystemPageSize();
  void *segbase = NULL;
  assert(seginfo);
  assert(exchangefn);
  assert(gasneti_segexch);

  #ifdef HAVE_MMAP
  { /* TODO: this assumes heap grows up */
    uintptr_t topofheap;
    #if GASNET_ALIGNED_SEGMENTS
      #if GASNETI_USE_HIGHSEGMENT
        { /* the segsizes requested may differ across nodes, so in order to 
             place the segment as high as possible while maintaining alignment, 
             we need another all-to-all to calculate the new aligned base address
           */
          gasneti_segexch_t se;
          uintptr_t minsegstart = (uintptr_t)-1;
          int i;

          /* gather the segsize info again */
          se.seginfo = gasneti_segment;
          se.heapend = gasneti_myheapend;
          se.segsize_request = segsize;
          (*exchangefn)(&se, sizeof(gasneti_segexch_t), gasneti_segexch);

          for (i=0;i<gasneti_nodes;i++) {
            uintptr_t segstart = 
                ((uintptr_t)gasneti_segexch[i].seginfo.addr + gasneti_segexch[i].seginfo.size) - 
                 gasneti_segexch[i].segsize_request;
            assert(gasneti_segexch[i].segsize_request >= 0);
            assert(segstart >= gasneti_maxbase);
            if (segstart < minsegstart) minsegstart = segstart;
          }

          segbase = (void *)minsegstart;
        }
      #else
        segbase = (void *)gasneti_maxbase;
      #endif
      topofheap = gasneti_maxheapend;
    #else
      topofheap = gasneti_myheapend;
      #if GASNETI_USE_HIGHSEGMENT
        segbase = (void *)((uintptr_t)gasneti_segment.addr + gasneti_segment.size - segsize);
      #else
        segbase = gasneti_segment.addr;
      #endif
    #endif

    if (segsize == 0) { /* no segment */
      gasneti_munmap(gasneti_segment.addr, gasneti_segment.size);
      segbase = NULL; 
    }
    else {
      if (topofheap + minheapoffset > (uintptr_t)segbase) {
        int maxsegsz;
        /* we're too close to the heap - readjust to prevent collision 
           note this allows us to return different segsizes on diff nodes
           (even when we are using GASNET_ALIGNED_SEGMENTS)
         */
        segbase = (void *)(topofheap + minheapoffset);
        maxsegsz = ((uintptr_t)gasneti_segment.addr + gasneti_segment.size) - (uintptr_t)segbase;
        if (maxsegsz <= 0) gasneti_fatalerror("minheapoffset too large to accomodate a segment");
        if (segsize > maxsegsz) {
          GASNETI_TRACE_PRINTF(I, ("WARNING: gasneti_segmentAttach() reducing requested segsize (%lu=>%lu) to accomodate minheapoffset",
            segsize, maxsegsz));
          segsize = maxsegsz;
        }
      }

      /* trim final segment if required */
      if (gasneti_segment.addr != segbase || gasneti_segment.size != segsize) {
        assert(segbase >= gasneti_segment.addr &&
               (uintptr_t)segbase + segsize <= (uintptr_t)gasneti_segment.addr + gasneti_segment.size);
        gasneti_munmap(gasneti_segment.addr, gasneti_segment.size);
        gasneti_mmap_fixed(segbase, segsize);
      }
    }
  }
  #else /* !HAVE_MMAP */
    /* for the T3E, and other platforms which don't support mmap */
    segbase = malloc(segsize);
    while (!segbase) {
      segsize = GASNETI_PAGE_ALIGN(segsize/2, pagesize);
      if (segsize == 0) break; 
      segbase = malloc(segsize + pagesize);
    }
    if (segbase) segbase = (void *)GASNETI_PAGE_ROUNDUP(segbase, pagesize);
  #endif
  assert(((uintptr_t)segbase) % pagesize == 0);
  assert(segsize % pagesize == 0);
  GASNETI_TRACE_PRINTF(C, ("Final segment: segbase="GASNETI_LADDRFMT"  segsize=%lu",
    GASNETI_LADDRSTR(segbase), (unsigned long)segsize));

  /*  gather segment information */
  gasneti_segment.addr = segbase;
  gasneti_segment.size = segsize;
  (*exchangefn)(&gasneti_segment, sizeof(gasnet_seginfo_t), seginfo);

  #if GASNET_ALIGNED_SEGMENTS == 1
    { int i; /*  check that segments are aligned */
      for (i=0; i < gasneti_nodes; i++) {
        if (seginfo[i].size != 0 && seginfo[i].addr != segbase) 
          gasneti_fatalerror("Failed to acquire aligned segments for GASNET_ALIGNED_SEGMENTS");
      }
    }
  #endif
}
/* ------------------------------------------------------------------------------------ */
