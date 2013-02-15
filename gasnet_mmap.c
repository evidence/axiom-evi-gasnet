/*  $Archive:: /Ti/GASNet/gasnet_mmap.c                   $
 *     $Date: 2002/09/02 23:25:00 $
 * $Revision: 1.1 $
 * Description: GASNet memory-mapping utilities
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef GASNETI_GASNET_INTERNAL_C
  #error This file not meant to be compiled directly - included by gasnet_internal.c
#endif

#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* MMAP_INITIAL_SIZE controls the maz size segment attempted by the mmap binary search
 * MMAP_GRANULARITY is the minimum increment used by the mmap binary search
 */
#ifndef GASNETI_MMAP_INITIAL_SIZE
#define GASNETI_MMAP_INITIAL_SIZE	(((size_t)2)<<30)  /* 2 GB */
#endif
#ifndef GASNETI_MMAP_GRANULARITY
#define GASNETI_MMAP_GRANULARITY	(((size_t)2)<<21)  /* 4 MB */
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
 * and returns the base address and size
 */
extern gasnet_seginfo_t gasneti_mmap_segment_search() {
  size_t pagesize = gasneti_getSystemPageSize();
  gasnet_seginfo_t si = gasneti_mmap_segsrch(0, GASNETI_MMAP_INITIAL_SIZE);

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
