/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_dump.c                  $
 *     $Date: 2002/08/05 10:23:44 $
 * $Revision: 1.1 $
 * Description: GASNet elan conduit - elan informational dumps
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet.h>
#include <gasnet_core_internal.h>

/* ------------------------------------------------------------------------------------ */
extern void gasnetc_dump_base() {
  ELAN_BASE *b = BASE();

  GASNETI_TRACE_PRINTF(C,("ELAN_BASE: {"));

  GASNETI_TRACE_PRINTF(C,(" init= %i",b->init));
  { char *dmatype;
    switch(b->dmaType) {
      case DMA_BYTE:        dmatype = "DMA_BYTE"; break;
      case DMA_HALFWORD:    dmatype = "DMA_HALFWORD"; break;
      case DMA_WORD:        dmatype = "DMA_WORD"; break;
    #ifdef DMA_DOUBLEWORD
      case DMA_DOUBLEWORD:  dmatype = "DMA_DOUBLEWORD"; break;
    #endif
    }
    GASNETI_TRACE_PRINTF(C,(" dmaType= %s",dmatype));
  }
  GASNETI_TRACE_PRINTF(C,(" retryCount= %i",b->retryCount));
  { char waitType[255];
    switch(b->waitType) {
      case ELAN_POLL_EVENT: strcpy(waitType,"ELAN_POLL_EVENT"); break;
      case ELAN_WAIT_EVENT: strcpy(waitType,"ELAN_WAIT_EVENT"); break;
      default: sprintf(waitType,"spin-poll iterations: %i",b->waitType);
    }
    GASNETI_TRACE_PRINTF(C,(" waitType= %s",waitType));
  }
  GASNETI_TRACE_PRINTF(C,(" group_rbufsize= %i",b->group_rbufsize));
  GASNETI_TRACE_PRINTF(C,(" group_branch= %i",b->group_branch));
  GASNETI_TRACE_PRINTF(C,(" group_hwbcast= %i",b->group_hwbcast));
  GASNETI_TRACE_PRINTF(C,(" tport_nslots= %i",b->tport_nslots));
  GASNETI_TRACE_PRINTF(C,(" tport_smallmsg= %i",b->tport_smallmsg));
  GASNETI_TRACE_PRINTF(C,(" tport_bigmsg= %i",b->tport_bigmsg));
  #if defined(ELAN_VER_1_3)
  GASNETI_TRACE_PRINTF(C,(" tport_fragsize= %i",b->tport_fragsize));
  #endif
  GASNETI_TRACE_PRINTF(C,(" evict_cache= %i",b->evict_cache));

  GASNETI_TRACE_PRINTF(C,(" galloc= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc)));
  GASNETI_TRACE_PRINTF(C,(" galloc_size= %i",b->galloc_size));
#if 0
  GASNETI_TRACE_PRINTF(C,(" galloc_base= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc_base)));
#elif defined(ELAN_VER_1_2)
  GASNETI_TRACE_PRINTF(C,(" galloc_mbase= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc_mbase)));
  GASNETI_TRACE_PRINTF(C,(" galloc_ebase= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc_ebase)));
#else
  GASNETI_TRACE_PRINTF(C,(" gallocElan= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->gallocElan)));
  GASNETI_TRACE_PRINTF(C,(" gallocElan_size= %i",b->gallocElan_size));
#endif

  GASNETI_TRACE_PRINTF(C,(" shm_enable= %i",b->shm_enable));
  GASNETI_TRACE_PRINTF(C,(" shm_heapsize= %i",b->shm_heapsize));
  GASNETI_TRACE_PRINTF(C,(" gallocShm= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->gallocShm)));
#if 0
  GASNETI_TRACE_PRINTF(C,(" gallocShm_size= %i",b->gallocShm_size));
#endif
  GASNETI_TRACE_PRINTF(C,(" shm_key= %i",b->shm_key));
  GASNETI_TRACE_PRINTF(C,(" shm_fragsize= %i",b->shm_fragsize));
  GASNETI_TRACE_PRINTF(C,(" shm_fifodepth= %i",b->shm_fifodepth));
  GASNETI_TRACE_PRINTF(C,(" shm_bigmsg= %i",b->shm_bigmsg));

  GASNETI_TRACE_PRINTF(C,("}"));

}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_dump_state() {
  ELAN_STATE *s = STATE();

  GASNETI_TRACE_PRINTF(C,("ELAN_STATE: {"));

  GASNETI_TRACE_PRINTF(C,(" version= %s",  s->version));
  GASNETI_TRACE_PRINTF(C,(" attached= %i", s->attached));
  GASNETI_TRACE_PRINTF(C,(" vp= %i", s->vp));
  GASNETI_TRACE_PRINTF(C,(" nvp= %i", s->nvp));
  GASNETI_TRACE_PRINTF(C,(" ctx= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->ctx)));
  GASNETI_TRACE_PRINTF(C,(" estate= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->estate)));
  GASNETI_TRACE_PRINTF(C,(" cap= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->cap)));
#ifdef ELAN_VER_1_2
  GASNETI_TRACE_PRINTF(C,(" main_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->main_base)));
  GASNETI_TRACE_PRINTF(C,(" elan_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->elan_base)));
  GASNETI_TRACE_PRINTF(C,(" alloc_size= %i", s->alloc_size));
#else
  GASNETI_TRACE_PRINTF(C,(" alloc_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->alloc_base)));
  GASNETI_TRACE_PRINTF(C,(" alloc_size= %i", s->alloc_size));
  GASNETI_TRACE_PRINTF(C,(" allocElan_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->allocElan_base)));
  GASNETI_TRACE_PRINTF(C,(" allocElan_size= %i", s->allocElan_size));
#endif

  GASNETI_TRACE_PRINTF(C,(" debugFlags= %i", s->debugFlags));
  GASNETI_TRACE_PRINTF(C,(" localId= %i", s->localId));
  GASNETI_TRACE_PRINTF(C,(" maxLocalIds= %i", s->maxLocalIds));

  { int i;
    char idstr[1024];
    idstr[0] = '\0';
    for (i=0; i < s->nLocalIds; i++) {
      char tmp[10];
      sprintf(tmp," %i ", s->localIds[i]);
      strcat(idstr,tmp);
    }
    GASNETI_TRACE_PRINTF(C,("local_ids= (%s)",idstr));
  }

  GASNETI_TRACE_PRINTF(C,("}"));
}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_dump_group() {
  ELAN_GROUP *g = GROUP();

  GASNETI_TRACE_PRINTF(C,("ELAN_GROUP: {"));

  GASNETI_TRACE_PRINTF(C,(" self= %i ", g->self));
  GASNETI_TRACE_PRINTF(C,(" size= %i ", g->size));
  { int i;
    char vpstr[1024];
    vpstr[0] = '\0';
    for (i=0; i < g->size; i++) {
      char tmp[10];
      sprintf(tmp," %i ", g->lookupFn(g->handle, i));
      strcat(vpstr,tmp);
    }
    GASNETI_TRACE_PRINTF(C,("VPs= (%s)",vpstr));
  }

#if 0
  GASNETI_TRACE_PRINTF(C,(" bcastVp= %i ", g->bcastVp));
#endif

  GASNETI_TRACE_PRINTF(C,("}"));

}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_dump_envvars() {
  FILE *out = stdout;
  const char *ev[] = {
    "LIBELAN_WAITTYPE",
    "LIBELAN_DATATYPE",
    "LIBELAN_RETRYCOUNT",
    "LIBELAN_GROUP_RBUFSIZE",
    "LIBELAN_GROUP_BRANCHRATIO",
    "LIBELAN_GROUP_HWBCAST",
    "LIBELAN_TPORT_NSLOTS",
    "LIBELAN_TPORT_SMALLMSG",
    "LIBELAN_TPORT_BIGMSG",
#if 0
    "LIBELAN_MAIN_BASE",
    "LIBELAN_ELAN_BASE",
#else
    "LIBELAN_ALLOC_BASE",
    "LIBELAN_ALLOCELAN_BASE",
#endif
    "LIBELAN_ALLOC_SIZE",
    "LIBELAN_ALLOCELAN_SIZE",
#if 0
    "LIBELAN_GALLOC_BASE",
#endif
    "LIBELAN_GALLOC_SIZE",
    "LIBELAN_GALLOCELAN_SIZE",
    "LIBELAN_SHM_ENABLE",
    "LIBELAN_SHM_FRAGSIZE",
    "LIBELAN_SHM_FIFODEPTH",
    "LIBELAN_EVICT_CACHE",
    "LIBELAN_DEBUGDUMP",
    "LIBELAN_DEBUGFLAGS",
    "LIBELAN_DEBUGFILE",
    "LIBELAN_DEBUGSIG",
    "LIBELAN_CORE",
    "LIBELAN_TRACE",

  #if 1
    "RMS_JOBID",
    "RMS_NNODES",
    "RMS_NODEID",
    "RMS_NPROCS",
    "RMS_RANK",
    "RMS_RESOURCEID",
    "RMS_TIMELIMIT",
    "RMS_MACHINE",
    "RMS_PROCID",
    "RMS_IMMEDIATE",
    "RMS_KEEP_CORE",
    "RMS_MEMLIMIT",
    "RMS_PARTITION",
    "RMS_PROJECT",
    "RMS_TIMELIMIT",
    "RMS_DEBUG",
    "RMS_EXITTIMEOUT",
    "RMS_STDINMODE",
    "RMS_STDOUTMODE",
    "RMS_STDERRMODE",
  #endif

    ""
  };
  { int i;
    for (i=0; i < (sizeof(ev)/sizeof(char*)); i++) {
      if (getenv(ev[i])) {
        GASNETI_TRACE_PRINTF(C,(" %s=%s", ev[i], getenv(ev[i])));
      }
    }
  }
}
/* ------------------------------------------------------------------------------------ */
void gasnetc_dump_tportstats() {
  ELAN_TPORTSTATS stats;
  elan_tportGetStats(TPORT(), &stats);

  /* TODO: add GASNETI_STATS_PRINTF and use that here instead */
  #define DUMP_STAT(statname, desc) \
    GASNETI_TRACE_PRINTF(C,(" %-20s= %-8lu \t("desc")", "ts_"#statname, (unsigned long)stats.ts_##statname))

  GASNETI_TRACE_PRINTF(C,("ELAN_TPORTSTATS: {"));
  DUMP_STAT(dRxBytes, "Number of bytes directly received to user buffer");
  DUMP_STAT(bRxBytes, "Number of bytes received via a buffer");
  DUMP_STAT(txBytes, "Number of bytes transmitted");
  DUMP_STAT(ndRx, "Number of direct receives");
  DUMP_STAT(nbRx, "Number of buffered receives");
  DUMP_STAT(nTx, "Number of transmits");
  DUMP_STAT(nShmRx, "Number of shared-memory receives");
  DUMP_STAT(nShmTx, "Number of shared-memory transmits");
  DUMP_STAT(nFragRx, "Number of fragmented receives");
  DUMP_STAT(nFragTx, "Number of fragmented transmits");
  DUMP_STAT(nTxDesc, "Number of tx descriptors allocated");
  DUMP_STAT(nRxDesc, "Number of rx descriptors allocated");
  DUMP_STAT(nSyncDesc, "Number of synchronous descriptors allocated");
  DUMP_STAT(nShmDesc, "Number of shared-memory descriptors allocated");
  DUMP_STAT(nBuf, "Number of buffers allocated");
  DUMP_STAT(bufBytes, "Number of bytes allocated to buffers");
  DUMP_STAT(bufAllocFail, "Number of buffer allocation failures");
  DUMP_STAT(rxLockEWait, "Number of waits done by Elan thread");
  DUMP_STAT(rxLockMWait, "Number of waits done by Main thread");
  DUMP_STAT(bufLockEWait, "Number of waits done by Elan thread");
  DUMP_STAT(bufLockMWait, "Number of waits done by Main thread");
  GASNETI_TRACE_PRINTF(C,("}"));

  #undef DUMP_STAT
}
/* ------------------------------------------------------------------------------------ */
void gasnetc_dump_groupstats() {
  ELAN_GROUPSTATS stats;
  elan_groupGetStats(GROUP(), &stats);

  #define DUMP_STAT(statname, desc) \
    GASNETI_TRACE_PRINTF(C,(" %-20s= %-8lu \t("desc")", "gs_"#statname, (unsigned long)stats.gs_##statname))

  GASNETI_TRACE_PRINTF(C,("ELAN_GROUPSTATS: {"));
  DUMP_STAT(bcastBytes, "Number of bytes transmitted via bcast");
  DUMP_STAT(hbcastBytes, "Number of bytes transmitted via hbcast");
  DUMP_STAT(bcastBytesNet, "Number of bytes transmitted via bcastNet");
  DUMP_STAT(bcastBytesShm, "Number of bytes transmitted via bcastShm");
  DUMP_STAT(reduceBytes, "Number of bytes accumulated via reduce");
  DUMP_STAT(reduceBytesNet, "Number of bytes accumulated via reduceNet");
  DUMP_STAT(reduceBytesShm, "Number of bytes accumulated via reduceShm");
  DUMP_STAT(nGsync, "Number of Gsync calls");
  DUMP_STAT(nHGsync, "Number of HGsync calls");
  DUMP_STAT(nGsyncShm, "Number of GsyncShm calls");
  DUMP_STAT(nBcast, "Number of Bcast calls");
  DUMP_STAT(nHBcast, "Number of HBcast calls");
  DUMP_STAT(nBcastShm, "Number of Bcast calls");
  DUMP_STAT(nReduce, "Number of Reduce calls");
  DUMP_STAT(nReduceShm, "Number of ReduceShm calls");
  GASNETI_TRACE_PRINTF(C,("}"));

  #undef DUMP_STAT
}
/* ------------------------------------------------------------------------------------ */
