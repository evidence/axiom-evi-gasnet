/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/firehose_fwd.h,v $
 *     $Date: 2008/09/10 01:56:58 $
 * $Revision: 1.2 $
 * Description: Configuration of firehose code to fit portals-conduit
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _PORTAL_FIREHOSE_FWD_H
#define _PORTAL_FIREHOSE_FWD_H

#include <portals/portals3.h>

/* Set this here because we need it to match */
#define FH_BUCKET_SIZE	GASNET_PAGESIZE

/* portals-conduit uses firehose-region */
#define FIREHOSE_REGION

/* portals-conduit allows completion callbacks to run in handlers */
#define FIREHOSE_COMPLETION_IN_HANDLER

/* portals-conduit has a client_t */
#define FIREHOSE_CLIENT_T
typedef ptl_handle_md_t firehose_client_t;

/*
 * This type must be defined, even if it is not used by the client.
 */
typedef int firehose_remotecallback_args_t;

#define FIREHOSE_REMOTE_CALLBACK_IN_HANDLER

#endif
