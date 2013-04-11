/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/elan-conduit/Attic/gasnet_core_help.h,v $
 *     $Date: 2013/04/11 19:26:06 $
 * $Revision: 1.1.1.1 $
 * Description: GASNet elan conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

GASNETI_BEGIN_EXTERNC

#include <gasnet_help.h>

#define GASNETI_EADDRFMT "0x%08x"
#define GASNETI_EADDRSTR(ptr) ((uint32_t)(uintptr_t)(ptr))

GASNETI_END_EXTERNC

#endif
