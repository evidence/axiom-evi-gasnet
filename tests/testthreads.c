/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testthreads.c,v $
 *     $Date: 2004/08/26 04:54:09 $
 * $Revision: 1.16 $
 *
 * Description: GASNet threaded tester.
 *   The test initializes GASNet and forks off up to 256 threads.  Each of
 *   these threads randomly chooses from a set of communication operations for
 *   a given amount of iterations.  The idea is to detect race errors by having
 *   many threads concurrently use different GASNet communication operations.
 *
 * Copyright 2003, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include "gasnet.h"
#include "gasnet_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "test.h"

#define CACHE_LINE_BYTES	(128)

#ifndef GASNET_PAR
#error This test can only be built for GASNet PAR configuration
#endif

typedef 
struct _threaddata_t {
	int	tid;		/* global thread id */
	int	ltid;		/* local thread id (index into each node's
				   array of threaddata_t) */
	int	tid_peer;	/* global thread id of remote peer thread */
	int	tid_peer_local; /* global thread id of local peer thread */

	volatile int	flag;
	char	_pad[CACHE_LINE_BYTES-5*sizeof(int)];
} 
threaddata_t;

typedef void (*testfunc_t)(threaddata_t *);
typedef gasnet_handlerarg_t harg_t;

/* configurable parameters */
#define DEFAULT_ITERS 50
int	iters = DEFAULT_ITERS;
int	sleep_min_us = 1;
int	sleep_max_us = 250000;
int	amiters_max = 50;
int     verbose = 0;
int     amtrace = 0;


#define ACTION_PRINTF \
  if (GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), verbose) MSG

int	sizes[] = { 0, /* gasnet_AMMaxMedium()-1      */
                    0, /* gasnet_AMMaxMedium()        */
                    0, /* gasnet_AMMaxMedium()+1      */
                    0, /* gasnet_AMMaxLongRequest()-1 */
                    0, /* gasnet_AMMaxLongRequest()   */
                    0, /* gasnet_AMMaxLongRequest()+1 */
                    0, /* gasnet_AMMaxLongReply()-1   */
                    0, /* gasnet_AMMaxLongReply()     */
                    0, /* gasnet_AMMaxLongReply()+1   */
                    /* some other interesting fixed values */
                    0, 1, 9, 128, 256, 1024, 2048, 4095, 4096, 4097, 
                    16384, 30326, TEST_SEGZ_PER_THREAD };

#define	SIZES_NUM	(sizeof(sizes)/sizeof(int))
#define RANDOM_SIZE()	(sizes[ (rand() % SIZES_NUM)])

int		AM_loopback = 0;
int		threads_num;
pthread_t	*tt_tids;
gasnet_node_t	*tt_thread_map;
void		**tt_addr_map;
threaddata_t	*tt_thread_data;

#define thread_barrier() PTHREAD_BARRIER(threads_num)

void	alloc_thread_data(int threads);
void	free_thread_data();
void *	threadmain(void *args);

/* GASNet Test functions */
void	test_sleep(threaddata_t *tdata);
void	test_put(threaddata_t *tdata);
void	test_get(threaddata_t *tdata);
void	test_amshort(threaddata_t *tdata);
void	test_ammedium(threaddata_t *tdata);
void	test_amlong(threaddata_t *tdata);
#if TEST_MPI
void init_test_mpi(int *argc, char ***argv);
void attach_test_mpi();
void mpi_barrier(threaddata_t *tdata);
void test_mpi(threaddata_t *tdata);

void mpi_handler(gasnet_token_t token, harg_t tid, harg_t sz);
void mpi_probehandler(gasnet_token_t token, harg_t tid);
void mpi_replyhandler(gasnet_token_t token, harg_t tid);
#endif

testfunc_t	test_functions_all[] = {
	test_sleep, test_put, test_get, test_amshort, test_ammedium, test_amlong
#if TEST_MPI
        , test_mpi
#endif
};

#define NUM_FUNCTIONS	(sizeof(test_functions_all)/sizeof(testfunc_t))
/* This array remains uninitialized */
testfunc_t	test_functions[NUM_FUNCTIONS] = { 0 };
static int	functions_num = 0;

/* AM Handlers */
void	ping_shorthandler(gasnet_token_t token, harg_t tid);
void 	pong_shorthandler(gasnet_token_t token, harg_t tid);

void	ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		harg_t tid);
void	pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		harg_t tid);

void	ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes,
		harg_t tid, harg_t target_id);
void	pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		harg_t tid);

#define hidx_ping_shorthandler   201
#define hidx_pong_shorthandler   202
#define hidx_ping_medhandler     203
#define hidx_pong_medhandler     204
#define hidx_ping_longhandler    205
#define hidx_pong_longhandler    206
#define hidx_mpi_handler         207
#define hidx_mpi_probehandler    208
#define hidx_mpi_replyhandler    209

gasnet_handlerentry_t htable[] = { 
	{ hidx_ping_shorthandler,  ping_shorthandler  },
	{ hidx_pong_shorthandler,  pong_shorthandler  },
	{ hidx_ping_medhandler,    ping_medhandler    },
	{ hidx_pong_medhandler,    pong_medhandler    },
	{ hidx_ping_longhandler,   ping_longhandler   },
	{ hidx_pong_longhandler,   pong_longhandler   },
      #if TEST_MPI
	{ hidx_mpi_handler,        mpi_handler        },
	{ hidx_mpi_probehandler,   mpi_probehandler   },
	{ hidx_mpi_replyhandler,   mpi_replyhandler   },
      #endif
};
#define HANDLER_TABLE_SIZE (sizeof(htable)/sizeof(gasnet_handlerentry_t))

void
usage(char *progname)
{
	printf("usage: %s [ -pgalvt ] [ -i <iters> ] <threads_per_node>\n\n", progname);
	printf("<threads_per_node> must be between 1 and %i       \n",TEST_MAXTHREADS);
	printf("no options means run all tests with %i iterations\n",DEFAULT_ITERS);
	printf("options:                                      \n");
	printf("  -p  use puts                                   \n");
	printf("  -g  use puts                                   \n");
	printf("  -a  use Active Messages                        \n");
	printf("  -l  use local Active Messages                  \n");
      #if TEST_MPI
	printf("  -m  use MPI calls                              \n");
      #endif
	printf("  -v  output information about actions taken     \n");
	printf("  -t  include AM handler actions with -v         \n");
	printf("  -i <iters> use <iters> iterations per thread   \n\n");

	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int 		threads = 1;
	int		i;
	pthread_t	*tids;
        const char *getopt_str;
        int opt_p=0, opt_g=0, opt_a=0, opt_m=0;

        #if TEST_MPI
          init_test_mpi(&argc, &argv);
          getopt_str = "pgamlvti:";
        #else
          getopt_str = "pgalvti:";
        #endif

	GASNET_Safe(gasnet_init(&argc, &argv));
    	GASNET_Safe(gasnet_attach(htable, HANDLER_TABLE_SIZE,
		    TEST_SEGSZ, TEST_MINHEAPOFFSET));
        TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */

	while ((i = getopt (argc, argv, getopt_str)) != EOF) {
          switch (i) {
		case 'p': opt_p = 1; break;
		case 'g': opt_g = 1; break;
		case 'a': opt_a = 1; break;
                case 'm': opt_m = 1; break;
		case 'l': AM_loopback = 1; break;
		case 'i': iters = atoi(optarg); break;
                case 'v': verbose = 1; break;
                case 't': amtrace = 1; break;
		default:
			usage(argv[0]);
          }
	}

        if (opt_p) test_functions[functions_num++] = test_put;
        if (opt_g) test_functions[functions_num++] = test_get;
        if (opt_a) {
          test_functions[functions_num++] = test_amshort;
          test_functions[functions_num++] = test_ammedium;
          test_functions[functions_num++] = test_amlong;
        }
        #if TEST_MPI
          if (opt_m) test_functions[functions_num++] = test_mpi;
        #endif
        if (amtrace) verbose = 1;

	/* Assume all test functions if no option is passed */
	if (functions_num  == 0) {
		MSG("running all functions!");
		
		memcpy(test_functions, test_functions_all, 
				sizeof(test_functions_all));
		functions_num = NUM_FUNCTIONS;
	}

	argc -= optind;

	if (argc != 1)
		usage(argv[0]);
	else {
		argv += optind;
		threads_num = threads = atoi(argv[0]);
	}

	if (threads > TEST_MAXTHREADS || threads < 1) {
		printf("Threads must be between 1 and 256\n");
		exit(EXIT_FAILURE);
	}

        /* limit sizes to a reasonable size */
        #define LIMIT(sz) MIN(sz,4194304)
        { int sz = 0;
          sizes[sz++] = LIMIT(gasnet_AMMaxMedium()-1);
          sizes[sz++] = LIMIT(gasnet_AMMaxMedium());
          sizes[sz++] = LIMIT(gasnet_AMMaxMedium()+1);
          sizes[sz++] = LIMIT(gasnet_AMMaxLongRequest()-1);
          sizes[sz++] = LIMIT(gasnet_AMMaxLongRequest());
          sizes[sz++] = LIMIT(gasnet_AMMaxLongRequest()+1);
          sizes[sz++] = LIMIT(gasnet_AMMaxLongReply()-1);
          sizes[sz++] = LIMIT(gasnet_AMMaxLongReply());
          sizes[sz++] = LIMIT(gasnet_AMMaxLongReply()+1);
          assert(sizes[sz] == 0);
        }

	alloc_thread_data(threads);
        #if TEST_MPI
          attach_test_mpi();
        #endif

	{
		int 	i;
		void	*ret;

		MSG("Forking %d gasnet threads", threads);
		for (i = 0; i < threads; i++) {
                        pthread_attr_t attr;
                        pthread_attr_init(&attr);
                        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
			if (pthread_create(&tt_tids[i], &attr, threadmain, 
					(void *) &tt_thread_data[i]) != 0) {
				printf("Error forking threads\n");
				exit(EXIT_FAILURE);
			}
		}

		for (i = 0; i < threads; i++) {
			if (pthread_join(tt_tids[i], &ret) != 0) {
				printf("Error joining threads\n");
				exit(EXIT_FAILURE);
			}
		}
	}

        BARRIER();

	free_thread_data();

	MSG("Tests complete");

        BARRIER();

	gasnet_exit(0);

	return 0;
}

void *
threadmain(void *args)
{
	int	i, idx;

	testfunc_t	func;
	threaddata_t	*td = (threaddata_t *) args;

	srand((unsigned int) time(0) * td->tid);

	thread_barrier();

	MSG("tid=%3d> starting.", td->tid);

	for (i = 0; i < iters; i++) {
		idx = rand() % functions_num;
		func = test_functions[idx];
		assert(func != NULL);

		func(td);
	}

	thread_barrier();
	MSG("tid=%3d> done.", td->tid);

	return NULL;
}

void
alloc_thread_data(int threads)
{
	int	nodes, tot_threads;

	nodes = gasnet_nodes();
	tot_threads = nodes * threads;

	tt_tids = (pthread_t *) test_malloc(sizeof(pthread_t) * threads);
	tt_thread_map = (gasnet_node_t *) test_malloc(sizeof(gasnet_node_t) * tot_threads);
	tt_thread_data = (threaddata_t *) test_malloc(sizeof(threaddata_t) * threads);
	tt_addr_map = (void **) test_malloc(sizeof(void *) * tot_threads);

	/* Initialize the thread to node map array and local thread data */
	{
		int 	i, j, tid, base;
		void	*segbase;

		threaddata_t	*td;
		for (i = 0; i < nodes; i++) {
			segbase = TEST_SEG(i);

			base = i * threads;
			for (j = 0; j < threads; j++) {
				tid = base + j;
				tt_thread_map[tid] = i;
				tt_addr_map[tid] = (void *) 
				    ((uintptr_t) segbase + 
				     j * TEST_SEGZ_PER_THREAD);

				if (i == gasnet_mynode()) {
					td = &tt_thread_data[j];

					td->tid = tid;
					td->ltid = j;
					td->tid_peer_local = base + 
						((j+1) % threads);
					td->tid_peer = (tid+threads) % 
						tot_threads;
				}
			}
		}
	}
}


void
free_thread_data()
{
	test_free(tt_tids);
	test_free(tt_thread_map);
	test_free(tt_addr_map);
	test_free(tt_thread_data);
}

/****************************************************************/
/* AM Handlers */
#define PRINT_AM(x) \
  if (GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), amtrace) ACTION_PRINTF x

void 
ping_shorthandler(gasnet_token_t token, harg_t idx) 
{
	gasnet_node_t	node;
	gasnet_AMGetMsgSource(token, &node);

	PRINT_AM(("node=%2d> AMShort Request for (%d,%d)", 
			(int)gasnet_mynode(), (int)node, (int)idx));
        assert(idx >= 0 && idx < threads_num);
        assert(node < gasnet_nodes());
	GASNET_Safe(gasnet_AMReplyShort1(token, hidx_pong_shorthandler, idx));
}

void 
pong_shorthandler(gasnet_token_t token, harg_t idx) 
{
	int	tid = tt_thread_data[idx].tid;
	PRINT_AM(("node=%2d> AMShort Reply for tid=%d, (%d,%d)", 
			(int)gasnet_mynode(), tid, (int)gasnet_mynode(), (int)idx));
        assert(idx >= 0 && idx < threads_num);
        assert(tid >= 0 && tid < threads_num*gasnet_nodes());
	tt_thread_data[idx].flag++;
}

void 
ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes, harg_t idx) 
{
	gasnet_node_t	node;
	gasnet_AMGetMsgSource(token, &node);

	PRINT_AM(("node=%2d> AMMedium Request for (%d,%d)", 
			(int)gasnet_mynode(), (int)node, (int)idx));
        assert(idx >= 0 && idx < threads_num);
        assert(node < gasnet_nodes());
        assert(nbytes <= gasnet_AMMaxMedium());
        assert((uintptr_t)buf+nbytes < (uintptr_t)TEST_SEG(gasnet_mynode()) ||
               (uintptr_t)buf >= (uintptr_t)TEST_SEG(gasnet_mynode()) + TEST_SEGSZ);
	GASNET_Safe(
		gasnet_AMReplyMedium1(token, hidx_pong_medhandler, 
			buf, nbytes, idx));
}
void 
pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		gasnet_handlerarg_t idx) 
{
	int	tid = tt_thread_data[idx].tid;

	PRINT_AM(("node=%2d> AMMedium Reply for tid=%d, (%d,%d)", 
			(int)gasnet_mynode(), tid, (int)gasnet_mynode(), (int)idx));
        assert(idx >= 0 && idx < threads_num);
        assert(tid >= 0 && tid < threads_num*gasnet_nodes());
        assert(nbytes <= gasnet_AMMaxMedium());
        assert((uintptr_t)buf+nbytes < (uintptr_t)TEST_SEG(gasnet_mynode()) ||
               (uintptr_t)buf >= (uintptr_t)TEST_SEG(gasnet_mynode()) + TEST_SEGSZ);
	tt_thread_data[idx].flag++;
}

void 
ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes, harg_t idx, harg_t target_id) 
{
	int		tid;
	void		*paddr;
	gasnet_node_t	node;

	gasnet_AMGetMsgSource(token, &node);
	tid = node * threads_num + idx;
	paddr = tt_addr_map[tid];

	PRINT_AM(("node=%2d> AMLong Request for (%d,%d)", 
			(int)gasnet_mynode(), (int)node, (int)idx));
        assert(idx >= 0 && idx < threads_num);
        assert(node < gasnet_nodes());
        assert(nbytes <= gasnet_AMMaxLongRequest());
        assert(buf == tt_addr_map[target_id]);
        assert((uintptr_t)buf + nbytes <= (uintptr_t)TEST_SEG(gasnet_mynode()) + TEST_SEGSZ);
	GASNET_Safe(
		gasnet_AMReplyLong1(token, hidx_pong_longhandler, 
			buf, nbytes, paddr, idx));
}

void 
pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes, harg_t idx) {
	int	tid = tt_thread_data[idx].tid;

	PRINT_AM(("node=%2d> AMLong Reply for tid=%d, (%d,%d)", 
			(int)gasnet_mynode(), tid, (int)gasnet_mynode(), (int)idx));
        assert(idx >= 0 && idx < threads_num);
        assert(tid >= 0 && tid < threads_num*gasnet_nodes());
        assert(nbytes <= gasnet_AMMaxLongReply());
        assert(buf == tt_addr_map[gasnet_mynode() * threads_num + idx]);
        assert((uintptr_t)buf + nbytes <= (uintptr_t)TEST_SEG(gasnet_mynode()) + TEST_SEGSZ);
	tt_thread_data[idx].flag++;
}

/****************************************************************/
/* GASNet testers */

void
test_sleep(threaddata_t *tdata)
{
	unsigned	usecs = (unsigned) sleep_min_us + 
				(rand() % (sleep_max_us - sleep_min_us));
	ACTION_PRINTF("tid=%3d> sleeping %.3f millisecs", tdata->tid, usecs/1000.0);
        { uint64_t goal = gasnett_ticks_to_us(gasnett_ticks_now()) + usecs;
          while (gasnett_ticks_to_us(gasnett_ticks_now()) < goal) 
            gasnett_sched_yield();
        }
	ACTION_PRINTF("tid=%3d> awaking", tdata->tid);
}

void
test_put(threaddata_t *tdata)
{
	int	peer = tdata->tid_peer;
	int	node = tt_thread_map[peer];
	void	*laddr = tt_addr_map[tdata->tid];
	void	*raddr = tt_addr_map[peer];
	int	 len;
	do {
		len = RANDOM_SIZE();
	} while (len > TEST_SEGZ_PER_THREAD);

	ACTION_PRINTF("tid=%3d> put (%p,%8d) -> tid=%3d,node=%d,addr=%p",
			tdata->tid, laddr, len, peer, node, raddr);

	gasnet_put(node, raddr, laddr, len);
}

void
test_get(threaddata_t *tdata)
{
	int	peer = tdata->tid_peer;
	int	node = tt_thread_map[peer];
	void	*laddr = tt_addr_map[tdata->tid];
	void	*raddr = tt_addr_map[peer];
	int	 len;
	do {
		len = RANDOM_SIZE();
	} while (len > TEST_SEGZ_PER_THREAD);

	ACTION_PRINTF("tid=%3d> get (%p,%8d) <- tid=%3d,node=%d,addr=%p",
			tdata->tid, laddr, len, peer, node, raddr);

	gasnet_get(laddr, node, raddr, len);
}

#define RANDOM_PEER(tdata)					\
	(AM_loopback ? 						\
		(rand() % 2 == 0 ? tdata->tid_peer		\
				 : tdata->tid_peer_local)	\
	: tdata->tid_peer)

void
test_amshort(threaddata_t *tdata)
{
	int 	 	peer = RANDOM_PEER(tdata);
	int		node = tt_thread_map[peer];

	ACTION_PRINTF("tid=%3d> AMShortRequest to tid=%3d", tdata->tid, peer);
	tdata->flag = -1;
        gasnett_local_wmb();
	GASNET_Safe(gasnet_AMRequestShort1(node, 
		    hidx_ping_shorthandler, tdata->ltid));
	GASNET_BLOCKUNTIL(tdata->flag == 0);
	tdata->flag = -1;

	ACTION_PRINTF("tid=%3d> AMShortRequest to tid=%3d complete.", tdata->tid, peer);
}

void
test_ammedium(threaddata_t *tdata)
{
	int 	 	peer = RANDOM_PEER(tdata);
	int		node = tt_thread_map[peer];
	void		*laddr = tt_addr_map[tdata->tid];
	size_t	 	len;

	do {
		len = RANDOM_SIZE();
	} while (len > gasnet_AMMaxMedium());
		
	ACTION_PRINTF("tid=%3d> AMMediumRequest (sz=%7d) to tid=%3d", tdata->tid, (int)len, peer);
	tdata->flag = -1;
        gasnett_local_wmb();
	GASNET_Safe(gasnet_AMRequestMedium1(node, 
		    hidx_ping_medhandler, laddr, len, 
		    tdata->ltid));
	GASNET_BLOCKUNTIL(tdata->flag == 0);
	tdata->flag = -1;

	ACTION_PRINTF("tid=%3d> AMMediumRequest to tid=%3d complete.", tdata->tid, peer);
}


void
test_amlong(threaddata_t *tdata)
{
	int 	 	peer = RANDOM_PEER(tdata);
	int		node = tt_thread_map[peer];
	void		*laddr = tt_addr_map[tdata->tid];
	void		*raddr = tt_addr_map[peer];
	size_t	 	len;

	do {
		len = RANDOM_SIZE();
	} while ((len > gasnet_AMMaxLongRequest()) || (len > gasnet_AMMaxLongReply()) 
              || (len > TEST_SEGZ_PER_THREAD));
		
	tdata->flag = -1;
        gasnett_local_wmb();
	ACTION_PRINTF("tid=%3d> AMLongRequest (sz=%7d) to tid=%3d", tdata->tid, (int)len, peer);

	GASNET_Safe(gasnet_AMRequestLong2(node, 
		    hidx_ping_longhandler, laddr, len, raddr, 
		    tdata->ltid, peer));
	GASNET_BLOCKUNTIL(tdata->flag == 0);
	tdata->flag = -1;

	ACTION_PRINTF("tid=%3d> AMLongRequest to tid=%3d complete.", tdata->tid, peer);
}

