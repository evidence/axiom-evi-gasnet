/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/testspawn/gasnet_core_help.h,v $
 *     $Date: 2005/01/18 20:49:43 $
 * $Revision: 1.1 $
 * Description:
 * Copyright 2005, Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

extern gasnet_node_t gasnetc_mynode;
extern gasnet_node_t gasnetc_nodes;

END_EXTERNC

#endif
