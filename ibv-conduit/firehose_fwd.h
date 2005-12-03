/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/firehose_fwd.h,v $
 *     $Date: 2005/12/03 01:42:23 $
 * $Revision: 1.6 $
 * Description: Configuration of firehose code to fit vapi-conduit
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _VAPI_FIREHOSE_FWD_H
#define _VAPI_FIREHOSE_FWD_H

#include <vapi_types.h>

/* Set this here because we need it to match */
#define FH_BUCKET_SIZE	GASNET_PAGESIZE

/* vapi offers "Fast Memory Regions".
 * They really are faster, so we use them by default when available */
#ifndef FIREHOSE_VAPI_USE_FMR
  #if HAVE_VAPI_FMR
    #define FIREHOSE_VAPI_USE_FMR 1
  #else
    #define FIREHOSE_VAPI_USE_FMR 0
  #endif
#endif

/* vapi-conduit uses firehose-region */
#define FIREHOSE_REGION

/* vapi-conduit allows completion callbacks to run in handlers */
#define FIREHOSE_COMPLETION_IN_HANDLER

/* vapi-conduit has a client_t */
#define FIREHOSE_CLIENT_T
typedef struct _firehose_client_t {
    #if FIREHOSE_VAPI_USE_FMR
      EVAPI_fmr_hndl_t handle;	/* used to release the region */
    #else
      VAPI_mr_hndl_t   handle;	/* used to release the region */
    #endif
    VAPI_lkey_t      lkey;	/* used for local access by HCA */
    VAPI_rkey_t      rkey;	/* used for remote access by HCA */
} firehose_client_t;

/* Simple tests show that the additional roundtrip AM latency we pay
 * for piggybacking a large put is smaller than the latency of a small
 * blocking put.  Thus, we expect this to always be a win for puts
 * that can be completed entirely via the AM.  For larger puts we've
 * moved some data from one message to another and expect to roughly
 * break even except for the added memcpy() overheads of the piggyback.
 *
 * XXX: run-time variable-length callback args would be nice here
 * XXX: So would an interface that could eliminate a memcpy on the sender
 */
#ifndef GASNETC_PUTINMOVE_LIMIT
  /* max bytes to piggyback on a put/miss */
  #define GASNETC_PUTINMOVE_LIMIT 2048	/* XXX: untuned */
#endif
#if GASNETC_PUTINMOVE_LIMIT
    typedef struct {
	void	*addr;
	size_t	len;
	char	data[GASNETC_PUTINMOVE_LIMIT];
    } firehose_remotecallback_args_t;
#else
    typedef int firehose_remotecallback_args_t; /* can't disable entirely */
#endif

#define FIREHOSE_REMOTE_CALLBACK_IN_HANDLER

/* Setup conduit-specific region parameters
 * Note that these are kept to sane sizes rather than the HCA limit
 * 128kB is the peak of the bandwidth curve and thus a good size.
 * With 32k * 128k = 4G we can pin upto 4GB of physical memory with these.
 * We don't yet deal well with many small regions.
 */
#define FIREHOSE_CLIENT_MAXREGIONS	32768
#define FIREHOSE_CLIENT_MAXREGION_SIZE	131072

#endif
