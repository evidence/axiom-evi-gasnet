/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/Attic/gasnet_extended.c,v $
 *     $Date: 2010/03/08 07:38:26 $
 * $Revision: 1.6 $
 * Description: GASNet Extended API for smp-conduit
 * Copyright 2009, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_core_internal.h>

#ifndef _IN_GASNET_EXTENDED_C
#define _IN_GASNET_EXTENDED_C
#else
#error "#include loop detected"
#endif

/* ------------------------------------------------------------------------------------ */
#if GASNET_PSHM
/*
  Conduit-specifc Barrier "interface":
  ===================================
*/

extern void gasnete_pshmbarrier_init(gasnete_coll_team_t team);

#define GASNETE_BARRIER_DEFAULT "PSHM"
#define GASNETE_BARRIER_READENV() do { \
        if(GASNETE_ISBARRIER("PSHM")) gasnete_coll_default_barrier_type = GASNETE_COLL_BARRIER_PSHM; \
    } while (0)

#define GASNETE_BARRIER_INIT(TEAM, BARRIER_TYPE) do {       \
    if ((BARRIER_TYPE) == GASNETE_COLL_BARRIER_PSHM &&      \
        (TEAM) == GASNET_TEAM_ALL) {                        \
      gasnete_pshmbarrier_init(TEAM);                       \
    }                                                       \
  } while (0)

#endif /* GASNET_PSHM */
/* ------------------------------------------------------------------------------------ */

/* pull in the reference extended w/o any changes */
#include "extended-ref/gasnet_extended.c"

/* ------------------------------------------------------------------------------------ */
