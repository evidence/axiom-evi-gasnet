/*  $Archive:: /Ti/AMMPI/ammpi_spmd.h                                     $
 *     $Date: 2002/06/16 09:19:26 $
 * $Revision: 1.2 $
 * Description: AMMPI Header for SPMD interface
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef __AMMPI_SPMD_H
#define __AMMPI_SPMD_H

#include <ammpi.h>

BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */
/* AMMPI SPMD Entry Points */

typedef int (*spawnfn_t)(int nproc, int argc, char **argv);
/* return non-zero if successful */


extern int AMMPI_SPMDStartup(int *argc, char ***argv,
                             int networkdepth, 
                             uint64_t *networkpid,
                             eb_t *eb, ep_t *ep); 
  /* should be always be called by program to initialize parallel job before parsing command line arguments
   *  must be called collectively by all worker processes
   * On worker processors, this call will modify the argc/argv params, call AM_Init,
   * and then return a bundle and endpoint properly configured for use with the SPMD job 
   * (translation table will be filled in, AM_SetExpectedResources called, and each worker given a unique tag)
   * worker processors should setup handler table for the endpoint and call SPMDBarrier before starting communication
   * Arguments:
   *  argc/argv - should be the unchanged ones passed to main, may return changed
   *  networkdepth - desired network depth hint (0 for default) (ignored on workers)
   *  networkpid - returns a globally unique pid for this job which is identical on all works (can be NULL for don't care) (ignored on master)
   *  eb, ep - variables to receive newly allocated bundle and endpoint on workers (ignored on master)
   */
extern int AMMPI_SPMDExit(int exitcode); 
  /* terminate the parallel job with given exit code (also handles AM_Terminate)
   */

extern int AMMPI_SPMDIsWorker(char **argv); 
  /* given the initial command line arguments, determine whether this process is a 
   * worker process created by the AMMPI SPMD job startup API
   */
extern int AMMPI_SPMDNumProcs(); /* return the number of processors in the parallel job */
extern int AMMPI_SPMDMyProc();   /* return a zero-based unique identifier of this processor in the parallel job */

extern int AMMPI_SPMDBarrier(); 
/* block until all SPMD processors call this function, 
 * and poll the SPMD endpoint while waiting
 * a slow, but functional barrier that is advisable to call after setting up handlers
 * but before making transport calls, to prevent returned messages due to races 
 */

END_EXTERNC

#endif
