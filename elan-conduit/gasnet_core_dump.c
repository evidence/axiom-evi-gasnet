/*  $Archive:: /Ti/GASNet/elan-conduit/gasnet_core_dump.c                  $
 *     $Date: 2003/10/11 13:09:56 $
 * $Revision: 1.11 $
 * Description: GASNet elan conduit - elan informational dumps
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_core_internal.h>
#include <elan3/elan3.h> /* for DMA_BYTE */

/* ------------------------------------------------------------------------------------ */
extern void gasnetc_dump_base() {
  ELAN_BASE *b = BASE();

  GASNETI_STATS_PRINTF(C,("ELAN_BASE: {"));
  GASNETI_STATS_PRINTF(C,(" init= %i",b->init));
  { char *dmatype="unknown";
    switch(b->dmaType) {
      case DMA_BYTE:        dmatype = "DMA_BYTE"; break;
      case DMA_HALFWORD:    dmatype = "DMA_HALFWORD"; break;
      case DMA_WORD:        dmatype = "DMA_WORD"; break;
    #ifdef DMA_DOUBLE
      case DMA_DOUBLE:  dmatype = "DMA_DOUBLE"; break;
    #endif
    #ifdef DMA_DOUBLEWORD
      case DMA_DOUBLEWORD:  dmatype = "DMA_DOUBLEWORD"; break;
    #endif
    }
    GASNETI_STATS_PRINTF(C,(" dmaType= %s",dmatype));
  }
  GASNETI_STATS_PRINTF(C,(" retryCount= %i",b->retryCount));
  { char waitType[255];
    switch(b->waitType) {
      case ELAN_POLL_EVENT: strcpy(waitType,"ELAN_POLL_EVENT"); break;
      case ELAN_WAIT_EVENT: strcpy(waitType,"ELAN_WAIT_EVENT"); break;
      default: sprintf(waitType,"spin-poll iterations: %i",(int)b->waitType);
    }
    GASNETI_STATS_PRINTF(C,(" waitType= %s",waitType));
  }

  #if ELAN_VERSION_GE(1,4,8)
  { char flagstr[255];
    flagstr[0] = '\0';
    if(b->flags&ELAN_EVICT_CACHE)     strcat(flagstr,"ELAN_EVICT_CACHE,");
    if(b->flags&ELAN_SHM_ENABLE)      strcat(flagstr,"ELAN_SHM_ENABLE,");
    if(b->flags&ELAN_TRAP_UNALIGNED)  strcat(flagstr,"ELAN_TRAP_UNALIGNED,");
    if(b->flags&ELAN_MULTI_CONTEXT)   strcat(flagstr,"ELAN_MULTI_CONTEXT,");
    if (flagstr[0]) flagstr[strlen(flagstr)-1] = '\0';
    GASNETI_STATS_PRINTF(C,(" flags= %i (%s)",b->flags,flagstr));
  }
  #endif
  GASNETI_STATS_PRINTF(C,(" group_rbufsize= %i",b->group_rbufsize));
  #if ELAN_VERSION_GE(1,4,8)
    GASNETI_STATS_PRINTF(C,(" group_cbufsize= %i",b->group_cbufsize));
    GASNETI_STATS_PRINTF(C,(" group_maxsegs= %i",b->group_maxsegs));
  #endif
  GASNETI_STATS_PRINTF(C,(" group_branch= %i",b->group_branch));
  #if ELAN_VERSION_GE(1,4,8)
    GASNETI_STATS_PRINTF(C,(" group_flags= %i%s",b->group_flags,(b->group_flags&ELAN_HWBCAST?" (ELAN_HWBCAST)":"")));
  #else
    GASNETI_STATS_PRINTF(C,(" group_hwbcast= %i",b->group_hwbcast));
  #endif

  GASNETI_STATS_PRINTF(C,(" tport_nslots= %i",b->tport_nslots));
  #if ELAN_VERSION_GE(1,4,8)
  GASNETI_STATS_PRINTF(C,(" tport_flags= %i",b->tport_flags));
  GASNETI_STATS_PRINTF(C,(" tport_nqxd= %i",b->tport_nqxd));
  #endif
  GASNETI_STATS_PRINTF(C,(" tport_smallmsg= %i",b->tport_smallmsg));
  GASNETI_STATS_PRINTF(C,(" tport_bigmsg= %i",b->tport_bigmsg));
  #if ELAN_VERSION_GE(1,3,0)
  GASNETI_STATS_PRINTF(C,(" tport_fragsize= %i",b->tport_fragsize));
  #endif
  #if ELAN_VERSION_GE(1,4,8)
    GASNETI_STATS_PRINTF(C,(" tport_stripemsg= %i",b->tport_stripemsg));
  #else
    GASNETI_STATS_PRINTF(C,(" evict_cache= %i",b->evict_cache));
  #endif

  #if ELAN_VERSION_GE(1,4,8)
    GASNETI_STATS_PRINTF(C,(" putget_flags= %i",b->putget_flags));
    GASNETI_STATS_PRINTF(C,(" putget_smallputsize= %i",b->putget_smallputsize));
    GASNETI_STATS_PRINTF(C,(" putget_stripeputsize= %i",b->putget_stripeputsize));
    GASNETI_STATS_PRINTF(C,(" putget_stripegetsize= %i",b->putget_stripegetsize));
    GASNETI_STATS_PRINTF(C,(" putget_throttle= %i",b->putget_throttle));

    GASNETI_STATS_PRINTF(C,(" mqueue_flags= %i",b->mqueue_flags));
    GASNETI_STATS_PRINTF(C,(" mqueue_slotsize= %i",b->mqueue_slotsize));
    GASNETI_STATS_PRINTF(C,(" mqueue_nslots= %i",b->mqueue_nslots));

    GASNETI_STATS_PRINTF(C,(" lock_flags= %i",b->lock_flags));

    GASNETI_STATS_PRINTF(C,(" galloc_flags= %i",b->galloc_flags));
    GASNETI_STATS_PRINTF(C,(" galloc_size= %i",b->galloc_size));
    GASNETI_STATS_PRINTF(C,(" gallocElan_size= %i",b->gallocElan_size));

    GASNETI_STATS_PRINTF(C,(" shm_flags= %i",b->shm_flags));
  #else
      GASNETI_STATS_PRINTF(C,(" galloc= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc)));
      GASNETI_STATS_PRINTF(C,(" galloc_size= %i",b->galloc_size));
    #if 0
      GASNETI_STATS_PRINTF(C,(" galloc_base= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc_base)));
    #elif defined(ELAN_VER_1_2)
      GASNETI_STATS_PRINTF(C,(" galloc_mbase= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc_mbase)));
      GASNETI_STATS_PRINTF(C,(" galloc_ebase= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->galloc_ebase)));
    #else
      GASNETI_STATS_PRINTF(C,(" gallocElan= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->gallocElan)));
      GASNETI_STATS_PRINTF(C,(" gallocElan_size= %i",b->gallocElan_size));
    #endif

      GASNETI_STATS_PRINTF(C,(" shm_enable= %i",b->shm_enable));
      GASNETI_STATS_PRINTF(C,(" shm_heapsize= %i",b->shm_heapsize));
      GASNETI_STATS_PRINTF(C,(" gallocShm= "GASNETI_LADDRFMT"",GASNETI_LADDRSTR(b->gallocShm)));
    #if 0
      GASNETI_STATS_PRINTF(C,(" gallocShm_size= %i",b->gallocShm_size));
    #endif
  #endif

  GASNETI_STATS_PRINTF(C,(" shm_key= %i",b->shm_key));
  GASNETI_STATS_PRINTF(C,(" shm_fragsize= %i",b->shm_fragsize));
  GASNETI_STATS_PRINTF(C,(" shm_fifodepth= %i",b->shm_fifodepth));
  GASNETI_STATS_PRINTF(C,(" shm_bigmsg= %i",b->shm_bigmsg));

  GASNETI_STATS_PRINTF(C,("}"));

}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_dump_state() {
  ELAN_STATE *s = STATE();

  GASNETI_STATS_PRINTF(C,("ELAN_STATE: {"));

  GASNETI_STATS_PRINTF(C,(" version= %s",  s->version));
  GASNETI_STATS_PRINTF(C,(" attached= %i", s->attached));
  GASNETI_STATS_PRINTF(C,(" vp= %i", s->vp));
  GASNETI_STATS_PRINTF(C,(" nvp= %i", s->nvp));
  GASNETI_STATS_PRINTF(C,(" ctx= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->ctx)));
  GASNETI_STATS_PRINTF(C,(" estate= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->estate)));
  GASNETI_STATS_PRINTF(C,(" cap= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->cap)));
#ifdef ELAN_VER_1_2
  GASNETI_STATS_PRINTF(C,(" main_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->main_base)));
  GASNETI_STATS_PRINTF(C,(" elan_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->elan_base)));
  GASNETI_STATS_PRINTF(C,(" alloc_size= %i", s->alloc_size));
#else
  GASNETI_STATS_PRINTF(C,(" alloc_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->alloc_base)));
  GASNETI_STATS_PRINTF(C,(" alloc_size= %i", s->alloc_size));
  GASNETI_STATS_PRINTF(C,(" allocElan_base= "GASNETI_LADDRFMT"", GASNETI_LADDRSTR(s->allocElan_base)));
  GASNETI_STATS_PRINTF(C,(" allocElan_size= %i", s->allocElan_size));
#endif

  GASNETI_STATS_PRINTF(C,(" debugFlags= %i", s->debugFlags));
  GASNETI_STATS_PRINTF(C,(" localId= %i", s->localId));
  GASNETI_STATS_PRINTF(C,(" maxLocalIds= %i", s->maxLocalIds));

  { int i;
    char idstr[1024];
    idstr[0] = '\0';
    for (i=0; i < s->nLocalIds; i++) {
      char tmp[10];
      sprintf(tmp," %i ", s->localIds[i]);
      strcat(idstr,tmp);
    }
    GASNETI_STATS_PRINTF(C,("local_ids= (%s)",idstr));
  }

  GASNETI_STATS_PRINTF(C,("}"));
}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_dump_group() {
  ELAN_GROUP *g = GROUP();

  GASNETI_STATS_PRINTF(C,("ELAN_GROUP: {"));

  GASNETI_STATS_PRINTF(C,(" self= %i ", g->self));
  GASNETI_STATS_PRINTF(C,(" size= %i ", g->size));
  { int i;
    char vpstr[1024];
    vpstr[0] = '\0';
    for (i=0; i < g->size; i++) {
      char tmp[10];
      sprintf(tmp," %i ", g->lookupFn(g->handle, i));
      strcat(vpstr,tmp);
    }
    GASNETI_STATS_PRINTF(C,("VPs= (%s)",vpstr));
  }

#if 0
  GASNETI_STATS_PRINTF(C,(" bcastVp= %i ", g->bcastVp));
#endif

  GASNETI_STATS_PRINTF(C,("}"));

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

    "LIBELAN_STATFILE",
    "LIBELAN_STATSIG",
    "LIBELAN_STATOPTIONS",

    "LIBELAN_NATTACH",

    "LIBELAN_PUTGET_SMALLPUTSIZE",
    "LIBELAN_PUTGET_THROTTLE",

  #if 1
    "RMS_JOBID",
    "RMS_NNODES",
    "RMS_NODEID",
    "RMS_NPROCS",
    "RMS_RANK",
    "RMS_RAILS",
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
    int found=0;
    for (i=0; i < (sizeof(ev)/sizeof(char*)); i++) {
      if (getenv(ev[i])) {
        if (!found) {
          GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
          GASNETI_STATS_PRINTF(C,("ELAN Environment Variables:"));
          found=1;
        }
        GASNETI_STATS_PRINTF(C,(" %s=%s", ev[i], getenv(ev[i])));
      }
    }
    if (found) GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
  }
}
/* ------------------------------------------------------------------------------------ */
void gasnetc_dump_tportstats() {
  ELAN_TPORTSTATS stats;
  LOCK_ELAN_WEAK();
    elan_tportGetStats(TPORT(), &stats);
  UNLOCK_ELAN_WEAK();

  #define DUMP_STAT(statname, desc) \
    GASNETI_STATS_PRINTF(C,(" %-20s= %-8lu \t("desc")", "ts_"#statname, (unsigned long)stats.ts_##statname))

  GASNETI_STATS_PRINTF(C,("ELAN_TPORTSTATS: {"));
  DUMP_STAT(dRxBytes, "Number of bytes directly received to user buffer");
  DUMP_STAT(bRxBytes, "Number of bytes received via a buffer");
  DUMP_STAT(txBytes, "Number of bytes transmitted");
#if ELAN_VERSION_GE(1,4,12)
  if (ts_txrBytes[0])
    DUMP_STAT(txrBytes[0], "Number of bytes transmitted (rail 0)");
  if (ts_txrBytes[1])
    DUMP_STAT(txrBytes[1], "Number of bytes transmitted (rail 1)");
#endif
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
  #if ELAN_VERSION_GE(1,4,10)
    DUMP_STAT(nRxWaitSleep, "Number times we slept in RxWait");
    DUMP_STAT(nTxWaitSleep, "Number times we slept in TxWait");
    DUMP_STAT(nTxSmall, "Number of small transmits");
    DUMP_STAT(nTxBig, "Number of big transmits");
    DUMP_STAT(nTxBigSync, "Number of big TXSYNC transmits");
    DUMP_STAT(nTxPsycho, "Number of big Psycho transmits");
    DUMP_STAT(nTxFragDesc, "Number of tx FRAG descriptors allocated");
    DUMP_STAT(nRxFragDesc, "Number of rx FRAG descriptors allocated");
    #if ELAN_VERSION_GE(1,4,12)
    { int i;
      for (i=0;i<64;i++) {
        if (stats.ts_txBin[i]) {
          char msg[80];
          sprintf(msg,"Tx msg count(sz=%i)",(1<<i));
          DUMP_STAT(txBin[i], msg);
        }
      }
    }
    #endif
  #else
    DUMP_STAT(rxLockEWait, "Number of waits done by Elan thread");
    DUMP_STAT(rxLockMWait, "Number of waits done by Main thread");
    DUMP_STAT(bufLockEWait, "Number of waits done by Elan thread");
    DUMP_STAT(bufLockMWait, "Number of waits done by Main thread");
  #endif

  GASNETI_STATS_PRINTF(C,("}"));

  #undef DUMP_STAT
}
/* ------------------------------------------------------------------------------------ */
void gasnetc_dump_groupstats() {
  ELAN_GROUPSTATS stats;
  LOCK_ELAN_WEAK();
    elan_groupGetStats(GROUP(), &stats);
  UNLOCK_ELAN_WEAK();

  #define DUMP_STAT(statname, desc) \
    GASNETI_STATS_PRINTF(C,(" %-20s= %-8lu \t("desc")", "gs_"#statname, (unsigned long)stats.gs_##statname))

  GASNETI_STATS_PRINTF(C,("ELAN_GROUPSTATS: {"));
  DUMP_STAT(bcastBytes, "Number of bytes transmitted via bcast");
  DUMP_STAT(hbcastBytes, "Number of bytes transmitted via hbcast");
  DUMP_STAT(bcastBytesNet, "Number of bytes transmitted via bcastNet");
  DUMP_STAT(bcastBytesShm, "Number of bytes transmitted via bcastShm");
#if ELAN_VERSION_GE(1,4,0)
  DUMP_STAT(reduce_internalBytes, "Number of bytes accumulated via reduce_internal");
  DUMP_STAT(reduce_internalBytesNet, "Number of bytes accumulated via reduce_internalNet");
  DUMP_STAT(reduce_internalBytesShm, "Number of bytes accumulated via reduce_internalShm");
  DUMP_STAT(nReduceI, "Number of ReduceI calls");
  DUMP_STAT(nReduceIShm, "Number of ReduceIShm calls");
#else
  DUMP_STAT(reduceBytes, "Number of bytes accumulated via reduce");
  DUMP_STAT(reduceBytesNet, "Number of bytes accumulated via reduceNet");
  DUMP_STAT(reduceBytesShm, "Number of bytes accumulated via reduceShm");
  DUMP_STAT(nReduce, "Number of Reduce calls");
  DUMP_STAT(nReduceShm, "Number of ReduceShm calls");
#endif
  DUMP_STAT(nGsync, "Number of Gsync calls");
  DUMP_STAT(nHGsync, "Number of HGsync calls");
  DUMP_STAT(nGsyncShm, "Number of GsyncShm calls");
  DUMP_STAT(nBcast, "Number of Bcast calls");
  DUMP_STAT(nHBcast, "Number of HBcast calls");
  DUMP_STAT(nBcastShm, "Number of Bcast calls");

  #if ELAN_VERSION_GE(1,4,8)
    DUMP_STAT(nGather, "Number of Gather calls");
    DUMP_STAT(nGatherBytes, "Number of bytes sent by Gather calls");
  #endif

  GASNETI_STATS_PRINTF(C,("}"));

  #undef DUMP_STAT
}
/* ------------------------------------------------------------------------------------ */
