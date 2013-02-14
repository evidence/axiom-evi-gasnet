#include <gasnet_gemini.h>

#ifndef MPI_SUCCESS
#define MPI_SUCCESS 0
#endif

uint32_t *gasnetc_gather_nic_addresses(void)
{
  gni_return_t status;
  uint32_t myaddress, pmiaddress;
  uint32_t cpu_id;
  uint32_t device_id;
  uint32_t *result = gasneti_malloc(gasneti_nodes * sizeof(uint32_t));

  device_id = gasneti_getenv_int_withdefault("PMI_GNI_DEV_ID", -1, 0);
  status = GNI_CdmGetNicAddress(device_id, &myaddress, &cpu_id);
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort();
  }
  pmiaddress = gasneti_getenv_int_withdefault("PMI_GNI_LOC_ADDR", -1, 0);
  if (pmiaddress != myaddress) {
#if GASNETC_DEBUG
    fprintf(stderr, "rank %d PMI_GNI_LOC_ADDR is %d, using it\n", gasneti_mynode, pmiaddress);
#endif
    myaddress = pmiaddress;
  }

  gasnetc_bootstrapExchange(&myaddress, sizeof(uint32_t), result);
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
