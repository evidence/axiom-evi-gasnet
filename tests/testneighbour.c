/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/Attic/testneighbour.c,v $
 *     $Date: 2005/03/12 11:21:16 $
 * $Revision: 1.4 $
 * Description: MG-like neighbour exchange
 * Copyright 2005, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/************************************************************
 * testneighbour.c:
 *   NAS MG modelled microbenchmark to measure the cost of neighbour ghost cell
 *   exchanges.  The benchmark replicates ghost exchanges over all dimensions
 *   (two of which generate strided data communication).
 *
*************************************************************/

#include "gasnet.h"
#include "gasnet_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
uintptr_t maxsegmentsz;
#ifndef TEST_SEGSZ
  #ifdef GASNET_SEGMENT_EVERYTHING
    #define TEST_SEGSZ_EXPR ((uintptr_t)maxsegmentsz)
  #else
    #define TEST_SEGSZ_EXPR ((uintptr_t)maxsegmentsz)
  #endif
#endif
#include "test.h"

typedef struct {
	int datasize;
	int iters;
	int dims;
	int axis;
	int category;
	uint64_t time;
	double max_throughput;
} stat_struct_t;

uintptr_t topalloc = 0;
FILE *nbour_fp;
int maxlevel = 4;
int myproc;
int nprocs;

#define CACHELINE_SZ	128
#define POWER_OF_TWO(P)	(((P)&((P)-1)) == 0)

#define PX_SZ (nb->dims[2]*nb->dims[1])
#define PY_SZ (nb->dims[2]*nb->dims[0])
#define PZ_SZ (nb->dims[1]*nb->dims[0])

#define AX	0
#define AY	1
#define AZ	2
#define AALL	3

#define LINEARIZEPROC(nb,k,j,i) (k*(nb)->procGrid[1]*(nb)->procGrid[0] + \
		                j*(nb)->procGrid[0] + i)

#define GHOST_TYPE_PUT	    0
#define GHOST_TYPE_AMLONG   1

#define GHOST_DIR_UPPER 0
#define GHOST_DIR_LOWER 1

#define GP_BLOCK(x) do { gasnet_handle_t h = (x);			    \
	    if ((h) != GASNET_INVALID_HANDLE) gasnet_wait_syncnb(h); } while (0)

/*
 * Memory requirements for this test differ according to the type of ghost
 * exchange being performed.  They all have the per-processor data grid size
 * memory requirement based on the level of the grid.
 *
 * Over all axes x,y,z planes along the z axis (xy) planes are contiguous in
 * memory.  This requires exchanges along the x and y axes to have some form 
 * of non-contiguous communication.
 *
 *
 * The following types of ghost exchanges are done:
 *
 * 1. UPC (Parry's MG)
 *    * Over each dimension, pack boundary plane in a buffer, send the buffer
 *      and signal the neighbour with a put.
 *    * Each processor spins on the signal waiting to unpack the buffer back
 *      into local computation data.
 *    * Memory reqs: 
 *
 * 2. Non-blocking GASNet (similar to UPC version)
 *
 * 2. GASNet Active-Messages, AMLong
 *    * Pack into buffer send AMLong.
 *    * Receiver runs handler, unpacks the data and increments a counter
 */

typedef
struct _nbour_t {
    int	 dimsz;	/* global dimension size */
    /* 0 => yz plane
     * 1 => xz plane
     * 2 => xy plane (contiguous)
     */
    int  procGrid[3];
    int  idGrid[3];

    /* Upper and Lower neighbours in each dimension (grid id) */
    int  idGridUpper[3];
    int  idGridLower[3];

    /* Upper and Lower neighbours in each dimension (GASNet node ids) */
    gasnet_node_t  nodeidUpper[3];
    gasnet_node_t  nodeidLower[3];

    /* Cache dims in all Lower neighbours */
    int  dimsLower[3];

    /* blocks per grid element in each dimension */
    int  elemsPerDim;
    int  dims[3];
    int  facesz[3]; /* Face sz for each axis in elements */

    uintptr_t totalSize;

    /* Different mechanisms for Ghost exchanges */
    double	*Ldata;	    /* Local computation data */
    uintptr_t	*Dir;	    /* Remote computation data, flattened 3d cube */
    uintptr_t	*Diryz;	    /* Target for yz boundary exchanges */
    uintptr_t	*Dirxz;	    /* Target for xz boundary exchanges */

    uintptr_t	*DirSync;   /* Target sync locations for notifies */
    uintptr_t	*DirSyncComm3;   /* Target sync after comm3 phase */

    /* Target communication buffers for non-contiguous planes */
    double    *yzBuffer;
    double    *xzBuffer; 
    /* xyBuffer requires no packing */

    /* Arrays into communicaiton buffers, for low/up neighbour in each dim */
    double    *dimBufs[3][2];

    /* Two local communication buffers for non-contiguous planes */
    double *yzCommBuffer;
    double *xzCommBuffer;

    /* For AM-long based updates XXX *not* cache friendly for floating funcs */
    int	    amdims[3][2];
    /* Loop iterations to kill time */
    int64_t	loopiters;

    /* For computing medians at node 0 */
    stat_struct_t   *stats0;
}
nbour_t;

/* Only one-level for now */
nbour_t	Nbour;

#define AREF(nb,k,j,i) (nb->Ldata[(k)*(nb)->dims[1]*(nb)->dims[0] + \
		                  (j)*(nb)->dims[0] + i])

#define NBOUR_SYNC_LEN	(1 + CACHELINE_SZ/sizeof(int))
#define NBOUR_SYNC_OFF(axis,id,phase) (NBOUR_SYNC_LEN*(4*(axis)+2*(id)+(phase)))
#define NBOUR_SYNCADDR(base,axis,id,phase) \
        (((volatile int*)(base)) + NBOUR_SYNC_OFF(axis,id,phase))

void setupGrid(nbour_t *nb, int level);
void allocMultiGrid(nbour_t *nb);
void initNbour(nbour_t *nb);
void freeNbour(nbour_t *nb);
void estimateMemSegment(nbour_t *nb, uintptr_t *local, uintptr_t *segment);

void ghostExchUPCMG         (nbour_t *nb, int iters, int axis, int pairwise_sync);
void ghostExchGASNetNonBlock(nbour_t *nb, int iters, int axis, int pairwise_sync);
void ghostExchAMLong        (nbour_t *nb, int iters, int axis);

gasnet_handle_t ge_put   (nbour_t *nb, int type, int dir, int axis, int *flag);
gasnet_handle_t ge_notify(nbour_t *nb, int dir, int axis);
void	        ge_wait  (nbour_t *nb, int dir, int axis);
void	        ge_unpack(nbour_t *nb, double *src, size_t destp, int axis);

void pairwise_signal_neighbours(nbour_t *nb, gasnet_handle_t *h_nbour, int axis_in, int phase);
void pairwise_wait_neighbours  (nbour_t *nb, gasnet_handle_t *h_nbour, int axis_in, int phase);

#define _hidx_ghostReqHandler 201
#define _hidx_ghostRepHandler 202

GASNET_INLINE_MODIFIER(ghostReqHandler_inner)
void
ghostReqHandler_inner(gasnet_token_t token, void *buf, size_t nbytes,
	              int axis, int destp, int *flag)
{
    double *src = (double *)buf;
    int     face = (destp != 0);

    if (axis != AZ)
	ge_unpack(&Nbour, src, destp, axis);
   
    Nbour.amdims[axis][face] = 1;

    return;
}
LONG_HANDLER(ghostReqHandler,3,4,
	     (token,addr,nbytes,a0,a1,UNPACK(a2)),
	     (token,addr,nbytes,a0,a1,UNPACK2(a2,a3)));

/*
 * This reply handler is currently unused.
 */
GASNET_INLINE_MODIFIER(ghostReqHandler_inner)
void
ghostRepHandler_inner(gasnet_token_t token, volatile int *flag)
{
    *flag = 1;
}
SHORT_HANDLER(ghostRepHandler,1,2,
	      (token, UNPACK(a0) ),
	      (token, UNPACK2(a0,a1)));

gasnet_handlerentry_t htable[] = {
    gasneti_handler_tableentry_with_bits(ghostReqHandler),
    gasneti_handler_tableentry_with_bits(ghostRepHandler),
};

void gam_amlong(nbour_t *nb, gasnet_node_t node, int axis, 
	        int srcp, int destp, int *flag);

#define init_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _init_stat
#define update_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _update_stat
#define print_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _print_stat

void _init_stat(nbour_t *nb, stat_struct_t *st, int axis, int dims, int sz)
{
	st->iters = 0;
	st->dims = dims;
	st->datasize = sz;
	st->axis = axis;
	st->time = 0;
	if (st->axis != AALL)
	    st->category = (nb->nodeidUpper[axis] != myproc) + 
		           (nb->nodeidLower[axis] != myproc);
	else
	    st->category = 3;
}

void _update_stat(stat_struct_t *st, uint64_t temptime, int iters)
{
	st->iters += iters;
	st->time += temptime;
} 

void _print_stat(nbour_t *nb, int myproc, stat_struct_t *st, const char *name)
{
	int	i,j,c;
	float	cattimes[4] = { 0.0 };
	int	catcount[4] = { 0 };
	double	stdev[4];
	double  ttime;

	/* Update statistics at zero.
	 * If we are doing a per-axis test, we separate the printed values
	 * within three categories based on the type of neighbour updates that
	 * were completed.
	 *
	 * Updates to Upper/Lower neighbour can be
	 *  1. Global/Global (both updates required communication)
	 *  2. Global/Local or Local/Global (only one update req'd comm).
	 *  3. Local/Local (no updates required communication)
	 *  4. Don't care (either local/global)
	 */
	gasnet_put(0, nb->stats0 + myproc, st, sizeof(stat_struct_t));

	BARRIER();

	if (myproc)
	    return;

	/* Find average in each category of face updates */
	for (i = 0; i < nprocs; i++) {
	    c = nb->stats0[i].category;
	    assert(c >= 0 && c <= 3);
	    cattimes[c] += ((float)nb->stats0[i].time) / nb->stats0[i].iters;
	    catcount[c]++;
	}
	/* Calculate average */
	for (i = 0; i < 4; i++) {
	    if (catcount[i] > 0)
		cattimes[i] /= (float) catcount[i];
	    else
		cattimes[i] = .0;
	}
	/* Calculate stdev for each category*/
	for (c = 0; c < 4; c++) {
	    if (catcount[c] < 2)
		stdev[c] = .0;
	    else {
		double sumsq = .0;
		double devmean = .0;
		double procavg;
		double divm;
		for (i = 0; i < nprocs; i++) {
		    if (nb->stats0[i].category != c)
			continue;
		    procavg = ((double)nb->stats0[i].time)/nb->stats0[i].iters;
		    devmean = (double)cattimes[c] - procavg;
		    sumsq += devmean*devmean; /*pow((double)cattimes[c] - procavg, 2.0);*/
		}
		divm = sumsq / (catcount[c]-1);
		stdev[c] = sqrt(divm);
	    }
	}

	if (catcount[3] > 0) {
	    /* Don't care about various global/local distinctions */
	    printf("DIM %4i fullexch %8i byte : %5i iters, %9.2f +/- %8.2f us ave (%s)\n",
	     st->dims, st->datasize, st->iters, cattimes[3], stdev[3], name);

	}
	else {
	    printf("DIM %4i  axis %c %8i byte : %5i iters, %9.2f +/- %8.2f us ave (%s)\n",
	      st->dims, 'x'+st->axis, st->datasize, st->iters, cattimes[2], stdev[2], name
	    );
	}
	if (nbour_fp != NULL) {
	    int cat = catcount[3] > 0 ? 3 : 2;
	    fprintf(nbour_fp, "%-11s %c %4i %8i %9.2f %8.2f ",
	        name, cat == 3 ? 'F' : st->axis + 'x', st->dims, st->datasize, 
		cattimes[cat], stdev[cat]);
	    for (i = 0; i < nprocs; i++) {
	        if (nb->stats0[i].category != cat) {
		    ttime = .0;
		}
		else
		    ttime = ((float)nb->stats0[i].time) / nb->stats0[i].iters;
		fprintf(nbour_fp, " %9.2f", ttime);
	    }
	    fprintf(nbour_fp, "\n");
	    fflush(nbour_fp);
	}

	fflush(stdout);
}

int level_dims[][20] = {	
	{ 16,32,48,64,80,96,112,128,0 },
	{ 16,32,48,64,80,96,112,128,144,160,176,192,208,224,240,256,0 },
	{ 32,64,96,128,160,192,224,256,288,320,352,384,416,448,480,512,0 },
	{ 64,128,192,256,320,384,448,512,576,640,704,768,832,896,960,1024,0 }
};

void
usage()
{
    if (myproc != 0)
	return;

    printf("\ntestneighbour Neighbour-to-Neighbour microbenchmark\n\n");
    printf("testneighbour [-f] [iters] [level]\n\n");
    printf("-f      run full neighbour exchange (NAS MG) instead of per axis\n");
    printf("[iters] How many iterations per exchange (default = 150)\n");
    printf("[level] select level of dimensions (default level = 0)\n");
    printf("   level=0 dims=<16,32,48,64,80,96,112,128>\n");
    printf("   level=1 dims=<16,32,48,64, .. 128,160,192,224,256>\n");
    printf("   level=2 dims=<32,64,96,128, .. 320,352,384,416,448,480,512>\n");
    printf("   level=3 dims=<32,64,96,128, .. 928,960,992,1024>\n\n");
    fflush(stdout);
    gasnet_exit(1);
}

int
main(int argc, char **argv)
{
    int	level = 0, i;
    int alldimensions = 1;
    char *nbourf;
    uintptr_t insegsz, outsegsz;
    int iters = 150;
    int dim;
    int maxdim = 0;
    int axis;
    int argn = 1;
    void *myseg;
    void *alloc;

    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));

    /* get SPMD info */
    myproc = gasnet_mynode();
    nprocs = gasnet_nodes();

    /* XXX parse args: iters min max */
    if (argc > argn) {
	if (*argv[argn] == '-') {
	    if (argv[argn][1] == 'f') 
		alldimensions = 0;
	    else {
		usage();
		gasnet_exit(1);
	    }
	    argn++;
	}
	if (argc > argn) {
	    iters = atoi(argv[argn++]);
	    if (!iters)
		usage();
	}
	if (argc > argn) {
	    level = atoi(argv[argn++]);
	    if (!(level >= 0 && level < 4))
		usage();
	}
    }

    for (i = 0; i < level_dims[level][i]; i++) 
	maxdim = MAX(level_dims[level][i], maxdim);

    if (!POWER_OF_TWO(nprocs)) {
	fprintf(stderr, "%s only runs on a power of two processors\n", argv[0]);
	gasnet_exit(1);
    }

    if (!myproc)
	print_testname("testneighbour", nprocs);

    /* setup max grid we intend to use, so we can get enough 
     * memory per proc at startup */
    setupGrid(&Nbour, maxdim);
    estimateMemSegment(&Nbour, &insegsz, &outsegsz);
    maxsegmentsz = outsegsz + PAGESZ*nprocs;

    GASNET_Safe(gasnet_attach(
	htable, sizeof(htable)/sizeof(gasnet_handlerentry_t),
	TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

    TEST_DEBUGPERFORMANCE_WARNING();

    initNbour(&Nbour);

    BARRIER();

    /* Run test over all axes:
     * 0 -> x: yz planes
     * 1 -> y: xz planes
     * 2 -> z: xy planes
     * 3 -> x,y,z Full MG-like Neighbour exchange
     */
    /* We may want to gather extended info in a file */
    if (!myproc && (nbourf = gasnet_getenv("NBOURTEST_FILE")) != NULL) {
	nbour_fp = fopen(nbourf, "w");
	if (nbour_fp == NULL) {
	    fprintf(stderr, "Can't open NBOURTEST_FILE %s\n", nbourf);
	    gasnet_exit(1);
	}
	printf("Saving extended output to %s\n", nbourf);
    }
    else
	nbour_fp = NULL;

    if (!myproc) {
        printf("\ntestneighbour running %d %s"
	       " (%d procs over processor grid = %2i x %2i x %2i)\n",
		iters, alldimensions ? "ghost exchanges per axis" :
		                       "full (NAS MG-like) ghost exchanges",
		nprocs, Nbour.procGrid[0], Nbour.procGrid[1], Nbour.procGrid[2]);

	printf(
	       "\nReported times are the medians across all processors only"
	       " for ghost exchanges that incur network communication\n");
    }

    BARRIER();


    if (alldimensions) {
	for (axis = 0; axis <= 2; axis++) {

	    if (!myproc) {
		if (axis == 2)
		    printf("\nExchange over 'z' contiguous axis, grid = %d procs\n",
			    Nbour.procGrid[2]);
		else
		    printf("\nExchange over '%c' non-contiguous axis, grid = "
			   "%d procs (DIM%s x stride %s)\n", 'x' + axis,
			   Nbour.procGrid[axis], axis==0 ? "^2" : "",
						 axis==0 ? "DIM" : "1");
		fflush(stdout);
	    } 

	    BARRIER();

	    for (i = 0; level_dims[level][i] != 0; i++) {
		dim = level_dims[level][i];
		setupGrid(&Nbour, dim);
		allocMultiGrid(&Nbour);
		BARRIER();
		/* In the alldimensions test, run only the non-blocking
		 * pairwise and the AMLong versions */
		ghostExchUPCMG(&Nbour, 1, axis, 0); /* Dry run */
		ghostExchUPCMG(&Nbour, iters, axis, 0);
	    }
	    BARRIER();

	    for (i = 0; level_dims[level][i] != 0; i++) {
		dim = level_dims[level][i];
		setupGrid(&Nbour, dim);
		allocMultiGrid(&Nbour);
		BARRIER();
		/* In the alldimensions test, run only the non-blocking
		 * pairwise and the AMLong versions */
		ghostExchGASNetNonBlock(&Nbour, 1, axis, 1); /* Dry run */
		ghostExchGASNetNonBlock(&Nbour, iters, axis, 1);
	    }
	    BARRIER();

	    for (i = 0; level_dims[level][i] != 0; i++) {
		dim = level_dims[level][i];
		setupGrid(&Nbour, dim);
		allocMultiGrid(&Nbour);
		BARRIER();
		ghostExchAMLong(&Nbour, 1, axis); /* Dry run */
		ghostExchAMLong(&Nbour, iters, axis);
	    }
	}
    }
    else {
	axis = 3;   /* Full ghost exchange, no individual axis */

	BARRIER();

	for (i = 0; level_dims[level][i] != 0; i++) {
	    dim = level_dims[level][i];
	    setupGrid(&Nbour, dim);
	    allocMultiGrid(&Nbour);
	    BARRIER();
	    ghostExchUPCMG(&Nbour, 1, axis, 0); /* Dry run */
	    ghostExchUPCMG(&Nbour, iters, axis, 0);
	}
	BARRIER();

	for (i = 0; level_dims[level][i] != 0; i++) {
	    dim = level_dims[level][i];
	    setupGrid(&Nbour, dim);
	    allocMultiGrid(&Nbour);
	    BARRIER();
	    ghostExchGASNetNonBlock(&Nbour, 1, axis, 1); /* Dry run */
	    ghostExchGASNetNonBlock(&Nbour, iters, axis, 1);
	}
	BARRIER();

	for (i = 0; level_dims[level][i] != 0; i++) {
	    dim = level_dims[level][i];
	    setupGrid(&Nbour, dim);
	    allocMultiGrid(&Nbour);
	    BARRIER();
	    ghostExchAMLong(&Nbour, 1, axis); /* Dry run */
	    ghostExchAMLong(&Nbour, iters, axis);
	}
    }

    freeNbour(&Nbour);
    BARRIER();

    if (nbour_fp != NULL)
	fclose(nbour_fp);

    gasnet_exit(0);

    return 0;
}

void
setupGrid(nbour_t *nb, int dimsz)
{
    int t_grid = 1;
    int axis;
    int elemsPerDim, totelemsPerDim;

    nb->procGrid[0] = 1;
    nb->procGrid[1] = 1;
    nb->procGrid[2] = 1;

    /* setup the processor grid */
    while (t_grid*2 <= nprocs) {
	nb->procGrid[0] *= 2;
	t_grid *= 2;
	if (t_grid*2 <= nprocs) {
	    nb->procGrid[1] *= 2;
	    t_grid *= 2;
	    if (t_grid*2 <= nprocs) {
		nb->procGrid[2] *= 2;
		t_grid *= 2;
	    }
	}
    }
    assert(t_grid == nprocs);

    /* Setup the proc id in the grid */
    t_grid = myproc;
    nb->idGrid[0] = (myproc % (nb->procGrid[0]*nb->procGrid[1])) 
	                      % nb->procGrid[0];
    nb->idGrid[1] = (myproc % (nb->procGrid[0]*nb->procGrid[1])) 
	                      / nb->procGrid[0];
    nb->idGrid[2] =  myproc/(nb->procGrid[0]*nb->procGrid[1]);

    /* Setup the number of blocks per grid element in each dimension. Total
     * elements per dimension contains an extra two boundary elements */
    nb->elemsPerDim = elemsPerDim = dimsz;/*(2<<(unsigned)level);*/
    totelemsPerDim = elemsPerDim + 2;
    nb->totalSize = 1;

    /* Setup lower and upper neighbours in each dimension */
    for (axis = 0; axis <= 2; axis++) {
	int blocksz = elemsPerDim / nb->procGrid[axis];

	/* We don't handle corner cases, yet */
	assert(blocksz > 0);
	assert(elemsPerDim > nb->procGrid[axis]);
	assert(elemsPerDim % nb->procGrid[axis] == 0);

	nb->idGridUpper[axis] = 
	    nb->idGrid[axis] == nb->procGrid[axis]-1 
	                      ? 0 : nb->idGrid[axis]+1;
	nb->idGridLower[axis] = 
	    nb->idGrid[axis] == 0 
	    ? nb->procGrid[axis]-1 : nb->idGrid[axis]-1;

	/* Now map the grid onto actual nodes */
	switch (axis) {
	    case 0: /* X axis */
		nb->nodeidUpper[0] = LINEARIZEPROC(nb,
			    nb->idGrid[2],nb->idGrid[1],nb->idGridUpper[0]);
		nb->nodeidLower[0] = LINEARIZEPROC(nb,
			    nb->idGrid[2],nb->idGrid[1],nb->idGridLower[0]);
		break;
	    case 1: /* Y axis */
		nb->nodeidUpper[1] = LINEARIZEPROC(nb,
			    nb->idGrid[2],nb->idGridUpper[1],nb->idGrid[0]);
		nb->nodeidLower[1] = LINEARIZEPROC(nb,
			    nb->idGrid[2],nb->idGridLower[1],nb->idGrid[0]);
		break;
	    case 2: /* Z axis */
		nb->nodeidUpper[2] = LINEARIZEPROC(nb,
			    nb->idGridUpper[2],nb->idGrid[1],nb->idGrid[0]);
		nb->nodeidLower[2] = LINEARIZEPROC(nb,
			    nb->idGridLower[2],nb->idGrid[1],nb->idGrid[0]);
		break;

	    default:
		break;
	}

	/* Don't forget boundary elements in each dimension */
	blocksz += 2;

	/* XXX assumption of equal block distribution */
	nb->dimsLower[axis] = blocksz;
	
	nb->totalSize *= (long) blocksz;
	nb->dims[axis] = blocksz;
    }

    nb->facesz[0] = nb->dims[1]*nb->dims[2];
    nb->facesz[1] = nb->dims[0]*nb->dims[2];
    nb->facesz[2] = nb->dims[0]*nb->dims[1];

    nb->dimsz = dimsz;

    if (0) {
	fprintf(stdout,
	    "%2d> level %2d [%1d,%1d,%1d] in grid [%1d,%1d,%1d] has "
	    "[%1d,%1d],[%1d,%1d],[%1d,%1d] OR"
	    "[%1d,%1d],[%1d,%1d],[%1d,%1d]\n",
	    myproc, dimsz,
	    nb->idGrid[0], nb->idGrid[1], nb->idGrid[2],
	    nb->procGrid[0], nb->procGrid[1], nb->procGrid[2],
	    nb->idGridLower[0], nb->idGridUpper[0],
	    nb->idGridLower[1], nb->idGridUpper[1],
	    nb->idGridLower[2], nb->idGridUpper[2],
	    nb->nodeidLower[0], nb->nodeidUpper[0],
	    nb->nodeidLower[1], nb->nodeidUpper[1],
	    nb->nodeidLower[2], nb->nodeidUpper[2]);

    }
}

/*
 * Estimate segment memory requirements for parry's ghost */
void 
estimateMemSegment(nbour_t *nb, uintptr_t *local, uintptr_t *segment)
{
    uintptr_t outseg = 0;
    uintptr_t inseg = 0;

    outseg += /* local xz and yz comm buffers, 2 boundaries each */
	      (nb->dims[0]*nb->dims[2]*2 + nb->dims[1]*nb->dims[2]*2) *
	      sizeof(double);
    
    outseg += /* per-processor directories: Dir, Dirxy, Dirxz, 
		                     Diryz, DirSync, DirSyncComm3 */
	      nprocs*6*sizeof(uintptr_t);

    inseg  += /* sync flags for each cube face, on a separate cache line */
	      (sizeof(int)*8*2*2*NBOUR_SYNC_LEN);

    inseg  += /* xz,yz and xy target comm buffers, 2 boundaries each */
	      (PX_SZ+PY_SZ+PZ_SZ)*2*sizeof(double);

    inseg  += /* comm buffers for non-contiguous planes */
	      (PX_SZ+PY_SZ)*2*sizeof(double);

    inseg  += /* Actual computation data, page aligned */
	      alignup((uintptr_t)(nb->totalSize * sizeof(double)), PAGESZ);
    inseg  += /* room for stats */
	      sizeof(stat_struct_t) * nprocs;

    *local = outseg;
    *segment = inseg;

    return;
}

void
freeNbour(nbour_t *nb)
{
    free(nb->Dir);
    free(nb->Diryz);
    free(nb->Dirxz);
    free(nb->DirSync);
    free(nb->DirSyncComm3);
}

void
initNbour(nbour_t *nb)
{
    nb->Dir   = (uintptr_t *) calloc(nprocs, sizeof(uintptr_t));
    nb->Diryz = (uintptr_t *) calloc(nprocs, sizeof(uintptr_t));
    nb->Dirxz = (uintptr_t *) calloc(nprocs, sizeof(uintptr_t));
    nb->DirSync = (uintptr_t *) calloc(nprocs, sizeof(uintptr_t));
    nb->DirSyncComm3 = (uintptr_t *) calloc(nprocs, sizeof(uintptr_t));
    return;
}

/*
 * Carve out our segment according to the grid dimensions currently set in Nb
 */
void
allocMultiGrid(nbour_t *nb)
{
    int i;
    char *segaddr;

    for (i = 0; i < nprocs; i++) {
	/* segaddr points to beginning of shared GASNet segment */
	segaddr = (char *) TEST_SEG(i);

	/* Common to parry and amlong approaches */
	nb->Dir[i] = (uintptr_t) segaddr;
	segaddr += alignup(nb->totalSize * sizeof(double), PAGESZ);
	nb->Diryz[i] = (uintptr_t) segaddr;
	segaddr += 2*PX_SZ*sizeof(double);
	nb->Dirxz[i] = (uintptr_t) segaddr;
	segaddr += 2*PY_SZ*sizeof(double);

	if (myproc == i) {
	    nb->dimBufs[0][0] = nb->yzBuffer = (double *) nb->Diryz[i];
	    nb->dimBufs[0][1] = nb->dimBufs[0][0] + PX_SZ;
	    nb->dimBufs[1][0] = nb->xzBuffer = (double *) nb->Dirxz[i];
	    nb->dimBufs[1][1] = nb->dimBufs[1][0] + PY_SZ;
	    nb->Ldata = (double *) nb->Dir[i];
	}

	if (myproc == i)
	    nb->yzCommBuffer = (double *) segaddr;
	segaddr += 2*PX_SZ*sizeof(double);
	if (myproc == i)
	    nb->xzCommBuffer = (double *) segaddr;
	segaddr += 2*PY_SZ*sizeof(double) ;

	/* Dirsync requires counters on separate cache lines */
	segaddr += NBOUR_SYNC_LEN*sizeof(int);
	nb->DirSync[i] = (uintptr_t) segaddr;
	segaddr += 8*NBOUR_SYNC_LEN*sizeof(int);
	nb->DirSyncComm3[i] = (uintptr_t) segaddr;
	segaddr += 8*NBOUR_SYNC_LEN*sizeof(int);
	if (i == 0) {/* save address for stats at 0 */
	    nb->stats0 = (stat_struct_t *) segaddr;
	    segaddr += sizeof(stat_struct_t)*nprocs;
	}
	if (myproc == i) {
	    topalloc = (uintptr_t) segaddr;
	    if (topalloc >= (uintptr_t) TEST_SEG(myproc) + TEST_SEGSZ) {
		fprintf(stderr, "DIM %d too large for segment\n", nb->dimsz);
		gasnet_exit(1);
	    }
	}
    }

}

void
ge_unpack(nbour_t *nb, double *src, size_t destp, int axis)
{
    int n,i,j,k;
    int dk = nb->dims[2];
    int dj = nb->dims[1];
    int di = nb->dims[0];

    n=0;
    switch (axis) {
      case 0:	/* X axis, yz plane, n * stride n */
        for (k=0; k < dk; k++)
	    for (j = 0; j < dj; j++)
	        AREF(nb,k,j,destp) = src[n++];
	break;
      case 1:	/* Y axis, xz plane, n * stride 1 */
	for (k=0; k < dk; k++)
	    for (i=0; i < di; i++)
		AREF(nb,k,destp,i) = src[n++];
	break;
      case 2: /* Z axis, xy plane, 1 * stride 1 */
	break;
      default:
	break;
    }
    return;
}


/*
 * Parry uses UPC-level shared directories to propagate the location of
 * per-thread communication buffers.
 *
 * if (axis == AALL), do all axis (full ghost exchange)
 *
 */
void 
ghostExchUPCMG(nbour_t *nb, int iters, int axis_in, int pairwise_sync)
{
    int i, j, axis, dest;
    int axis_tot;

    uint64_t	    begin, end;
    stat_struct_t   stcomm3;
    int		    axes[3];
    gasnet_handle_t hput;

    if (axis_in == AALL) {
	axes[0] = 0; axes[1] = 1; axes[2] = 2;
	axis_tot = 3;
	init_stat(nb, &stcomm3, axis_in, nb->dimsz, (PX_SZ+PY_SZ+PZ_SZ)*sizeof(double)*2);
    }
    else {
	axes[0] = axis_in;
	axis_tot = 1;
	init_stat(nb, &stcomm3, axis_in, nb->dimsz, nb->facesz[axis_in]*sizeof(double)*2);
    }

    BARRIER();

    for (i = 0; i < iters; i++) {
	begin = TIME();
	for (j = 0; j < axis_tot; j++) {
	    axis = axes[j];

	    /* Send data to upper and lower neighbour, in turn */
	    hput = ge_put(nb, GHOST_TYPE_PUT, GHOST_DIR_UPPER, axis, NULL);
	    if (hput != GASNET_INVALID_HANDLE) {
		gasnet_wait_syncnb(hput);
		gasnet_wait_syncnb( ge_notify(nb, GHOST_DIR_UPPER, axis) );
		ge_wait(nb, GHOST_DIR_UPPER, axis);
	    }

	    hput = ge_put(nb, GHOST_TYPE_PUT, GHOST_DIR_LOWER, axis, NULL);
	    if (hput != GASNET_INVALID_HANDLE) {
		gasnet_wait_syncnb(hput);
		gasnet_wait_syncnb( ge_notify(nb, GHOST_DIR_LOWER, axis) );
		ge_wait(nb, GHOST_DIR_LOWER, axis);
	    }
	}
	end = TIME();
	BARRIER(); /* don't include the barrier time */
	update_stat(&stcomm3, (end-begin), 1);
    }

    if (iters > 1) {
	print_stat(nb, myproc, &stcomm3, "UPC-MG");
    }

    BARRIER();

    return;
}

/*
 * Parry uses UPC-level shared directories to propagate the location of
 * per-thread communication buffers.
 */
void 
ghostExchGASNetNonBlock(nbour_t *nb, int iters, int axis_in, int pairwise_sync)
{
    unsigned int i, j, axis, dest, face;
    volatile int *sync;

    uint64_t	    begin, end;
    stat_struct_t   stcomm3;
    gasnet_handle_t hput[2];
    gasnet_handle_t sput[6];
    int		    *syncaddr;
    int		    phase = 1;

    int	    axes[3];
    int	    axis_tot;

    int	    sfaces, sfacedone[2];
    int	    rfaces, rfacedone[2];
    int	    sent;

    if (axis_in == AALL) {
	/* Here we start with axis 'z' since it's the contiguous one and will
	 * overlap subsequent non-contiguous axis that require packing */
	axes[0] = 0; axes[1] = 1; axes[2] = 2;
	axis_tot = 3;
	init_stat(nb, &stcomm3, axis_in, nb->dimsz, (PX_SZ+PY_SZ+PZ_SZ)*sizeof(double)*2);
    }
    else {
	axes[0] = axis_in;
	axis_tot = 1;
	init_stat(nb, &stcomm3, axis_in, nb->dimsz, nb->facesz[axis_in]*sizeof(double)*2);
    }

    BARRIER();

    for (i = 0; i < iters; i++) {

	sent = 0;

	begin = TIME();

	for (j = 0; j < axis_tot; j++) {
	    axis = axes[j];

	    /* Both lower and upper faces can proceed independently */
	    hput[1] = ge_put(nb, GHOST_TYPE_PUT, GHOST_DIR_LOWER, axis, NULL);
	    hput[0] = ge_put(nb, GHOST_TYPE_PUT, GHOST_DIR_UPPER, axis, NULL);

	    /* Mark locally completed puts as done */
	    rfacedone[0] = sfacedone[0] = (hput[0] == GASNET_INVALID_HANDLE);
	    rfacedone[1] = sfacedone[1] = (hput[1] == GASNET_INVALID_HANDLE);
	    rfaces = sfaces = sfacedone[0] + sfacedone[1];

	    /*
	     * Poll until either one of these conditions are fulfilled:
	     *   1. A notify is received (other proc is done with update)
	     *   2. A non-blocking put is completed.
	     */
	    while (!(sfaces == 2 && rfaces == 2)) {

		/* Any of upper or lower face is complete ? */
		for (face=0; face<2; face++) {
		    if (rfacedone[face])
			continue;
		    sync = NBOUR_SYNCADDR(nb->DirSync[myproc], axis, face, 0);
		    if (*sync != 0) {
			/* Unless the axis is contiguous, unpack data */
			if (axis != AZ) {
			    ge_unpack(nb, nb->dimBufs[axis][face], 
				      face ? nb->dims[axis]-1 : 0, axis);
			}
			*sync = 0;
			rfacedone[face] = 1;
			rfaces++;
		    }
		}

		/* Any of *our* ghost exchanges complete ? */
		if (gasnet_try_syncnb_some(hput, 2) == GASNET_ERR_NOT_READY)
		    continue;

		/* Which face has completed */
		for (face=0; face<2; face++) {
		    /* Unless the face is done or not ready, skip it */
		    if (sfacedone[face] || hput[face] != GASNET_INVALID_HANDLE)
			continue;
		    sput[sent] = ge_notify(nb, face ? GHOST_DIR_LOWER 
				                    : GHOST_DIR_UPPER, axis);
		    sfacedone[face] = 1;
		    sfaces++;
		    sent++;
		}
	    }
	    /* When the loop ends, we've received face updates from both
	     * neighbours */
	}

	end = TIME();
	update_stat(&stcomm3, (end-begin), 1);
	/* We don't time the sync here since it's essentially free.  The notify
	 * is simply a non-blocking signal, we don't care when it completes
	 * locally.
	 */
	gasnet_wait_syncnb_all(sput, sent);
	BARRIER();
    }

    if (iters > 1) {
	print_stat(nb, myproc, &stcomm3, "GASNet-NB");
    }

    BARRIER();

    return;
}

/*
 * Pairwise sync with neighbours.
 *
 * It's currently unused in all three versions of neighbour exchanges.
 */
void
pairwise_signal_neighbours(nbour_t *nb, gasnet_handle_t *h_nbour, int axis_in, int phase)
{
    int	    i, axis, axis_tot;
    int	    axes[3];
    int	    destup, destdown;

    if (axis_in == AALL) {
	axis_tot = 3;
	axes[0] = 0; axes[1] = 1; axes[2] = 2;
    }
    else {
	axis_tot = 1;
	axes[0] = axis_in;
    }

    for (i = 0; i < axis_tot; i++) {

	axis = axes[i];
	destup   = nb->nodeidUpper[axis];
	destdown = nb->nodeidLower[axis];

	h_nbour[i*2+0] = gasnet_put_nb_val(destup, 
	    (void *) NBOUR_SYNCADDR(nb->DirSyncComm3[destup], axis, 1, phase), 
	    1, sizeof(int));

	h_nbour[i*2+1] = gasnet_put_nb_val(destdown, 
	    (void *) NBOUR_SYNCADDR(nb->DirSyncComm3[destdown], axis, 0, phase), 
	    1, sizeof(int));
    }
}

void
pairwise_wait_neighbours(nbour_t *nb, gasnet_handle_t *h_nbour, int axis_in, int phase)
{
    int	    i, axis, axis_tot;
    int	    nfaces;
    int	    faces = 0;
    int	    axes[3];
    volatile int    *sync;

    if (axis_in == AALL) {
	nfaces = 6;
	axes[0] = 0; axes[1] = 1; axes[2] = 2;
	axis_tot = 3;
    }
    else {
	nfaces = 2;
	axes[0] = axis_in;
	axis_tot = 1;
    }

    /* Reap our previous phase handles and poll on local signals */
    gasnet_wait_syncnb_all(h_nbour, nfaces);

    do {
	faces = 0;
	gasnet_AMPoll();
	for (i = 0; i < axis_tot; i++) {
	    axis = axes[i];
	    sync = NBOUR_SYNCADDR(nb->DirSyncComm3[myproc], axis, 0, phase);
	    if (*sync) faces++;
	    sync = NBOUR_SYNCADDR(nb->DirSyncComm3[myproc], axis, 1, phase);
	    if (*sync) faces++;
	}
    } while (faces < nfaces);

    /* Reset signal locations for current phase */
    for (i = 0; i < axis_tot; i++) {
	axis = axes[i];
	sync = NBOUR_SYNCADDR(nb->DirSyncComm3[myproc], axis, 0, phase);
	*sync = 0;
	sync = NBOUR_SYNCADDR(nb->DirSyncComm3[myproc], axis, 1, phase);
	*sync = 0;
    }
}

void
ghostExchAMLong(nbour_t *nb, int iters, int axis_in)
{
    int i, j, axis, dest, axis_tot;
    int ghostexchUpper[3];
    int ghostexchLower[3];
    long maxmsg = 0;

    int	axes[3];

    uint64_t	    begin, end;
    stat_struct_t   stcomm3;

    if (axis_in == AALL) {
	axes[0] = 0; axes[1] = 1; axes[2] = 2;
	axis_tot = 3;
	init_stat(nb, &stcomm3, axis_in, nb->dimsz, (PX_SZ+PY_SZ+PZ_SZ)*sizeof(double)*2);
    }
    else {
	axes[0] = axis_in;
	axis_tot = 1;
	init_stat(nb, &stcomm3, axis_in, nb->dimsz, nb->facesz[axis_in]*sizeof(double)*2);
    }

    /* skip this test if dimensions are larger than AMLong.  This test does not
     * require global coordination since the dimensions are split up equally. */
    for (i = 0; i < axis_tot; i++) 
	maxmsg = MAX(maxmsg, nb->facesz[i]*sizeof(double));

    if (maxmsg > gasnet_AMMaxLongRequest()) {
	if (!myproc) {
	    printf("Skipping AMLong with dim=%d (%ld > AMMaxLongRequest())\n",
			nb->dimsz, maxmsg);
	    fflush(stdout);
	}
	return;
    }

    BARRIER();

    for (i = 0; i < iters; i++) {

	nb->amdims[0][0] = nb->amdims[0][1] = 
	nb->amdims[1][0] = nb->amdims[1][1] = 
	nb->amdims[2][0] = nb->amdims[2][1] =  0;

	BARRIER();

	begin = TIME();

	for (j = 0; j < axis_tot; j++) {
	    axis = axes[j];
	    ge_put(nb, GHOST_TYPE_AMLONG, GHOST_DIR_UPPER, axis, 
		   &nb->amdims[axis][0]); 
	    ge_put(nb, GHOST_TYPE_AMLONG, GHOST_DIR_LOWER, axis, 
		   &nb->amdims[axis][1]); 
	    /* Wait until the face update is completed */
	    while (!(nb->amdims[axis][0] && nb->amdims[axis][1]))
		    gasnet_AMPoll();
	    nb->amdims[axis][0] = nb->amdims[axis][1] = 0;
	}
	end = TIME();

	update_stat(&stcomm3, (end-begin), 1);
	BARRIER();
    }

    if (iters > 1) {
	print_stat(nb, myproc, &stcomm3, "AMLongAsync");
    }

    BARRIER();

    return;
}

gasnet_handle_t
ge_put(nbour_t *nb, int type, int dir, int axis, int *flag)
{
    int	n=0,i,j,k;
    int dk = nb->dims[2];
    int dj = nb->dims[1];
    int di = nb->dims[0];

    double  *src, *dest;
    int	    srcp, destp;
    size_t  len;

    gasnet_node_t   node;
    
    if (dir == GHOST_DIR_UPPER) {
	node = nb->nodeidUpper[axis];
	srcp = nb->dims[axis]-2;
	destp = 0;
    }
    else { /* GHOST_DIR_LOWER */
	node = nb->nodeidLower[axis];
	srcp = 1;
	destp = nb->dimsLower[axis]-1;
    }

    /* take care of both axes that require packing */
    switch(axis) {
	case AX:
	    if (node == myproc) {
		for (k=0; k < dk; k++)
		    for (j=0; j < dj; j++)
			AREF(nb,k,j,destp) = AREF(nb,k,j,srcp);
		goto local_copy;
	    }
	    else {
		for (k = 0; k < dk; k++) 
		    for (j = 0; j < dj; j++) 
			nb->yzCommBuffer[n++] = AREF(nb,k,j,srcp);
		src = nb->yzCommBuffer;
		dest = (double *) nb->Diryz[node] + (destp!=0)*PX_SZ;
		len = PX_SZ*sizeof(double);
		/* send packed buf */
	    }
	    break;
	case AY:
	    if (node == myproc) {
		for (k = 0; k < dk; k++)
		    for (i = 0; i < di; i++)
			AREF(nb,k,destp,i) = AREF(nb,k,srcp,i);
		goto local_copy;
	    }
	    else {
		for (k = 0; k < dk; k++)
		    for (i = 0; i < di; i++)
			nb->xzCommBuffer[n++] = AREF(nb,k,srcp,i);
		src = nb->xzCommBuffer;
		dest = (double *) nb->Dirxz[node] + (destp!=0)*PY_SZ;
		len = PY_SZ*sizeof(double);
		/* send packed buf */
	    }
	    break;
	case AZ:
	    src  = (double *) nb->Ldata + srcp*dj*di;
	    dest = (double *) nb->Dir[node] + destp*PZ_SZ; 
	    len = PZ_SZ*sizeof(double);
	    if (node == myproc) {
		memcpy(dest,src,PZ_SZ*sizeof(double));
		goto local_copy;
	    }
	    break;
	default:
	    break;
    }

    if (type == GHOST_TYPE_AMLONG) {
	/* By now, send an AMLong with data */
	LONGASYNC_REQ(3,4,(node,gasneti_handleridx(ghostReqHandler),src,len,dest,
			   axis,destp,PACK(flag)));
	return GASNET_INVALID_HANDLE;
    }
    else {
	return gasnet_put_nb_bulk(node, dest, src, len);
    }

local_copy:
    if (type == GHOST_TYPE_AMLONG)
	*flag = 1;
    return GASNET_INVALID_HANDLE;
}

gasnet_handle_t
ge_notify(nbour_t *nb, int dir, int axis)
{
    int islower = (dir == GHOST_DIR_LOWER);
    int node = islower ? nb->nodeidLower[axis] : nb->nodeidUpper[axis];
    volatile int *sync = NBOUR_SYNCADDR(nb->DirSync[node], axis, islower, 0);

    if (node == myproc) {
	*sync = 1;
	return GASNET_INVALID_HANDLE;
    }
    else
	return gasnet_put_nb_val(node, (void *)sync, 1, sizeof(int));
}


void
ge_wait(nbour_t *nb, int dir, int axis)
{
    int islower = (dir == GHOST_DIR_LOWER);
    volatile int *syncaddr = NBOUR_SYNCADDR(nb->DirSync[myproc], axis, islower, 0);
    int destp = islower ? nb->dims[axis]-1 : 0;
    double *src = nb->dimBufs[axis][dir];

    while (*syncaddr == 0) 
    { gasnet_AMPoll(); }

    /* Unless the axis is contiguous, unpack received data */
    if (axis != AZ)
	ge_unpack(nb, src, destp, axis);

    *syncaddr = 0;
    return;
}

