/* $Id: testcore1.c,v 1.4 2003/04/25 22:55:53 welcome Exp $
 * $Date: 2003/04/25 22:55:53 $
 * $Revision: 1.4 $
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 *
 * Description: GASNet Core Monotonic checksum test
 * This stress tests the ability of the core to successfully send
 * AM Requests/Replies through get and/or put functions.
 *
 * Get version:
 * 1. Node A generates 'n' random seeds and keeps a local checksum for each
 *    . .(barrier)
 * 2. Node B gets the 'n' seeds from Node A and keeps its checksum for each
 *    . .(barrier)
 * 3. Node A gets the checksum hashes from Node B and compares them against
 *    its own copy once the gets are sync'd
 *
 * Put version:
 * Steps 2 and 3 are puts for each other node.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <gasnet.h>
#include "test.h"

#define DEBUG_TRACE
#define CHKSUM_LENGTH	8
#define CHKSUM_NUM	400
#define CHKSUM_TOTAL	CHKSUM_LENGTH*CHKSUM_NUM
#define TESTSafe(x, msg) do {					\
	    if (!(x)) {  printf msg; gasnet_exit(1); } } while (0)

typedef struct {
	uint32_t	seed;
	unsigned char	chksum[CHKSUM_TOTAL];
} monoseed_t;

monoseed_t	 *_mseed;

int	myproc;
int	numproc;
int	peerproc;
int	numprocs;
gasnet_seginfo_t *seginfo_table;

/* Test specific globals */
int		chksum_success = 0;
int		chksum_iters = 0;
int		chksum_received = 0;
gasnet_hsl_t    chksum_recv_lock = GASNET_HSL_INITIALIZER;

#define CHKSUM_DUMP(chksum) do {			\
		int i = 0;				\
		uint8_t *c = (uint8_t *)chksum;		\
		printf("0x");				\
		for (i = 0; i < CHKSUM_TOTAL; i++)	\
			printf("%0x", c[i]);		\
	} while (0);

#ifdef VERBOSE
void
monoseed_trace(int iter, int seed, void *chksum_loc, void *chksum_rem)
{
	printf("%d> iter=%4d, seed=%12d, chksum_local=", myproc, iter, seed); 
	CHKSUM_DUMP(chksum_loc);
	
	if (chksum_rem != NULL) {
		printf(" chksum_remote=");
		CHKSUM_DUMP(chksum_rem);
	}
	printf("\n"); fflush(stdout);
}
#else
#define monoseed_trace(iter, seed, chksum_loc, chksum_rem)
#endif

void
chksum_gen(int seed, void *buf)
{
	int		i;
	uint64_t	chksum;
        uint8_t         *p = buf;

	chksum = test_checksum((void *)&seed, 4);
	for (i = 0; i < CHKSUM_NUM; i++) {
		chksum = test_checksum((void *)&chksum, CHKSUM_LENGTH);
		memcpy(p, &chksum, CHKSUM_LENGTH);
		p += CHKSUM_LENGTH;
	}
	return;
}

void
monoseed_init(int num)
{
	int 		i;
	uint64_t	chksum;

	if (myproc % 2 == 0) {
		_mseed = (monoseed_t *) malloc(sizeof(monoseed_t) * num);
		assert(_mseed != NULL);
		srandom(time(0));

		for (i = 0; i < num; i++) {
			_mseed[i].seed = (int) random() + 1;
			chksum_gen(_mseed[i].seed, &_mseed[i].chksum);
		}
	}
	return;
}

