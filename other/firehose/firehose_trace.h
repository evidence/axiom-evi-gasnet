/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/firehose/firehose_trace.h,v $
 *     $Date: 2006/02/04 01:20:21 $
 * $Revision: 1.1 $
 * Description: Internal Header file
 * Copyright 2004, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_FIREHOSE_STATS
  #define GASNETI_FIREHOSE_STATS(CNT,VAL,TIME)             \
        VAL(C, FH_LOCAL_PIN, pages requested)              \
        CNT(C, FH_LOCAL_HIT, cnt)                          \
        CNT(C, FH_LOCAL_MISS, cnt)                         \
        VAL(C, FH_TRY_LOCAL_PIN, pages requested)          \
        CNT(C, FH_TRY_LOCAL_HIT, cnt)                      \
        CNT(C, FH_TRY_LOCAL_MISS, cnt)                     \
        VAL(C, FH_PARTIAL_LOCAL_PIN, pages requested)      \
        CNT(C, FH_PARTIAL_LOCAL_HIT, cnt)                  \
        CNT(C, FH_PARTIAL_LOCAL_MISS, cnt)                 \
        VAL(C, FH_REMOTE_PIN, pages requested)             \
        CNT(C, FH_REMOTE_HIT, cnt)                         \
        CNT(C, FH_REMOTE_PENDING, cnt)                     \
        CNT(C, FH_REMOTE_MISS, cnt)                        \
        VAL(C, FH_TRY_REMOTE_PIN, pages requested)         \
        CNT(C, FH_TRY_REMOTE_HIT, cnt)                     \
        CNT(C, FH_TRY_REMOTE_MISS, cnt)                    \
        VAL(C, FH_PARTIAL_REMOTE_PIN, pages requested)     \
        CNT(C, FH_PARTIAL_REMOTE_HIT, cnt)                 \
        CNT(C, FH_PARTIAL_REMOTE_MISS, cnt)                \
        VAL(C, FH_RELEASE, firehoses)			   \
        VAL(C, FH_REQUEST_ALLOC, requests allocated)
#endif
