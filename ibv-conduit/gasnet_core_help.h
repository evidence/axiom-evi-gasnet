/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_help.h,v $
 *     $Date: 2005/02/12 11:29:43 $
 * $Revision: 1.9 $
 * Description: GASNet vapi conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

/* Don't yet have any mixed approaches in which there is a pinned
 * segment and firehose is used to dynamically register stack, etc.
 * Once we do, these symbols may change name or meaning.
 */
#if defined(GASNET_SEGMENT_LARGE) || defined(GASNET_SEGMENT_EVERYTHING)
  #define GASNETC_USE_FIREHOSE 1
#elif defined(GASNET_SEGMENT_FAST)
  #define GASNETC_PIN_SEGMENT 1
#endif

#ifndef GASNETC_USE_FIREHOSE
  #define GASNETC_USE_FIREHOSE 0
#endif
#ifndef GASNETC_PIN_SEGMENT
  #define GASNETC_PIN_SEGMENT 0
#endif

END_EXTERNC

#endif