void
chksum_test(int iters)
{
	int	i;
	int	iamsender, iamreceiver;
#ifdef VERBOSE
	int     nloop = 0;
#endif

	iamsender = (myproc % 2 == 0);
	iamreceiver = !iamsender;

	BARRIER();

	if (iamsender) {
		for (i = 0; i < iters; i++)
			GASNET_Safe(
			    gasnet_AMRequestShort2((gasnet_node_t)peerproc, 
				201, i, _mseed[i].seed));
	}

	while (chksum_received < iters) {
		/*
		if (iamreceiver) {
			if (chksum_received % 5 == 0) {
				printf("sleep 1\n");
				sleep(1);
			}
		}
		*/
#ifdef VERBOSE
	    nloop++;
	    if (nloop % 1000 == 0) {
		printf("TEST[%d] nloop = %d chksum_received = %d\n",
		       myproc,nloop,chksum_received);
	    }
#endif
	    gasnet_AMPoll();
	}
#ifdef VERBOSE
	printf("TEST[%d] COMPLETE: nloop = %d chksum_received = %d\n",
	       myproc,nloop,chksum_received);
#endif

	BARRIER();

	if (iamsender) {
		printf("chksum_test(%d) passed %d/%d\n", chksum_iters, 
		    chksum_success, chksum_received);
	}
}


/*
 * Format is
 * AMRequestShort2(dest, chksum_reqh, i, seed)
 *
 * chksum_reqh(i, seed) generates the checksum and replies with a Medium
 *
 * AMReplyMedium(token, chksum_reph, src, nbytes, i)
 *
 * chksum_reph(i, src, nbytes) compares src[nbytes] to its copy of the
 * checksum at i
 */
void chksum_reqh(gasnet_token_t token, 
	gasnet_handlerarg_t iter, gasnet_handlerarg_t seed)
{
        unsigned char   chksum_reqbuf[CHKSUM_TOTAL];
	gasnet_hsl_lock(&chksum_recv_lock);
	chksum_received++;
	gasnet_hsl_unlock(&chksum_recv_lock);
	chksum_gen(seed, &chksum_reqbuf);
	monoseed_trace(iter, seed, &chksum_reqbuf, NULL);
	GASNET_Safe( 
	    gasnet_AMReplyMedium1(token, 202, &chksum_reqbuf, 
	        CHKSUM_TOTAL, iter));
	return;
}

void
chksum_reph(gasnet_token_t token, 
	void *buf, size_t nbytes, gasnet_handlerarg_t iter) 
{
	uint64_t	chksum;

	chksum_received++;
	assert(iter < chksum_iters && iter >= 0);
	assert(nbytes == CHKSUM_TOTAL);
	monoseed_trace(iter, _mseed[iter].seed, &_mseed[iter].chksum, buf);
	if (memcmp(&_mseed[iter].chksum, buf, CHKSUM_LENGTH) == 0) 
		chksum_success++;
	else {
		printf("iter %3d failed! chksum_local=", iter);
		CHKSUM_DUMP(&_mseed[iter].chksum);
		printf(" chksum_remote=");
		CHKSUM_DUMP(buf);
		printf("\n");
	}
	return;
}
	
/* Test handlers */

int
main(int argc, char **argv)
{
	int	iters = 0;
	gasnet_handlerentry_t htable[] = {
		{ 201, chksum_reqh },
		{ 202, chksum_reph }
	};

	/* call startup */
        GASNET_Safe(gasnet_init(&argc, &argv));
        GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t), TEST_SEGSZ, TEST_MINHEAPOFFSET));

        assert(CHKSUM_TOTAL <= gasnet_AMMaxMedium());

	if (argc < 2) {
		printf("Usage: %s <iters>\n", argv[0]);
		gasnet_exit(1);
	}
	if (argc > 1) iters = atoi(argv[1]);
	if (!iters) iters = 1;

	/* get SPMD info */
	chksum_iters = iters;
	myproc = gasnet_mynode();
	numprocs = gasnet_nodes();
	TESTSafe(numprocs % 2 == 0, ("Need an even number of threads\n"));
	peerproc = (myproc % 2) ? myproc-1 : myproc+1;

	seginfo_table = (gasnet_seginfo_t *) malloc(sizeof(gasnet_seginfo_t) * numprocs);
	if (seginfo_table == NULL) {
		printf("Cannot allocate seginfo_table.\n");
		gasnet_exit(1);
	}

	printf("%d> starting monoseed_init(%d)\n", myproc, iters);
	monoseed_init(iters);
	printf("%d> starting chksums_test(%d)\n", myproc, iters);
	chksum_test(iters);

	gasnet_exit(0);
	return(0);
}
