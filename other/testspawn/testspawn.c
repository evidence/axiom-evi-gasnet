/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/testspawn/testspawn.c,v $
 *     $Date: 2005/01/18 20:49:43 $
 * $Revision: 1.1 $
 * Description: 
 * Copyright 2005, Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <stdio.h>

#include <gasnet.h>
#include <gasnet_internal.h>

gasnet_node_t gasnetc_nodes = (gasnet_node_t)(-1);
gasnet_node_t gasnetc_mynode = (gasnet_node_t)(-1);

int main(int argc, const char **argv) {

  gasneti_bootstrapInit(&argc, &argv, &gasnetc_nodes, &gasnetc_mynode);
  gasneti_bootstrapBarrier();

  printf("Hello from node %d of %d\n", (int)gasnetc_mynode, (int)gasnetc_nodes);

  /* XXX: should test the bootstrap collectives here */

  gasneti_bootstrapBarrier();
  gasneti_bootstrapFini();

  return 0;
}
