/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/testspawn/testspawn.c,v $
 *     $Date: 2005/01/18 23:26:23 $
 * $Revision: 1.2 $
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
  gasnet_node_t *array1, *array2;
  gasnet_node_t i, j;

  gasneti_bootstrapInit(&argc, &argv, &gasnetc_nodes, &gasnetc_mynode);
  gasneti_bootstrapBarrier();
  array1 = gasneti_calloc(gasnetc_nodes, sizeof(gasnet_node_t));
  array2 = gasneti_calloc(gasnetc_nodes, sizeof(gasnet_node_t));

  printf("Hello from node %d of %d\n", (int)gasnetc_mynode, (int)gasnetc_nodes);
  fflush(stdout);

  /* Test bootstrapBroadcast */
  gasneti_bootstrapBarrier();
  if (!gasnetc_mynode) {
    printf("Testing bootstrapBroadcast()...\n");
    fflush(stdout);
  }
  for (i=0; i<gasnetc_nodes; ++i) {
    gasneti_bootstrapBroadcast(&gasnetc_mynode, sizeof(gasnet_node_t), &j, i);
    if (j != i) {
      gasneti_fatalerror("gasneti_bootstrapBroadcast(root=%d) failed", (int)i);
    }
  }
  gasneti_bootstrapBarrier();
  if (!gasnetc_mynode) {
    printf("bootstrapBroadcast() OK\n");
    fflush(stdout);
  }

  /* Test bootstrapExchange */
  gasneti_bootstrapBarrier();
  if (!gasnetc_mynode) {
    printf("Testing bootstrapExchange()...\n");
    fflush(stdout);
  }
  gasneti_bootstrapExchange(&gasnetc_mynode, sizeof(gasnet_node_t), array1);
  for (i=0; i<gasnetc_nodes; ++i) {
    if (array1[i] != i) {
      gasneti_fatalerror("gasneti_bootstrapExchange failed");
    }
  }
  gasneti_bootstrapBarrier();
  if (!gasnetc_mynode) {
    printf("bootstrapExchange() OK\n");
    fflush(stdout);
  }
 
  /* Test bootstrapAlltoall */
  gasneti_bootstrapBarrier();
  if (!gasnetc_mynode) {
    printf("Testing bootstrapAlltoall()...\n");
    fflush(stdout);
  }
  for (i=0; i<gasnetc_nodes; ++i) {
    array1[i] = i + 100*gasnetc_mynode;
  }
  gasneti_bootstrapAlltoall(array1, sizeof(gasnet_node_t), array2);
  for (i=0; i<gasnetc_nodes; ++i) {
    if (array2[i] != gasnetc_mynode + 100*i) {
      gasneti_fatalerror("gasneti_bootstrapAlltoall() failed");
    }
  }
  gasneti_bootstrapBarrier();
  if (!gasnetc_mynode) {
    printf("bootstrapAlltoall() OK\n");
    fflush(stdout);
  }

  gasneti_bootstrapBarrier();
  gasneti_bootstrapFini();

  return 0;
}
