/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testcoll.c,v $
 *     $Date: 2005/03/25 03:31:10 $
 * $Revision: 1.16 $
 * Description: GASNet collectives test
 * Copyright 2002-2004, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include "gasnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include "test.h"

#define DEFAULT_SZ	(32*1024)

#define PRINT_LATENCY 0
#define PRINT_THROUGHPUT 1

int myproc;
int numprocs;
int peerproc;

int *segment;

static int *A, *B, *C, *D, *E, *F, *G;
static int **Av, **Bv, **Cv, **Dv, **Ev, **Fv, **Gv;

typedef struct {
	int datasize;
	int iters;
	uint64_t time;
} stat_struct_t;


#define init_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _init_stat
#define update_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _update_stat
#define print_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _print_stat

void _init_stat(stat_struct_t *st, int sz)
{
	st->iters = 0;
	st->datasize = sz;
	st->time = 0;
}

void _update_stat(stat_struct_t *st, uint64_t temptime, int iters)
{
	st->iters += iters;
	st->time += temptime;
} 

void _print_stat(int myproc, stat_struct_t *st, const char *name, int operation)
{
	switch (operation) {
	case PRINT_LATENCY:
		printf("Proc %2i - %4i byte : %7i iters,"
			   " latency %10i us total, %9.3f us ave. (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_THROUGHPUT:
		printf("Proc %2i - %4i byte : %7i iters,"
#if 0
			" throughput %9.3f KB/sec (%s)\n"
#else
			" inv. throughput %9.3f us (%s)\n"
#endif
                        ,
			myproc, st->datasize, st->iters,
#if 0
			((int)st->time == 0 ? 0.0 :
                        (1000000.0 * st->datasize * st->iters / 1024.0) / ((int)st->time)),
#else
                        (((float)((int)st->time)) / st->iters),
#endif
			name);
		fflush(stdout);
		break;
	default:
		break;
	}
}

/*
 * Test NO/NO - in/out data is not generated/consumed in same barrier phase
 */
void test_NONO(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/single-addr NO/NO";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*A = (myproc == root) ? r : -1;
	*B = myproc;
	for (i = 0; i < numprocs; ++i) {
	    D[i] = i * r + myproc;
	}

	BARRIER();

	gasnet_coll_broadcast(GASNET_TEAM_ALL, A, root, A, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gather(GASNET_TEAM_ALL, root, C, B, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_scatter(GASNET_TEAM_ALL, E, root, D, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gather_all(GASNET_TEAM_ALL, F, B, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_exchange(GASNET_TEAM_ALL, G, D, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);

	BARRIER();

	if (r != *A) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (C[i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
	    }
	}
	if (*E != myproc*r + root) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	for (i = 0; i < numprocs; ++i) {
	    if (F[i] != i) {
		MSG("ERROR: %s gather_all validation failed", name);
	    }
	}
	for (i = 0; i < numprocs; ++i) {
	    if (G[i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test MY/MY - in/out data is generated/consumed locally in same barrier phase
 */
void test_MYMY(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/single-addr MY/MY";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*A = (myproc == root) ? r : -1;
	*B = myproc;

	gasnet_coll_broadcast(GASNET_TEAM_ALL, A, root, A, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (r != *A) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gather(GASNET_TEAM_ALL, root, C, B, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (C[i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
		C[i] *= r;
	    }
	}
	gasnet_coll_scatter(GASNET_TEAM_ALL, B, root, C, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (*B != myproc*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gather_all(GASNET_TEAM_ALL, C, B, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (C[i] != i*r) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	    C[i] += myproc;
	}
	gasnet_coll_exchange(GASNET_TEAM_ALL, D, C, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (D[i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test ALL/ALL - in/out data is generated/consumed remotely in same barrier phase
 */
void test_ALLALL(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/single-addr ALL/ALL";
    int j;
    int tmp;
    gasnet_node_t peer;

    MSG0("Starting %s test", name);

    peer = ((myproc ^ 1) == numprocs) ? myproc : (myproc ^ 1);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	tmp = (peer == root) ? r : -1;
	gasnet_put(peer, A, &tmp, sizeof(int));

	gasnet_coll_broadcast(GASNET_TEAM_ALL, A, root, A, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, A, sizeof(int));
	if (tmp != r) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	tmp = peer;
	gasnet_put(peer, B, &tmp, sizeof(int));
	gasnet_coll_gather(GASNET_TEAM_ALL, root, C, B, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(D, root, C, numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (D[i] != i) {
		MSG("ERROR: %s gather validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on D */
	tmp = myproc * r;
	gasnet_put(root, D+myproc, &tmp, sizeof(int));
	gasnet_coll_scatter(GASNET_TEAM_ALL, B, root, D, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, B, sizeof(int));
	if (tmp != peer*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	BARRIER(); /* to avoid conflict on B */
	tmp = peer*r - 1;
	gasnet_put(peer, B, &tmp, sizeof(int));
	gasnet_coll_gather_all(GASNET_TEAM_ALL, C, B, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(D, peer, C, numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (D[i] != i*r - 1) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on C & D */
	for (i = 0; i < numprocs; ++i) {
	    C[i] += peer;
	}
	gasnet_put(peer, D, C, numprocs*sizeof(int));
	gasnet_coll_exchange(GASNET_TEAM_ALL, C, D, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(D, peer, C, numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (D[i] != i + peer*r - 1) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void test_NB(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/single-addr NB";
    int j;
    int *X = test_malloc(iters*sizeof(int));
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	X[j] = random();
	A[j] = (myproc == root) ? X[j] : 0;
	h[j] = gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, A+j, root, A+j, sizeof(int),
				      		GASNET_COLL_SINGLE |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (A[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test NO/NO - in/out data is not generated/consumed in same barrier phase
 * Unaligned/multi-addr variant
 */
void testM_NONO(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/multi-addr NO/NO";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*Av[myproc] = (myproc == root) ? r : -1;
	*Bv[myproc] = myproc;
	for (i = 0; i < numprocs; ++i) {
	    Dv[myproc][i] = i * r + myproc;
	}

	BARRIER();

	gasnet_coll_broadcastM(GASNET_TEAM_ALL, (void**)Av, root, Av[root], sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gatherM(GASNET_TEAM_ALL, root, Cv[root], (void**)Bv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_scatterM(GASNET_TEAM_ALL, (void**)Ev, root, Dv[root], sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gather_allM(GASNET_TEAM_ALL, (void**)Fv, (void**)Bv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_exchangeM(GASNET_TEAM_ALL, (void**)Gv, (void**)Dv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);

	BARRIER();

	if (r != *Av[myproc]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (Cv[myproc][i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
	    }
	}
	if (*Ev[myproc] != myproc*r + root) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	for (i = 0; i < numprocs; ++i) {
	    if (Fv[myproc][i] != i) {
		MSG("ERROR: %s gather_all validation failed", name);
	    }
	}
	for (i = 0; i < numprocs; ++i) {
	    if (Gv[myproc][i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test MY/MY - in/out data is generated/consumed locally in same barrier phase
 * SINGLE/multi-addr variant
 */
void testM_MYMY(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/multi-addr MY/MY";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*Av[myproc] = (myproc == root) ? r : -1;
	*Bv[myproc] = myproc;

	gasnet_coll_broadcastM(GASNET_TEAM_ALL, (void**)Av, root, Av[root], sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (r != *Av[myproc]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gatherM(GASNET_TEAM_ALL, root, Cv[root], (void**)Bv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (Cv[myproc][i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
		Cv[myproc][i] *= r;
	    }
	}
	gasnet_coll_scatterM(GASNET_TEAM_ALL, (void**)Bv, root, Cv[root], sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (*Bv[myproc] != myproc*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gather_allM(GASNET_TEAM_ALL, (void**)Cv, (void**)Bv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (Cv[myproc][i] != i*r) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	    Cv[myproc][i] += myproc;
	}
	gasnet_coll_exchangeM(GASNET_TEAM_ALL, (void**)Dv, (void**)Cv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test ALL/ALL - in/out data is generated/consumed remotely in same barrier phase
 * SINGLE/multi-addr variant
 */
void testM_ALLALL(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/multi-addr ALL/ALL";
    int j;
    int tmp;
    gasnet_node_t peer;

    MSG0("Starting %s test", name);

    peer = ((myproc ^ 1) == numprocs) ? myproc : (myproc ^ 1);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	tmp = (peer == root) ? r : -1;
	gasnet_put(peer, Av[peer], &tmp, sizeof(int));

	gasnet_coll_broadcastM(GASNET_TEAM_ALL, (void**)Av, root, Av[root], sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, Av[peer], sizeof(int));
	if (tmp != r) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	tmp = peer;
	gasnet_put(peer, Bv[peer], &tmp, sizeof(int));
	gasnet_coll_gatherM(GASNET_TEAM_ALL, root, Cv[root], (void**)Bv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], root, Cv[root], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i) {
		MSG("ERROR: %s gather validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on D */
	tmp = myproc * r;
	gasnet_put(root, Dv[root]+myproc, &tmp, sizeof(int));
	gasnet_coll_scatterM(GASNET_TEAM_ALL, (void**)Bv, root, Dv[root], sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, Bv[peer], sizeof(int));
	if (tmp != peer*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	BARRIER(); /* to avoid conflict on B */
	tmp = peer*r - 1;
	gasnet_put(peer, Bv[peer], &tmp, sizeof(int));
	gasnet_coll_gather_allM(GASNET_TEAM_ALL, (void**)Cv, (void**)Bv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], peer, Cv[peer], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i*r - 1) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on C & D */
	for (i = 0; i < numprocs; ++i) {
	    Cv[myproc][i] += peer;
	}
	gasnet_put(peer, Dv[peer], Cv[myproc], numprocs*sizeof(int));
	gasnet_coll_exchangeM(GASNET_TEAM_ALL, (void**)Cv, (void**)Dv, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], peer, Cv[peer], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i + peer*r - 1) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void testM_NB(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/multi-addr NB";
    int i, j;
    int **Z = test_malloc(iters * numprocs * sizeof(int *));
    int *X = test_malloc(iters*sizeof(int));
    int *Y = (int *)TEST_MYSEG() + myproc;
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    for (i = 0; i < iters; ++i) {
      int **p = Z + i*numprocs;
      for (j = 0; j < numprocs; ++j) {
	p[j] = (int *)TEST_SEG(j) + j + i;
      }
    }

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
        int **p = Z + j*numprocs;
	X[j] = random();
	Y[j] = (myproc == root) ? X[j] : 0;
	assert(&(Y[j]) == p[myproc]);
	h[j] = gasnet_coll_broadcastM_nb(GASNET_TEAM_ALL, (void**)p, root, p[root], sizeof(int),
				      		GASNET_COLL_SINGLE |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (Y[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(Z);
    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test NO/NO - in/out data is not generated/consumed in same barrier phase
 * LOCAL/single-addr variant
 */
void testL_NONO(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/single-addr NO/NO";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*Av[myproc] = (myproc == root) ? r : -1;
	*Bv[myproc] = myproc;
	for (i = 0; i < numprocs; ++i) {
	    Dv[myproc][i] = i * r + myproc;
	}

	BARRIER();

	gasnet_coll_broadcast(GASNET_TEAM_ALL, Av[myproc], root, (myproc == root) ? Av[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gather(GASNET_TEAM_ALL, root, (myproc == root) ? Cv[root] : NULL, Bv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_scatter(GASNET_TEAM_ALL, Ev[myproc], root, (myproc == root) ? Dv[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gather_all(GASNET_TEAM_ALL, Fv[myproc], Bv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_exchange(GASNET_TEAM_ALL, Gv[myproc], Dv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);

	BARRIER();

	if (r != *Av[myproc]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (Cv[myproc][i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
	    }
	}
	if (*Ev[myproc] != myproc*r + root) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	for (i = 0; i < numprocs; ++i) {
	    if (Fv[myproc][i] != i) {
		MSG("ERROR: %s gather_all validation failed", name);
	    }
	}
	for (i = 0; i < numprocs; ++i) {
	    if (Gv[myproc][i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test MY/MY - in/out data is generated/consumed locally in same barrier phase
 * LOCAL/single-addr variant
 */
void testL_MYMY(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/single-addr MY/MY";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*Av[myproc] = (myproc == root) ? r : -1;
	*Bv[myproc] = myproc;

	gasnet_coll_broadcast(GASNET_TEAM_ALL, Av[myproc], root, (myproc == root) ? Av[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (r != *Av[myproc]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gather(GASNET_TEAM_ALL, root, (myproc == root) ? Cv[root] : NULL, Bv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (Cv[myproc][i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
		Cv[myproc][i] *= r;
	    }
	}
	gasnet_coll_scatter(GASNET_TEAM_ALL, Bv[myproc], root, (myproc == root) ? Cv[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (*Bv[myproc] != myproc*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gather_all(GASNET_TEAM_ALL, Cv[myproc], Bv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (Cv[myproc][i] != i*r) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	    Cv[myproc][i] += myproc;
	}
	gasnet_coll_exchange(GASNET_TEAM_ALL, Dv[myproc], Cv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test ALL/ALL - in/out data is generated/consumed remotely in same barrier phase
 * LOCAL/single-addr variant
 */
void testL_ALLALL(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/single-addr ALL/ALL";
    int j;
    int tmp;
    gasnet_node_t peer;

    MSG0("Starting %s test", name);

    peer = ((myproc ^ 1) == numprocs) ? myproc : (myproc ^ 1);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	tmp = (peer == root) ? r : -1;
	gasnet_put(peer, Av[peer], &tmp, sizeof(int));

	gasnet_coll_broadcast(GASNET_TEAM_ALL, Av[myproc], root, (myproc == root) ? Av[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, Av[peer], sizeof(int));
	if (tmp != r) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	tmp = peer;
	gasnet_put(peer, Bv[peer], &tmp, sizeof(int));
	gasnet_coll_gather(GASNET_TEAM_ALL, root, (myproc == root) ? Cv[root] : NULL, Bv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], root, Cv[root], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i) {
		MSG("ERROR: %s gather validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on D */
	tmp = myproc * r;
	gasnet_put(root, Dv[root]+myproc, &tmp, sizeof(int));
	gasnet_coll_scatter(GASNET_TEAM_ALL, Bv[myproc], root, (myproc == root) ? Dv[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, Bv[peer], sizeof(int));
	if (tmp != peer*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	BARRIER(); /* to avoid conflict on B */
	tmp = peer*r - 1;
	gasnet_put(peer, Bv[peer], &tmp, sizeof(int));
	gasnet_coll_gather_all(GASNET_TEAM_ALL, Cv[myproc], Bv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], peer, Cv[peer], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i*r - 1) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on C & D */
	for (i = 0; i < numprocs; ++i) {
	    Cv[myproc][i] += peer;
	}
	gasnet_put(peer, Dv[peer], Cv[myproc], numprocs*sizeof(int));
	gasnet_coll_exchange(GASNET_TEAM_ALL, Cv[myproc], Dv[myproc], sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], peer, Cv[peer], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i + peer*r - 1) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void testL_NB(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/single-addr NB";
    int i, j;
    int *X = test_malloc(iters*sizeof(int));
    int *Y = (int *)TEST_MYSEG() + myproc;
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	X[j] = random();
	Y[j] = (myproc == root) ? X[j] : 0;
	h[j] = gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, Y+j, root, (myproc == root) ? Y+j : NULL, sizeof(int),
				      		GASNET_COLL_LOCAL |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (Y[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test NO/NO - in/out data is not generated/consumed in same barrier phase
 * LOCAL/multi-addr variant
 */
void testLM_NONO(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/multi-addr NO/NO";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*Av[myproc] = (myproc == root) ? r : -1;
	*Bv[myproc] = myproc;
	for (i = 0; i < numprocs; ++i) {
	    Dv[myproc][i] = i * r + myproc;
	}

	BARRIER();

	gasnet_coll_broadcastM(GASNET_TEAM_ALL, (void**)(Av+myproc), root, (myproc == root) ? Av[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gatherM(GASNET_TEAM_ALL, root, (myproc == root) ? Cv[root] : NULL, (void**)(Bv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_scatterM(GASNET_TEAM_ALL, (void**)(Ev+myproc), root, (myproc == root) ? Dv[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_gather_allM(GASNET_TEAM_ALL, (void**)(Fv+myproc), (void**)(Bv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_coll_exchangeM(GASNET_TEAM_ALL, (void**)(Gv+myproc), (void**)(Dv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);

	BARRIER();

	if (r != *Av[myproc]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (Cv[myproc][i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
	    }
	}
	if (*Ev[myproc] != myproc*r + root) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	for (i = 0; i < numprocs; ++i) {
	    if (Fv[myproc][i] != i) {
		MSG("ERROR: %s gather_all validation failed", name);
	    }
	}
	for (i = 0; i < numprocs; ++i) {
	    if (Gv[myproc][i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test MY/MY - in/out data is generated/consumed locally in same barrier phase
 * LOCAL/multi-addr variant
 */
void testLM_MYMY(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/multi-addr MY/MY";
    int j;

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	*Av[myproc] = (myproc == root) ? r : -1;
	*Bv[myproc] = myproc;

	gasnet_coll_broadcastM(GASNET_TEAM_ALL, (void**)(Av+myproc), root, (myproc == root) ? Av[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (r != *Av[myproc]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gatherM(GASNET_TEAM_ALL, root, (myproc == root) ? Cv[root] : NULL, (void**)(Bv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (Cv[myproc][i] != i) {
		    MSG("ERROR: %s gather validation failed", name);
		    gasnet_exit(1);
		}
		Cv[myproc][i] *= r;
	    }
	}
	gasnet_coll_scatterM(GASNET_TEAM_ALL, (void**)(Bv+myproc), root, (myproc == root) ? Cv[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	if (*Bv[myproc] != myproc*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	gasnet_coll_gather_allM(GASNET_TEAM_ALL, (void**)(Cv+myproc), (void**)(Bv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (Cv[myproc][i] != i*r) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	    Cv[myproc][i] += myproc;
	}
	gasnet_coll_exchangeM(GASNET_TEAM_ALL, (void**)(Dv+myproc), (void**)(Cv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i + myproc*r) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

/*
 * Test ALL/ALL - in/out data is generated/consumed remotely in same barrier phase
 * LOCAL/multi-addr variant
 */
void testLM_ALLALL(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/multi-addr ALL/ALL";
    int j;
    int tmp;
    gasnet_node_t peer;

    MSG0("Starting %s test", name);

    peer = ((myproc ^ 1) == numprocs) ? myproc : (myproc ^ 1);

    for (j = 0; j < iters; ++j) {
	gasnet_node_t i;
	int r = random();
      
	tmp = (peer == root) ? r : -1;
	gasnet_put(peer, Av[peer], &tmp, sizeof(int));

	gasnet_coll_broadcastM(GASNET_TEAM_ALL, (void**)(Av+myproc), root, (myproc == root) ? Av[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, Av[peer], sizeof(int));
	if (tmp != r) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
	tmp = peer;
	gasnet_put(peer, Bv[peer], &tmp, sizeof(int));
	gasnet_coll_gatherM(GASNET_TEAM_ALL, root, (myproc == root) ? Cv[root] : NULL, (void**)(Bv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], root, Cv[root], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i) {
		MSG("ERROR: %s gather validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on D */
	tmp = myproc * r;
	gasnet_put(root, Dv[root]+myproc, &tmp, sizeof(int));
	gasnet_coll_scatterM(GASNET_TEAM_ALL, (void**)(Bv+myproc), root, (myproc == root) ? Dv[root] : NULL, sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(&tmp, peer, Bv[peer], sizeof(int));
	if (tmp != peer*r) {
	    MSG("ERROR: %s scatter validation failed", name);
	    gasnet_exit(1);
	}
	BARRIER(); /* to avoid conflict on B */
	tmp = peer*r - 1;
	gasnet_put(peer, Bv[peer], &tmp, sizeof(int));
	gasnet_coll_gather_allM(GASNET_TEAM_ALL, (void**)(Cv+myproc), (void**)(Bv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], peer, Cv[peer], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i*r - 1) {
		MSG("ERROR: %s gather_all validation failed", name);
		gasnet_exit(1);
	    }
	}
	BARRIER(); /* to avoid conflict on C & D */
	for (i = 0; i < numprocs; ++i) {
	    Cv[myproc][i] += peer;
	}
	gasnet_put(peer, Dv[peer], Cv[myproc], numprocs*sizeof(int));
	gasnet_coll_exchangeM(GASNET_TEAM_ALL, (void**)(Cv+myproc), (void**)(Dv+myproc), sizeof(int),
				      GASNET_COLL_LOCAL |
				      GASNET_COLL_IN_ALLSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	gasnet_get(Dv[myproc], peer, Cv[peer], numprocs*sizeof(int));
	for (i = 0; i < numprocs; ++i) {
	    if (Dv[myproc][i] != i + peer*r - 1) {
		MSG("ERROR: %s exchange validation failed", name);
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void testLM_NB(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/multi-addr NB";
    int i, j;
    int **Z = test_malloc(iters*sizeof(int *));
    int *X = test_malloc(iters*sizeof(int));
    int *Y = (int *)TEST_MYSEG() + myproc;
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    for (i = 0; i < iters; ++i) {
      Z[i] = &Y[i];
    }

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	X[j] = random();
	Y[j] = (myproc == root) ? X[j] : 0;
	h[j] = gasnet_coll_broadcastM_nb(GASNET_TEAM_ALL, (void**)(Z+j), root, (myproc == root) ? Y+j : NULL, sizeof(int),
				      		GASNET_COLL_LOCAL |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (Y[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(Z);
    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

int main(int argc, char **argv)
{
    int arg;
    int iters = 0;
    gasnet_node_t i;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();

    if (!myproc)
	print_testname("testcoll", numprocs);

    if (argc > 1) {
      iters = atoi(argv[1]);
    }
    if (iters < 1) {
      iters = 1000;
    }
    
    if (myproc == 0) {
	printf("Running coll test(s) with %d iterations.\n", iters);
    }
    gasnet_coll_init(NULL, 0, NULL, 0, 0);

    segment = (int *) TEST_MYSEG();

    MSG("running.");
    BARRIER();

    srandom(1);

    Av = test_malloc(numprocs * sizeof(int *));
    Bv = test_malloc(numprocs * sizeof(int *));
    Cv = test_malloc(numprocs * sizeof(int *));
    Dv = test_malloc(numprocs * sizeof(int *));
    Ev = test_malloc(numprocs * sizeof(int *));
    Fv = test_malloc(numprocs * sizeof(int *));
    Gv = test_malloc(numprocs * sizeof(int *));

    /* Carve some variables out of the segment: */
    A = segment;	/* int [1] */
    B = A + 1;		/* int [1] */
    C = B + 1;		/* int [N] */
    D = C + numprocs;	/* int [N] */
    E = D + numprocs;	/* int [1] */
    F = E + 1;		/* int [N] */
    G = F + numprocs;	/* int [N] */

    /* The unaligned eqivalents as arrays of pointers: */
    /* Using (TEST_SEG(i) + i) yields unaligned even when the segments are aligned.
       This is to help catch any case where addresses might have been misused that
       might go undetected if the addresses were aligned */
    for (i = 0; i < numprocs; ++i) {
	Av[i] = (int *)TEST_SEG(i) + i;
	Bv[i] = Av[i] + 1;
	Cv[i] = Bv[i] + 1;
	Dv[i] = Cv[i] + numprocs;
	Ev[i] = Dv[i] + numprocs;
	Fv[i] = Ev[i] + 1;
	Gv[i] = Fv[i] + numprocs;
    }

    for (i = 0; i < numprocs; ++i) {
      MSG0("Running tests with root = %d", (int)i);

#if GASNET_ALIGNED_SEGMENTS == 1
      test_NONO(iters, i);
      test_MYMY(iters, i);
      test_ALLALL(iters, i);
      test_NB(iters, i);
#endif	/* Aligned segments */
      testM_NONO(iters, i);
      testM_MYMY(iters, i);
      testM_ALLALL(iters, i);
      testM_NB(iters, i);
      testL_NONO(iters, i);
      testL_MYMY(iters, i);
      testL_ALLALL(iters, i);
      testL_NB(iters, i);
      testLM_NONO(iters, i);
      testLM_MYMY(iters, i);
      testLM_ALLALL(iters, i);
      testLM_NB(iters, i);
    }

    BARRIER();

    test_free(Av);
    test_free(Bv);
    test_free(Cv);
    test_free(Dv);
    test_free(Ev);
    test_free(Fv);
    test_free(Gv);

    MSG("done.");

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */
