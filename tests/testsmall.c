/************************************************************
	testsmall.c:
		measures the round-trip time and throughput of 
		get and put by varying payload size (1,2,4 or 8 bytes).
		
*************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include "gasnet.h"
#include "test.h"




#define GASNET_HEADNODE 0
#define PRINT_LATENCY 0
#define PRINT_THROUGHPUT 1

typedef struct {
	int datasize;
	int iters;
	uint64_t time;
} stat_struct_t;

gasnet_handlerentry_t handler_table[2];

int myproc;
int numprocs;
int peerproc;

gasnet_seginfo_t *seginfo_table;
void *srcmem;
void *tgtmem;
int srcsize;
int tgtsize;

char msgbuf[PAGESZ];
char ackbuf[PAGESZ];

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
		printf("Proc %2i - %4i byte : %7i iters,"
			   " latency %10i us total, %9.3f us ave. (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_THROUGHPUT:
		printf("Proc %2i - %4i byte : %7i iters,"
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


void roundtrip_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put(peerproc, tgtmem, msgbuf, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put latency", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get(ackbuf, peerproc, tgtmem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get latency", PRINT_LATENCY);
	}	
}

void oneway_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put(peerproc, tgtmem, msgbuf, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put throughput", PRINT_THROUGHPUT);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get(ackbuf, peerproc, tgtmem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get throughput", PRINT_THROUGHPUT);
	}	
}


void roundtrip_nbi_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of nonblocking implicit put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi(peerproc, tgtmem, msgbuf, nbytes);
			gasnet_wait_syncnbi_puts();
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi latency", PRINT_LATENCY);
	}	


	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of nonblocking implicit get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi(ackbuf, peerproc, tgtmem, nbytes);
			gasnet_wait_syncnbi_gets();
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nbi latency", PRINT_LATENCY);
	}	

}

void oneway_nbi_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of nonblocking implicit put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi(peerproc, tgtmem, msgbuf, nbytes);
		}
		gasnet_wait_syncnbi_puts();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi throughput", PRINT_THROUGHPUT);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the throughput of nonblocking implicit get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi(ackbuf, peerproc, tgtmem, nbytes);
		}
		gasnet_wait_syncnbi_gets();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nbi throughput", PRINT_THROUGHPUT);
	}	
}


void roundtrip_nb_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
    gasnet_handle_t hdlget, hdlput;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(srcmem, 0, nbytes);
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of nonblocking put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			hdlput = gasnet_put_nb(peerproc, tgtmem, msgbuf, nbytes);
			gasnet_wait_syncnb(hdlput);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nb latency", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of nonblocking get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		hdlget = gasnet_get_nb(ackbuf, peerproc, tgtmem, nbytes);
			gasnet_wait_syncnb(hdlget);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nb latency", PRINT_LATENCY);
	}	

}

void oneway_nb_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
    gasnet_handle_t hdlget, hdlput;
    gasnet_handle_t *handles;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	handles = (gasnet_handle_t*) malloc(sizeof(gasnet_handle_t) * iters);
	if (handles == NULL) {
		printf("Cannot allocate handles for non blocking operations.\n");
		gasnet_exit(1);
	}
	
	memset(srcmem, 0, nbytes);
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of sending a message */
		begin = TIME();
		/*for (i = 0; i < iters; i++) {
			hdlput = gasnet_put_nb(peerproc, tgtmem, msgbuf, nbytes);
		        gasnet_wait_syncnb(hdlput);
		}*/
                for (i = 0; i < iters; i++) {
                        handles[i] = gasnet_put_nb(peerproc, tgtmem, msgbuf, nbytes);
                }
		gasnet_wait_syncnb_all(handles, iters); 
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nb throughput", PRINT_THROUGHPUT);
	}	
	
	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the throughput of receiving a message */
		begin = TIME();
		/*for (i = 0; i < iters; i++) {
		    hdlget = gasnet_get_nb(msgbuf, peerproc, tgtmem, nbytes);
		    gasnet_wait_syncnb(hdlget);
		}*/
                for (i = 0; i < iters; i++) {
                    handles[i] = gasnet_get_nb(msgbuf, peerproc, tgtmem, nbytes);
                } 
		gasnet_wait_syncnb_all(handles, iters); 
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nb throughput", PRINT_THROUGHPUT);
	}	
	
	free(handles);
}

int main(int argc, char **argv)
{
    int iters = 0;
    int i, j;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

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
    assert(seginfo_table[peerproc].size == TEST_SEGSZ);

	for (j = 1; j <= 2048; j *= 2)  roundtrip_test(iters, j); 

  	for (j = 1; j <= 2048; j *= 2)  oneway_test(iters, j);

  	for (j = 1; j <= 2048; j *= 2)  roundtrip_nbi_test(iters, j);

  	for (j = 1; j <= 2048; j *= 2)  oneway_nbi_test(iters, j);

  	for (j = 1; j <= 2048; j *= 2)  roundtrip_nb_test(iters, j);

  	for (j = 1; j <= 2048; j *= 2)  oneway_nb_test(iters, j);

	free(seginfo_table);

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */
