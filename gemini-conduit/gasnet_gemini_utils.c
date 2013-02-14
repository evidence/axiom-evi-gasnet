#include <stdint.h>
#include <assert.h>
#include <gasnet_gemini.h>
#include <pmi_cray.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gni_pub.h>

#ifndef MPI_SUCCESS
#define MPI_SUCCESS 0
#endif

#define mygetenv(_name) \
  gasneti_getenv_int_withdefault(_name, -1, 0)

uint32_t *gasnetc_gather_nic_addresses(void)
{
  gni_return_t status;
  uint32_t myaddress, pmiaddress;
  uint32_t cpu_id;
  uint32_t device_id = gasnetc_GNIT_Device_Id();
  uint32_t *result = gasneti_malloc(gasneti_nodes * sizeof(uint32_t));

  status = GNI_CdmGetNicAddress(device_id, &myaddress, &cpu_id);
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort();
  }
  pmiaddress = mygetenv("PMI_GNI_LOC_ADDR");
  if (pmiaddress != myaddress) {
#if GASNETC_DEBUG
    fprintf(stderr, "rank %d PMI_GNI_LOC_ADDR is %d, using it\n", gasneti_mynode, pmiaddress);
#endif
    myaddress = pmiaddress;
  }

  gasnetc_GNIT_Allgather(&myaddress, sizeof(uint32_t), result);
  if (result[gasneti_mynode] != myaddress) {
    fprintf(stderr, "rank %d gathernic got %x should be %x\n", gasneti_mynode,
	    result[gasneti_mynode], myaddress);
    gasnetc_GNIT_Abort();
  }

  return(result);
}


int gasnetc_gem_init(char **errorstringp)
{
   int size, rank;
   int spawned;
   int appnum;
   int ret;

   if ((ret = PMI2_Init (&spawned, &size, &rank, &appnum)) != MPI_SUCCESS) {
     *errorstringp = (char *) "Failure in PMI2_Init\n";
     return(GASNET_ERR_NOT_INIT);
   }
   gasneti_nodes = size;
   gasneti_mynode = rank;
   return(GASNET_OK);
}


char gasnetc_GNIT_Ptag(void)
{
  return(mygetenv("PMI_GNI_PTAG"));
}

int gasnetc_GNIT_Cookie(void)
{
  return(mygetenv("PMI_GNI_COOKIE"));
}

int gasnetc_GNIT_Device_Id(void)
{
  return(mygetenv("PMI_GNI_DEV_ID"));
}

void gasnetc_GNIT_Allgather(void *local, long length, void *global)
{
  /* work in chunks of same size as the gasnet_node_t */
  gasnet_node_t itembytes = sizeof(gasnet_node_t) + GASNETI_ALIGNUP(length, sizeof(gasnet_node_t));
  gasnet_node_t itemwords = itembytes / sizeof(gasnet_node_t);
  gasnet_node_t *unsorted = gasneti_malloc(itembytes * gasneti_nodes);
  gasnet_node_t *temporary;

#if GASNET_DEBUG
  char *found = gasneti_calloc(gasneti_nodes, 1);
#endif
  int i, status;

  /* perform unsorted Allgather of records with prepended node number */
  temporary = gasneti_malloc(itembytes);
  temporary[0] = gasneti_mynode;   memcpy(&temporary[1], local, length);  
  status = PMI_Allgather(temporary, unsorted, itembytes);
  gasneti_free(temporary);
  if (status != PMI_SUCCESS) {
    fprintf(stderr, "rank %d: PMI_Allgather failed\n", gasneti_mynode);
    gasnetc_GNIT_Abort();
  }

  /* extract the records from the unsorted array by using the prepended node numbers */
  for (i = 0; i < gasneti_nodes; i += 1) {
    gasnet_node_t peer = unsorted[i * itemwords];
    if (peer >= gasneti_nodes) {
      fprintf(stderr, "rank %d PMI_Allgather failed, item %d is %d\n", 
	      gasneti_mynode, i, peer);
      gasnetc_GNIT_Abort();
    }
    memcpy((void *) ((uintptr_t) global + (peer * length)),
           &unsorted[(i * itemwords) + 1],
           length);
#if GASNET_DEBUG
    ++found[peer];
#endif
  }

#if GASNET_DEBUG
  /* verify exactly-once */
  for (i = 0; i < gasneti_nodes; i += 1) {
    if (!found[i]) {
      fprintf(stderr, "rank %d Allgather rank %d missing\n", gasneti_mynode, i);
      gasnetc_GNIT_Abort();
    }
  }
  gasneti_free(found);
  /* check own data */
  if (memcmp(local, (void *) ((uintptr_t ) global + (gasneti_mynode * length)), length) != 0) {
    fprintf(stderr, "rank %d, allgather error\n", gasneti_mynode);
    gasnetc_GNIT_Abort();
  }
#endif

  gasneti_free(unsorted);
}


void gasnetc_GNIT_Finalize(void)
{
  PMI2_Finalize();
}


void gasnetc_GNIT_Barrier(void)
{
  PMI_Barrier();
}
