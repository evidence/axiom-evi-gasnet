/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testcoll.c,v $
 *     $Date: 2005/01/22 01:22:13 $
 * $Revision: 1.7 $
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
}

int main(int argc, char **argv)
{
    int arg;
    int iters = 0;
    gasnet_node_t root;
    int *src;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();

#if GASNET_ALIGNED_SEGMENTS != 1
    if (myproc == 0) {
	printf("This test currently requires aligned segments - exiting w/o running the test\n");
    }
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
      /* test_ALLALL(iters, root); */
    }

    BARRIER();

    MSG("done.");
#endif	/* Aligned segments */

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */
