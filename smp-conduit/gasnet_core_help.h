/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/gasnet_core_help.h,v $
 *     $Date: 2005/05/02 07:00:24 $
 * $Revision: 1.5 $
 * Description: GASNet smp conduit core Header Helpers (Internal code, not for client use)
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

#define GASNETC_MAX_ARGS   16
#define GASNETC_MAX_MEDIUM 65536   /* limited only by bufferring constraints */
#define GASNETC_MAX_LONG   ((size_t)0x7fffffff) /* unlimited */

END_EXTERNC

#endif
