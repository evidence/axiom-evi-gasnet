/************************************************************
	testlarge.c:
		measures the bandwidth of get and put
		for large messages with payload size (512 .. limit bytes)
		also measures the barrier time.
		
*************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include "gasnet.h"
#include "test.h"

DECLARE_ALIGNED_SEG(4096*16);


#define GASNET_HEADNODE 0
#define PRINT_LATENCY 0
#define PRINT_THROUGHPUT 1

typedef struct {
	int datasize;
	int iters;
	uint64_t time;
	double max_throughput;
} stat_struct_t;

gasnet_handlerentry_t handler_table[2];

int myproc;
int numprocs;
int peerproc;

int min_payload;
int max_payload;

gasnet_seginfo_t *seginfo_table;
char *srcmem;
char *tgtmem;
void *msgbuf;

void init_stat(stat_struct_t *st, int sz)
{
	st->iters = 0;
	st->datasize = sz;
	st->time = 0;
}

void update_stat(stat_struct_t *st, uint64_t temptime, int iters)
{
	st->iters += iters;
	st->time += temptime;
} 

void print_stat(int myproc, stat_struct_t *st, char *name, int operation)
{
	switch (operation) {
	case PRINT_LATENCY:
		printf("Proc %3i - %10i byte : %7i iters,"
			   " latency %12i us total, %9.3f us ave. (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_THROUGHPUT:
		printf("Proc %3i - %10i byte : %7i iters,"
			" throughput %9.3f KB/sec (%s)\n",
			myproc, st->datasize, st->iters,
			(1000000.0 * st->datasize * st->iters / 1024.0) / ((int)st->time),
			name);
		fflush(stdout);
		break;
	default:
		break;
	}
}

void bulk_test(int iters) {GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    int64_t temptime;
    stat_struct_t stget, stput;
    int payload;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;
    
	for (payload = min_payload; payload <= max_payload; payload *= 2) {
		init_stat(&stput, payload);

		BARRIER();
	
		if (iamsender) {
			/* measure the throughput of sending a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
				gasnet_put_bulk(peerproc, tgtmem, msgbuf, payload);
			}
			end = TIME();
		 	update_stat(&stput, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stput, "put_bulk throughput", PRINT_THROUGHPUT);
		}	
	
		init_stat(&stget, payload);

		if (iamsender) {
			/* measure the throughput of receiving a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
			    gasnet_get_bulk(msgbuf, peerproc, tgtmem, payload);
			}
			end = TIME();
		 	update_stat(&stget, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stget, "get_bulk throughput", PRINT_THROUGHPUT);
		}	

	}

}

void bulk_test_nbi(int iters) {GASNET_BEGIN_FUNCTION();
    int i, j;
    int increment = 1024;
    int64_t begin, end;
    int64_t temptime;
    stat_struct_t stget, stput;
    int payload;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;
    
	for (payload = min_payload; payload <= max_payload; payload *= 2) {
		init_stat(&stput, payload);

		BARRIER();
	
		if (iamsender) {
			/* measure the throughput of sending a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
				gasnet_put_nbi_bulk(peerproc, tgtmem, msgbuf, payload);
			}
			gasnet_wait_syncnbi_puts();
			end = TIME();
		 	update_stat(&stput, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stput, "put_nbi_bulk throughput", PRINT_THROUGHPUT);
		}	
	
		init_stat(&stget, payload);

		if (iamsender) {
			/* measure the throughput of receiving a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
			    gasnet_get_nbi_bulk(msgbuf, peerproc, tgtmem, payload);
			}
			gasnet_wait_syncnbi_gets();
			end = TIME();
		 	update_stat(&stget, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stget, "get_nbi_bulk throughput", PRINT_THROUGHPUT);
		}	

	}

}

void bulk_test_nb(int iters) {GASNET_BEGIN_FUNCTION();
    int i, j;
    int64_t begin, end;
    int64_t temptime;
    int increment = 1024;
    stat_struct_t stget, stput;
    gasnet_handle_t hdlget, hdlput;
    gasnet_handle_t *handles;
    int payload;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;
    
	handles = (gasnet_handle_t *) malloc(sizeof(gasnet_handle_t) * iters);
	if (handles == NULL) {
		printf("Cannot allocate handles for non blocking operations.\n");
		gasnet_exit(1);
	}

	for (payload = min_payload; payload <= max_payload; payload *= 2) {
		init_stat(&stput, payload);

		BARRIER();
	
		if (iamsender) {
			/* measure the throughput of sending a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
				handles[i] = gasnet_put_nb_bulk(peerproc, tgtmem, msgbuf, payload);
			}
			gasnet_wait_syncnb_all(handles, iters);
			end = TIME();
		 	update_stat(&stput, (end - begin), iters);
		}
	
		BARRIER();
       
		if (iamsender) {
			print_stat(myproc, &stput, "put_nb_bulk throughput", PRINT_THROUGHPUT);
		}	
	
		init_stat(&stget, payload);

		if (iamsender) {
			/* measure the throughput of receiving a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
			    handles[i] = gasnet_get_nb_bulk(msgbuf, peerproc, tgtmem, payload);
			}
			gasnet_wait_syncnb_all(handles, iters);
			end = TIME();
		 	update_stat(&stget, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stget, "get_nb_bulk throughput", PRINT_THROUGHPUT);
		}	

	}

	free(handles);
}


int main(int argc, char **argv)
{
    int iters = 0;
   
    int *seg = (int*)MYSEG();
 
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv, NULL, 0, MYSEG(), SEGSZ(), 0));

    /* parse arguments */
    if (argc < 2) {
        printf("Usage: %s (iters) \n", argv[0]);
        gasnet_exit(1);
    }

    if (argc > 1) iters = atoi(argv[1]);
    if (!iters) iters = 1;

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    /* Only allow 1 or even number for numprocs */
    if (numprocs % 2 == 1) {
    	printf("Number of threads should be even number.\n");
    	gasnet_exit(1);
    }

    seginfo_table = (gasnet_seginfo_t *) malloc(sizeof(gasnet_seginfo_t) * numprocs);
    if (seginfo_table == NULL) {
    	printf("Cannot allocate seginfo_table.\n");
    	gasnet_exit(1);
    }
    GASNET_Safe(gasnet_getSegmentInfo(seginfo_table, numprocs));
    
    /* initialize global data in my thread */
    srcmem = (void *) seginfo_table[myproc].addr;
    
    /* Setting peer thread rank */
    peerproc = (myproc % 2) ? (myproc - 1) : (myproc + 1);
    
    tgtmem = (void *) seginfo_table[peerproc].addr;

	min_payload = 16;
	max_payload = SEGSZ();
	msgbuf = (void *) malloc(max_payload);
	if (msgbuf == NULL) {
		printf("Cannot allocate %d bytes for temporary storage.\n", max_payload);
		gasnet_exit(1);
	}

	bulk_test(iters);
	bulk_test_nbi(iters);
	bulk_test_nb(iters);

	free(msgbuf);
	free(seginfo_table);

    gasnet_exit(0);

    return 0;

}


/* ------------------------------------------------------------------------------------ */
