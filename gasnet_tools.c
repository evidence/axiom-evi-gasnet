/*  $Archive:: /Ti/GASNet/gasnet_internal.c                               $
 *     $Date: 2002/06/01 14:24:57 $
 * $Revision: 1.1 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include <gasnet.h>
#include <gasnet_internal.h>

/* set to non-zero for verbose error reporting */
int gasneti_VerboseErrors = 1;

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  gasnet_hsl_t gasneti_atomicop_lock = GASNET_HSL_INITIALIZER;
#endif

#define GASNET_VERSION_STR  _STRINGIFY(GASNET_VERSION)
extern const char gasneti_IdentString_APIVersion[];
const char gasneti_IdentString_APIVersion[] = "$GASNetAPIVersion: " GASNET_VERSION_STR " $";

#define GASNET_CONFIG_STR _STRINGIFY(GASNET_CONFIG)
extern const char gasneti_IdentString_Config[];
const char gasneti_IdentString_Config[] = "$GASNetConfig: GASNET_" GASNET_CONFIG_STR " $";

/* ------------------------------------------------------------------------------------ */
extern void gasneti_fatalerror(char *msg, ...) {
  static va_list argptr;
  static char expandedmsg[255];

  va_start(argptr, msg); /*  pass in last argument */
  sprintf(expandedmsg, "*** GASNet FATAL ERROR: %s\n", msg);
  vfprintf(stderr, expandedmsg, argptr);
  fflush(stderr);
  va_end(argptr);

  abort();
}
/* ------------------------------------------------------------------------------------ */
extern int64_t gasneti_getMicrosecondTimeStamp(void) {
    int64_t retval;
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
	perror("gettimeofday");
	abort();
    }
    retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
    return retval;
}
/* ------------------------------------------------------------------------------------ */
/* build a code-location string (used by gasnete_current_loc) */
char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum) {
  int sz;
  char *loc;
  int fnlen;
  if (!funcname) funcname = "";
  if (!filename) filename = "*unknown file*";
  fnlen = strlen(funcname);
  sz = fnlen + strlen(filename) + 20;
  loc = gasneti_malloc(sz);
  if (*funcname)
    sprintf(loc,"%s%s at %s:%i",
           funcname,
           (fnlen && funcname[fnlen-1] != ')'?"()":""),
           filename, linenum);
  else
    sprintf(loc,"%s:%i", filename, linenum);
  return loc;
}
/* ------------------------------------------------------------------------------------ */
size_t gasneti_getSystemPageSize() {
  #ifdef CRAYT3E
    /* on Cray: shmemalign allocates mem aligned across nodes, 
        but there seems to be no fixed page size (man pagesize)
        this is probably because they don't support VM
       actual page size is set separately for each linker section, 
        ranging from 512KB(default) to 8MB
       Here we return 1 to reflect the lack of page alignment constraints
   */
    return 1;
  #else
    size_t pagesz = getpagesize();
    assert(pagesz > 0);
    return pagesz;
  #endif
}
/* ------------------------------------------------------------------------------------ */
