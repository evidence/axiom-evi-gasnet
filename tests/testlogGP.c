/*  $Archive:: /Ti/GASNet/tests/testsmall.c                                 $
 *     $Date: 2003/07/11 21:17:49 $
 * $Revision: 1.6 $
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
#include <time.h>
#include <float.h>
                                                                                
#include "gasnet.h"
#include "test.h"

#define GASNET_HEADNODE 0

/* smallest number of delay loops to try */
#define LOOP_MIN	100

enum {
	PRINT_EEL,
	PRINT_OVERHEAD,
	PRINT_GAP,
	PRINT_BIG_G
};

typedef struct {
	int datasize;
	int iters;
	int64_t time;
} stat_struct_t;

gasnet_handlerentry_t handler_table[2];

int myproc;
int numprocs;
int peerproc;

void *mymem;
void *peermem;

extern void delay(int n);

/* Compute some number of loops needed to get no less that the specified delay.
 * Returns the number of loops needed and overwrites the argument with the
 * actual achieved delay
 */
int calibrate_delay(int iters, int64_t *time_p) 
{
	int64_t begin, end, time;
	float target = *time_p;
	float ratio = 0.0;
	int i, loops = 0;

	do {
		if (loops == 0) {
			loops = LOOP_MIN;	/* first pass */
		} else {
			int tmp = loops * ratio;

			if (tmp > loops) {
				loops = tmp;
			} else {
				loops += 1;	/* ensure progress in the face of round-off */
			}
		}

		begin = TIME();
		for (i = 0; i < iters; i++) { delay(loops); }
		end = TIME();
		time = end - begin;
		ratio = target / (float)time;
	} while (ratio > 1.0);

	*time_p = time;
	return loops;
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
	case PRINT_EEL:
		printf("Proc %2i - %7i byte : %5i iters,"
			   " %10i us elapsed    = %9.3f us/msg (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			(0.5*(float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_OVERHEAD:
		printf("Proc %2i - %7i byte : %5i iters,"
			   " %10i us difference = %9.3f us/msg (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_GAP:
		printf("Proc %2i - %7i byte : %5i iters,"
			   " %10i us elapsed    = %9.3f us/msg (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_BIG_G:
		printf("Proc %2i - %7i byte : %5i iters,"
			   " %10i us elapsed    = %9.3f us/Kb  (%s)\n",
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


void put_tests(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i, loops;
    int64_t begin, end, delay_time;
    stat_struct_t st;

	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	memset(mymem, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of put */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put(peerproc, peermem, mymem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
		print_stat(myproc, &st, "put: EEL - put", PRINT_EEL);
	}
	
	BARRIER();
	
	if (iamsender) {
    		/* measure baseline (no cpu loop) gap for nonblocking explicit puts */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_handle_t h = gasnet_put_nb(peerproc, peermem, mymem, nbytes);
			gasnet_wait_syncnb(h);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);

    		/* Seek number of loops needed to exceed running time by 20% or more */
		delay_time = 1.2 * st.time;
		loops = calibrate_delay(iters, &delay_time);

		/* Now measure overhead */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_handle_t h = gasnet_put_nb(peerproc, peermem, mymem, nbytes);
			delay(loops);
			gasnet_wait_syncnb(h);
		}
		end = TIME();
	 	update_stat(&st, (end - begin) - delay_time, iters);
		print_stat(myproc, &st, "put: o_i - put_nb", PRINT_OVERHEAD);
	}

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of nonblocking implicit put */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi(peerproc, peermem, mymem, nbytes);
		}
		gasnet_wait_syncnbi_puts();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
		print_stat(myproc, &st, "put: gap - put_nbi", PRINT_GAP);
	}
	
	BARRIER();

	if (iamsender) {
		/* measure the throughput of nonblocking implicit bulk put */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi_bulk(peerproc, peermem, mymem, nbytes);
		}
		gasnet_wait_syncnbi_puts();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
		print_stat(myproc, &st, "put: G   - put_nbi_bulk", PRINT_BIG_G);
	}
}	


void get_tests(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i, loops;
    int64_t begin, end, delay_time;
    stat_struct_t st;
    float ratio;

	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	memset(mymem, 0, nbytes);

	BARRIER();

	if (iamsender) {
		/* measure the round-trip time of get */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get(mymem, peerproc, peermem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
		print_stat(myproc, &st, "get: EEL - get", PRINT_EEL);
	}
	
	BARRIER();
	
	if (iamsender) {
    		/* measure baseline (no cpu loop) gap for nonblocking explicit gets */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_handle_t h = gasnet_get_nb(mymem, peerproc, peermem, nbytes);
			gasnet_wait_syncnb(h);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);

    		/* Seek number of loops needed to exceed running time by 20% or more */
		delay_time = 1.2 * st.time;
		loops = calibrate_delay(iters, &delay_time);

		/* Now measure overhead */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_handle_t h = gasnet_get_nb(mymem, peerproc, peermem, nbytes);
			delay(loops);
			gasnet_wait_syncnb(h);
		}
		end = TIME();
	 	update_stat(&st, (end - begin) - delay_time, iters);
		print_stat(myproc, &st, "get: o_i - get_nb", PRINT_OVERHEAD);
	}

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of nonblocking implicit get */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi(mymem, peerproc, peermem, nbytes);
		}
		gasnet_wait_syncnbi_gets();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
		print_stat(myproc, &st, "get: gap - get_nbi", PRINT_GAP);
	}
	
	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of nonblocking implicit bulk put */
		init_stat(&st, nbytes);
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi_bulk(mymem, peerproc, peermem, nbytes);
		}
		gasnet_wait_syncnbi_gets();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
    		print_stat(myproc, &st, "get: G   - get_nbi_bulk", PRINT_BIG_G);
	}
}


int main(int argc, char **argv)
{
    int iters = 0;
    int i;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

    /* parse arguments */
    if (argc < 3) {
        printf( "Usage: %s iters sizes... \n"
		"    sizes are limited to %d\n", argv[0], TEST_SEGSZ);
        gasnet_exit(1);
    }

    iters = atoi(argv[1]);
    if (!iters) iters = 1;

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    /* Only allow 1 or even number for numprocs */
    if (numprocs % 2 == 1) {
    	printf("Number of threads should be even number.\n");
    	gasnet_exit(1);
    }
    
    /* initialize global data in my thread */
    mymem = (void *) TEST_MYSEG();
    
    /* Setting peer thread rank */
    peerproc = (myproc % 2) ? (myproc - 1) : (myproc + 1);
    
    peermem = (void *) TEST_SEG(peerproc);

    for (i = 2; i < argc; ++i) {
        int size = atoi(argv[i]);

        if (size < 0 || size > TEST_SEGSZ) {
            printf("size is limited to <= %d\n", TEST_SEGSZ);
            continue;
        }

	put_tests(iters, size); 
	get_tests(iters, size); 
    }

    BARRIER();
    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */
