/*  $Archive:: /Ti/AMUDP/amudp_spmd.h                                     $
 *     $Date: 2003/12/17 10:12:24 $
 * $Revision: 1.2 $
 * Description: AMUDP Header for SPMD interface
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef __AMUDP_SPMD_H
#define __AMUDP_SPMD_H

#include <amudp.h>

BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */
/* AMUDP SPMD Entry Points */

typedef int (*amudp_spawnfn_t)(int nproc, int argc, char **argv);
/* return non-zero if successful */


extern int AMUDP_SPMDStartup(int *argc, char ***argv,
                             int nproc, int networkdepth, 
                             amudp_spawnfn_t spawnfn,
                             uint64_t *networkpid,
                             eb_t *eb, ep_t *ep); 
  /* should be always be called by program to initialize parallel job before parsing command line arguments
   * On master processor, this call will never return if successful. 
   * Instead, it will block for the duration of the job and behave as the console
   * It will call the spawnfn with the proper argc/argv to be used in spawning the worker threads
   * On worker processors, this call will modify the argc/argv params, call AM_Init,
   * and then return a bundle and endpoint properly configured for use with the SPMD job 
   * (translation table will be filled in, AM_SetExpectedResources called, and each worker given a unique tag)
   * worker processors should setup handler table for the endpoint and call SPMDBarrier before starting communication
   * Arguments:
   *  argc/argv - should be the unchanged ones passed to main, may return changed
   *  nproc - the number of SPMD worker processors. 0 to read from arguments (ignored on workers)
   *  networkdepth - desired network depth hint (0 for default) (ignored on workers)
   *  spawnfn - callback function to be used in spawning workers from the master  (ignored on workers)
   *    this function may do arbitrary work, but the end result must be that nproc worker processes 
   *    are created somewhere on the accessible network, which then subsequently call AMUDP_SPMDStartup with the given args
   *    the worker processes created MUST be all be in different address spaces (i.e. can't just create a thread)
   *    may be one of special functions provided below (e.g. AMUDP_SPMDLocalSpawn)
   *    passing NULL will give the user a choice amongst the library-provided spawn functions
   *  networkpid - returns a globally unique pid for this job which is identical on all works (can be NULL for don't care) (ignored on master)
   *  eb, ep - variables to receive newly allocated bundle and endpoint on workers (ignored on master)
   */
extern int AMUDP_SPMDExit(int exitcode); 
  /* terminate the parallel job with given exit code (also handles AM_Terminate)
   */

extern int AMUDP_SPMDSetExitCallback(void (*fp)(int));
  /* register a function to be called when AMUDP_SPMDExit is called by any node
   * exit code is passed
   */
extern void (*AMUDP_SPMDkillmyprocess)(int);
  /* function used to finally kill the process (_exit by default) */

extern int AMUDP_SPMDIsWorker(char **argv); 
  /* given the initial command line arguments, determine whether this process is a 
   * worker process created by the AMUDP SPMD job startup API
   */
extern int AMUDP_SPMDNumProcs(); /* return the number of processors in the parallel job */
extern int AMUDP_SPMDMyProc();   /* return a zero-based unique identifier of this processor in the parallel job */

extern int AMUDP_SPMDBarrier(); 
/* block until all SPMD processors call this function, 
 * and poll the SPMD endpoint while waiting
 * a slow, but functional barrier that is advisable to call after setting up handlers
 * but before making transport calls, to prevent returned messages due to races 
 */

extern const char* AMUDP_SPMDgetenvMaster(const char *keyname); 
/* This function is the global analog of the C library function getenv()
 * It reads variables from the environment of the SPMD master as it appeared at spawn time 
 * returns NULL if the given keyname did not appear in the environment,
 * or if AMUDP_SPMDStartup() hasn't been successfully called yet
 * the string returned should never be modified by the caller
 */

extern int AMUDP_SPMDAllGather(void *source, void *dest, size_t len);
/* AMUDP_SPMDAllGather: gather len bytes from source buf on each node, concatenate them and write 
   them into the dest buffer (which must have length len*numnodes) in rank order
 */

/* ------------------------------------------------------------------------------------ */
/* AMUDP SPMD Spawning functions
 * some useful library-provided spawning functions, 
 * which can be passed to AMUDP_Startup
 * (should never be called directly)
 * you can always write your own to suit your environment, of course
 * (see amudp_spawn.c for several examples)
 */
extern int AMUDP_SPMDLocalSpawn(int nproc, int argc, char **argv); /* fork/exec the worker processes on this machine */
extern int AMUDP_SPMDSshSpawn(int nproc, int argc, char **argv);   /* call ssh remote shell */
extern int AMUDP_SPMDRexecSpawn(int nproc, int argc, char **argv); /* call rexec spawning tool */
extern int AMUDP_SPMDGlunixSpawn(int nproc, int argc, char **argv); /* requires -DGLUNIX when compiling library */
extern int AMUDP_SPMDCustomSpawn(int nproc, int argc, char **argv);

typedef struct {
  char abbrev;
  const char *desc;
  amudp_spawnfn_t fnptr;
} amudp_spawnfn_desc_t;

extern amudp_spawnfn_desc_t const AMUDP_Spawnfn_Desc[];
/* null-terminated array of spawn functions available, with descriptions */

END_EXTERNC

#endif
