/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testcoll.c,v $
 *     $Date: 2005/03/11 19:15:55 $
 * $Revision: 1.14 $
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
    int j;
    int *A = segment;		/* int [1] */
    int *B = A + 1;		/* int [1] */
    int *C = B + 1;		/* int [N] */
    int *D = C + numprocs;	/* int [N] */
    int *E = D + numprocs;	/* int [1] */
    int *F = E + 1;		/* int [N] */
    int *G = F + numprocs;	/* int [N] */

    if (myproc == 0)
	MSG("Starting NO/NO test");

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
	    MSG("NO/NO: broadcast validation failed");
	    gasnet_exit(1);
	}
	if (myproc == root) {
	    for (i = 0; i < numprocs; ++i) {
		if (C[i] != i) {
		    MSG("NO/NO: gather validation failed");
		    gasnet_exit(1);
		}
	    }
	}
	if (*E != myproc*r + root) {
	    MSG("NO/NO: scatter validation failed");
	    gasnet_exit(1);
	}
	for (i = 0; i < numprocs; ++i) {
	    if (F[i] != i) {
		MSG("NO/NO: gather_all validation failed");
	    }
	}
	for (i = 0; i < numprocs; ++i) {
	    if (G[i] != i + myproc*r) {
		MSG("NO/NO: exchange validation failed");
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
    int j;
    int *A = segment;		/* int [1] */
    int *B = A + 1;		/* int [1] */
    int *C = B + 1;		/* int [N] */
    int *D = C + numprocs;	/* int [N] */

    if (myproc == 0)
	MSG("Starting MY/MY test");

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
	    MSG("MY/MY: broadcast validation failed");
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
		    MSG("MY/MY: gather validation failed");
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
	    MSG("MY/MY: scatter validation failed");
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
		MSG("MY/MY: gather_all validation failed");
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
		MSG("MY/MY: exchange validation failed");
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
    int j;
    int tmp;
    int *A = segment;		/* int [1] */
    int *B = A + 1;		/* int [1] */
    int *C = B + 1;		/* int [N] */
    int *D = C + numprocs;	/* int [N] */
    gasnet_node_t peer;

    if (myproc == 0)
	MSG("Starting ALL/ALL test");

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
	    MSG("ALL/ALL: broadcast validation failed");
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
		MSG("ALL/ALL: gather validation failed");
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
	    MSG("ALL/ALL: scatter validation failed");
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
		MSG("ALL/ALL: gather_all validation failed");
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
		MSG("ALL/ALL: exchange validation failed");
		gasnet_exit(1);
	    }
	}
    }

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void test_NB(int iters, gasnet_node_t root) {
    int j;
    int *A = segment;		/* int [iters] */
    int *B = test_malloc(iters*sizeof(int));
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    if (myproc == 0)
	MSG("Starting NB test");

    for (j = 0; j < iters; ++j) {
	B[j] = random();
	A[j] = (myproc == root) ? B[j] : 0;
	h[j] = gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, A+j, root, A+j, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_ALLSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (A[j] != B[j]) {
	    MSG("NB: broadcast validation failed");
	    gasnet_exit(1);
	}
    }
    test_free(B);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

int main(int argc, char **argv)
{
    int arg;
    int iters = 0;
    gasnet_node_t root;
    int *src;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();

    if (!myproc)
	print_testname("testcoll", numprocs);

#if GASNET_ALIGNED_SEGMENTS != 1
    if (myproc == 0) {
	printf("This test currently requires aligned segments - exiting w/o running the test\n");
        fflush(NULL);
    }
    BARRIER();
    return 0;
#else

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
    src = segment + 16;

    MSG("running.");
    BARRIER();

    srandom(1);

    for (root = 0; root < numprocs; ++root) {
      if (!myproc) MSG("Running tests with root = %d", (int)root);

      test_NONO(iters, root);
      test_MYMY(iters, root);
      test_ALLALL(iters, root);
      test_NB(iters, root);
    }

    BARRIER();

    MSG("done.");
#endif	/* Aligned segments */

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */
