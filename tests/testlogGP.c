/*  $Archive:: /Ti/GASNet/tests/testsmall.c                                 $
 *     $Date: 2003/07/10 23:12:05 $
 * $Revision: 1.2 $
 * Description: GASNet logGP tester.
 *   measures the ping-pong average round-trip time and
 *   average flood throughput of GASNet gets and puts
 *   over varying payload size and synchronization mechanisms
 * Copyright 2002, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <float.h>
                                                                                
#include "gasnet.h"
#include "test.h"

#define LOOPS 10000 /* bad things will happen if this isn't big enough */

#define GASNET_HEADNODE 0
#define PRINT_LATENCY 0
#define PRINT_HALF_LATENCY 1
#define PRINT_THROUGHPUT 2
#define PRINT_BIG_G 3

typedef struct {
	int datasize;
	int iters;
	int64_t time;
} stat_struct_t;

gasnet_handlerentry_t handler_table[2];

int myproc;
int numprocs;
int peerproc;

void *srcmem;
void *tgtmem;
int srcsize;
int tgtsize;

static float x, y;
static volatile float z = (1.0 + FLT_EPSILON);
                                                                                
void cpu_loop(int n)
{
  int i;
	                                                                                   
  y = z;
  x = 1.0;
  for (i=0; i<n; i++) { x *= y; }
}

void init_stat(stat_struct_t *st, int sz)
{
	st->iters = 0;
	st->datasize = sz;
	st->time = 0;
}

void update_stat(stat_struct_t *st, int64_t temptime, int iters)
{
	st->iters += iters;
	st->time += temptime;
} 

void print_stat(int myproc, stat_struct_t *st, char *name, int operation)
{
	switch (operation) {
	case PRINT_LATENCY:
		printf("Proc %2i - %7i byte : %5i iters,"
			   " total %10i us elapsed = %9.3f us/msg (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_HALF_LATENCY:
		printf("Proc %2i - %7i byte : %5i iters,"
			   " total %10i us elapsed = %9.3f us/msg (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			(0.5*(float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_BIG_G:
		printf("Proc %2i - %7i byte : %5i iters,"
			   " total %10i us elapsed = %9.3f us/Kb (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			(1024.0*(float)st->time) / ((float)st->iters * (float)st->datasize),
			name);
		fflush(stdout);
		break;
	default:
		printf("ERROR\n");
		break;
	}
}


void latency_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put(peerproc, tgtmem, srcmem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put EEL", PRINT_HALF_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get(srcmem, peerproc, tgtmem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get EEL", PRINT_HALF_LATENCY);
	}	
}

void gap_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of nonblocking implicit put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi(peerproc, tgtmem, srcmem, nbytes);
		}
		gasnet_wait_syncnbi_puts();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi gap", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the throughput of nonblocking implicit get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi(srcmem, peerproc, tgtmem, nbytes);
		}
		gasnet_wait_syncnbi_gets();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nbi gap", PRINT_LATENCY);
	}	
}


void overhead_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of nonblocking implicit put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi(peerproc, tgtmem, srcmem, nbytes);
			cpu_loop(LOOPS);
		}
		gasnet_wait_syncnbi_puts();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);

		/* subtract our CPU time */
		begin = TIME();
		for (i = 0; i < iters; i++) { cpu_loop(LOOPS); }
		end = TIME();
	 	update_stat(&st, (begin - end), 0);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi initator overhead", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the throughput of nonblocking implicit get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi(srcmem, peerproc, tgtmem, nbytes);
			cpu_loop(LOOPS);
		}
		gasnet_wait_syncnbi_gets();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);

		/* subtract our CPU time */
		begin = TIME();
		for (i = 0; i < iters; i++) { cpu_loop(LOOPS); }
		end = TIME();
	 	update_stat(&st, (begin - end), 0);
	}
	
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nbi initator overhead", PRINT_LATENCY);
	}	
}


void bigG_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	memset(srcmem, 0, nbytes);

	/* initialize statistics */
	init_stat(&st, nbytes);

	BARRIER();
	
	if (iamsender) {
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi_bulk(peerproc, tgtmem, srcmem, nbytes);
		}
		gasnet_wait_syncnbi_puts();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi_bulk G", PRINT_BIG_G);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi_bulk(srcmem, peerproc, tgtmem, nbytes);
		}
		gasnet_wait_syncnbi_gets();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
    		print_stat(myproc, &st, "get_nbi_bulk G", PRINT_BIG_G);
	}	
}


int main(int argc, char **argv)
{
    int iters = 0;
    int bigsz = 0;
    int i, j;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

    /* parse arguments */
    if (argc < 2 || argc > 3) {
        printf( "Usage: %s bigsz [iters] \n"
		"    bigsz is size to use for measuring G (max possible is %d)\n"
        	"    iters defaults to 1000 \n", argv[0], TEST_SEGSZ);
        gasnet_exit(1);
    }

    bigsz = atoi(argv[1]);
    if (argc > 2) iters = atoi(argv[2]);
    if (!iters) iters = 1000;

    if (bigsz < 0 || bigsz > TEST_SEGSZ) {
        printf("bigsz is limited to <= %d\n", TEST_SEGSZ);
        gasnet_exit(1);
    }

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    /* Only allow 1 or even number for numprocs */
    if (numprocs % 2 == 1) {
    	printf("Number of threads should be even number.\n");
    	gasnet_exit(1);
    }
    
    /* initialize global data in my thread */
    srcmem = (void *) TEST_MYSEG();
    
    /* Setting peer thread rank */
    peerproc = (myproc % 2) ? (myproc - 1) : (myproc + 1);
    
    tgtmem = (void *) TEST_SEG(peerproc);

	latency_test(iters, 8); 
	overhead_test(iters, 8); 
  	gap_test(iters, 8);
  	bigG_test(iters, bigsz);

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */
