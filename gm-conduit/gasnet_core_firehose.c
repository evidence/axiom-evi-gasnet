/* ------------------------------------------------------------------------ */
/* Firehose                                                                 */
/* ------------------------------------------------------------------------ */
/*
The idea behind firehose resides in reserving a portion of the amount of
pinnable pages to every node on the cluster, allowing each node to manage its
number of pinnable pages in any way it wants.

Firehose works using the following parameters:
M: amount of local memory we try to guarentee as pinnable
   - n.b. Means to "guarentee" the memory is still unclear. . either we pin it
     all or we trust the system until we are out of physical memory (the
     second approach is probably sufficient if M is a smaller fraction of the
     amount of physical memory and/or system load).
B: bucket size, a multiple of the page size, defines a region of memory that
   can be used for DMAs if pinned
N: the amount of nodes in the job
F: a firehose is a local handle to a remote _pinned_ bucket
   Each node posesses F = M/(B*(N-1)) firehoses that it can use on N-1 nodes

For example, a 16 node job (N) that uses 4 4k pages for bucket size (B) over
512 MB of pinnable memory (M) on a 32-bit architecture posesses
    F = 512M/( 4*4k*15 ) = 2184 firehoses per node

F is significant both for local buckets and firehoses (remotely "owned" buckets)
- locally, the node will have to manage up to 2184*15 = 32760 buckets which
  could be mapped to firehoses all over the job.
- remotely, for firehoses the node owns to other buckets, the node will need
  to store metadata for up to 2184 firehoses on 15 nodes.

The amount of metadata put aside for the algorithm scales with the value of M,
which in turn should scale with the amount of physical memory on the machine.
As such, the firehose algorithm is scalable.
*/

/* ------------------------------------------------------------------------ */
/* Buckets and Bucket Descriptors
Each bucket needs a descriptor and two types of descriptors must be created for
- Local Buckets, where M/B bucket descriptors are initialized at startup to
  support all the buckets that could be pinned in the local node's memory
  space.
- Firehoses (Remote Buckets), where M/(B*(N-1)) descriptors are initialized
  for (N-1) nodes at startup to support the maximum number of firehoses (M/B)
  that may be mapped to remote firehoses.
*/

/* ------------------------------------------------------------------------ */
/* Bucket Descriptor reference count
Each bucket descriptor has a reference count, which is used differently for
local buckets and firehoses:
- Local Buckets keep a reference count to know how many remote nodes expect
  the bucket to be pinned.  In other words, how many other nodes own firehoses
  to the local bucket.  At initialization, the reference count for each bucket
  descriptor is set to REFCOUNT_INIT, meaning the bucket is not pinned.  Once
  a first node requests the bucket to be pinned, its reference count is bumped
  up to 1.  Other nodes are free to move one of their firehoses to/from this
  bucket as the operation on refcount will be respectively
  increment/decrement.  Once the refcount reaches zero, we are free to unpin
  the page or manage it separately - see below for more details.
- Firehoses keep a refcount to prevent a race condition.  When the local node
  needs RDMA access on a remote node, it sends a list of old and new bucket
  addresses.  To the node receiving the firehose move request, it is simply a
  matter of decrementing/incrementing the refcount on old/new buckets.
  However, to prevent other threads/processes on the local node from moving a
  firehose another thread expects to use, refcount is used for the concerned
  bucket descriptors to indicate that a move_firehose/dma operation is in
  progress.  In other words, the refcount for firehoses simply locks the
  bucket from being unpinned by other threads/processes running on the same
  node.
*/

/* ------------------------------------------------------------------------ */
/*
We need two separate types of lookup tables:
- Local Bucket Table, we must inquire about every bucket in the segment.
  Lookup is based solely on <bucket_address>
- Firehose Table, we must inquire about firehoses on each node.  Lookup is
  based on <node:bucket_address> where bucket_address is one of the F
  firehoses the local node posesses to each other node. 
*/

/* ------------------------------------------------------------------------ */
/*
Implementing the lookup tables
 - Local Bucket Table
   The Local Bucket Table requires a total M/B bucket descriptors to be
   created and if each of these descripors use the bucket address as the key
   for the lookup.  Since the number of possible key lookups is equal to the
   amount of descriptors actually stored in memory, the Local Bucket Table may
   simply be an array.
 - Firehose Table
   The Firehose Table is actually composed of 'N' smaller firehose tables,
   each having M/B possible key lookups.  This means we have M*N/B possible
   lookups for only M/B descriptors, which would require a constant overhead
   of 'N' in order to implement an array-based lookup.  For obvious reasons,
   some sort of hash table will have to be used in order to provide a
   fixed-time <node:bucket_address> lookup.
*/
 
/* ------------------------------------------------------------------------ */
/*
Unreferenced Local Buckets
Once the refcount for local buckets reaches zero, we have two choices:
 1. Leave the bucket pinned
 2. Unpin it

 The first approach saves some pinning cost and possibly offers better
 performance in environments where physical memory is not an issue by leaving
 parts of the segment pinned.  Conversely, it also helps "guarentee" the M
 parameter by leaving a page of physical memory pinned for use of the current
 application.  The second approach is more simpler than the first in that it
 doesn't require any additional overhead in order to manage unreferenced
 buckets.

 The projected implementation will use a combination of both approaches.  It
 will leave some buckets lying around up to a specified threshold, where it
 will start unpinning buckets in order to prevent starving the system in
 physical memory.  The ideal value of this threshold remains to be determined.
*/

/* ------------------------------------------------------------------------ */
/*
Managing Unreferenced Local Buckets
In order to make recycling of unreferenced local buckets possible, they are
managed explicitly in a Local Bucket Victim FIFO.  This FIFO is simply an
LRU-based list which keeps track of a bounded amount of unreferenced local
buckets.  A bucket from this list may be unpinned at any time in order to
satisfy a request to pin another bucket in the segment.
*/

/* ------------------------------------------------------------------------ */
/*
Local Bucket descriptor representation
- address	the actual address of the pinned page(s)
- refcount 	the reference count for the current bucket. 
- next/prev	pointers/indexes used in Bucket Victim FIFO

 * 32-bit platforms (4k pages)
   bucket_desc_max = M_max/B_min = 4GB/4K = 1MB
   So we have at most 1MB bucket descriptors to create.  If we use an
   array-based table lookup, we don't need to store the address in the
   descriptor, may use indexes for next/prev and need at most 10 bits for
   refcount (max nodes on cluster = 1024).  Thus, a 64-bit type would be
   sufficient:
   | prev (20 bits) | next (20 bits) | refcount (24 bits) |

 * 64-bit platforms (8k pages)
   bucket_desc_max = M_max/B_min = 64GB/8k = 8MB (here 64 is taken since we
   cannot fathom more than 64GB on 64-bit system running Myrinet yet).
   So we have at most 8MB bucket descriptors to create.  Again, using an
   array-based table lookup, we may use the above approach to use a 64-bit
   type to store the descriptor:
   | prev (23 bits) | next (23 bits) | refcount (18 bits) |
   A worse case metadata overhead in this case yields 64MB, which is improved
   when we start clustering the B parameter.  Hence, the larger M, the more we
   can afford to have a larger B while maintaining the same amount of pinning
   overhead.

 * In both cases, the amount of metadata scales with M which in turn is
   loosely based on the amount of physical memory - an array-based table
   lookup-up
*/

/* ------------------------------------------------------------------------ */
/*
Firehose (Remote Bucket) descriptor representation
- address	the actual address of the pinned page(s)
- refcount 	the reference count (bucket "locked")
- next		LRU FIFO for 0-refcounts

  * 32-bit platforms (4k pages)
    F_max = M_max/(B_min*(N_min-1)) = 4GB/(4K*(2-1)) = 1MB
    A node may own a maximum of 1MB firehoses for all nodes, although we
    support up to 1024 nodes, which requires the look-up table to support up
    to 1024*1MB possibly lookups.  Array-based table lookup is impossible in
    this case.  We need some form of hashing.
  * 64-bit platforms (8k pages)
    F_max = M_max/(B_min(N_min-1)) = 64GB/(8K*(2-1)) = 8MB
    idem, would need 8GB for array-based lookup.  Also need some form of
    hashing.
*/
/* ------------------------------------------------------------------------ */
/*
Moving the Firehose on remote nodes
When the local node moves a firehose on a remote node, it expects the firehose
to stay put during the entire duration of the DMA operation.  Once the DMA
completes, the firehose is free to be moved.  The only way for firehoses to be
mishandled or for non-blocking put/get operations and for threaded clients:
Both are able to read/modify the firehose before the DMA application
associated with the firehose is complete.  In order to 'lock' the firehoses, a
reference count is kept with each firehose descriptor which references the
amount of outstanding operations which use the firehose on a fixed remote
bucket.

In implementation, it may be required to bound the number of outstanding
operations to the maximum storable reference count in order to guarentee
correctness and prevent overflow.

Finding a free firehose requires keeping track of each usued/unused firehose.
This is done using a FIFO list of 0-refcount firehose descriptors which allows
using the least recently used firehose when looking for a firehose to move.

A different approach might be to ignore recently used firehoses and walk
through the hash table looking for 0-refcount firehoses.

In both cases, it may be required to poll if no firehose has a 0-refcount.
This is to allow outstanding DMA operations to complete and decrement their
refcounts in the hopes that a 0-refcount may be obtained as soon as possible.
*/
/* ------------------------------------------------------------------------ */
/*
Pinning Local buckets
Requests to pin memory must be fulfilled for
 - bucket pins/unpins inside reserved segment
   Usual puts/gets from/to segment locations, which use the prescribed
   firehose algorithm.
   This approach explicitly lists which buckets to pin/unpin by bucket
   address.
 - out-of segment bucket pins
   These accomodate putting from an out-of-segment region into a remote
   segment region or getting into an out-of-segment region from a remote
   segment region.  However, these are always used by the local node wanting
   to fulfill an RDMA operation.  The only restriction on these bucket pins is
   that they may only be used during the elapsed time for the DMA operation to
   complete.  While buckets may not be stolen while the operation completes,
   they are immediately set to a reference count of 0 and enqueued in the
   Victim FIFO queue as soon as the operation completes.
   This approach implicitly lists the buckets to pin by providing a src/length
   tuple.
   XXX Currently, the approach is to pin everything, even "small" puts which
       are out of the segment will lead to a bucket being pinned out of the
       segment

Requests for pinning memory come in one of the two following forms:
 A. By address and length
    Caller needs a certain portion in memory to be pinned.  Useful for
    out-of-segment local pins. 
 B. Ordered bucket list
    Caller provides a sorted bucket list to pin/unpin.  Hopefully, the sort
    allows us to find contiguous buckets in order to minimize registration
    overhead.

In both cases, the caller doesn't really care if the pages covered by the
requested region are pinned or not, he simply wants them pinned after the
call.  In the first case, we know that the first region contains contiguous
buckets while the second forces us to find them.
*/

/* ------------------------------------------------------------------------ */
/*
Futher Research
It might be interesting, once firehose is up and running, to profile the
algorithm in order to see how it performs with respect to the F parameter.
Since each node posesses F firehoses to each other node, this constrains the
amount of firehoses a specific node may use to another node.  It might be
interesting to look at credit or token-based approaches where it is possible
for a node to posess more firehoses to another node.  This would probably use
an approach where unused firehoses are given for other nodes to use.  However,
this would lead to having dynamic sized hash tables, which presents another
problem difficult to overcome with the current firehose approach.  It may be
possible to overcome this pitfall by using a single large hash table that
hashes using two keys <node:bucket_address> instead of 'N' smaller hash tables
only using the <bucket_address> key.  In any case, we know that a single node
need only to create a hash table the size of F_max, which is the maximum
number of firehoses the node may own over all nodes combined.
*/
/* ------------------------------------------------------------------------ */
#include <gasnet.h>
#include <gasnet_core_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#ifdef GASNETC_FIREHOSE
/*
 * The code below uses the following namespaces
 *
 * GASNETC_BUCKET_*, GASNETC_FIREHOSE_* respectively refer to local bucket and
 * remote bucket (firehose) macros.
 *
 * gasnetc_bucket_*, gasnetc_firehose_* refer to functions and variables
 * (although gasnetc_firehose_* variables are abbreviated to gasnetc_fh_)
 */

/* Firehose MACROs */
#ifndef GASNETC_BUCKET_SIZE
#define GASNETC_BUCKET_SIZE		4*PAGE_SIZE
#endif
#ifndef GASNETC_BUCKET_SHIFT
#define GASNETC_BUCKET_SHIFT		14
#endif
#define GASNETC_SEGMENT_MOD_SIZE	GASNETC_BUCKET_SIZE	

typedef
struct gasnetc_bucket_desc {
	/* packed are refcount and prev pointer when used in victim fifo */
	uint32_t	refc_prev;
	uint32_t	next;
} 
gasnetc_bucket_desc_t;

static gasnetc_bucket_desc_t	*gasnetc_bucket_table;
static uintptr_t		 gasnetc_bucket_start;
static size_t			 gasnetc_bucket_victim_count;
static size_t			 gasnetc_bucket_victim_max;
static gasnetc_bucket_desc_t	*gasnetc_bucket_victim_head_ptr;
static gasnetc_bucket_desc_t	*gasnetc_bucket_victim_tail_ptr;

#ifdef GASNETI_THREADS
gasnetc_lock_t		 gasnetc_bucket_lock = GASNETC_LOCK_INITIALIZER;
gasnetc_lock_t		 gasnetc_bucket_victim_lock = GASNETC_LOCK_INITIALIZER;
#endif

extern void	gasnetc_bucket_init(uintptr_t, uintptr_t);
extern void	gasnetc_bucket_finalize();
extern void	gasnetc_bucket_pin_by_addr(uintptr_t, size_t);
extern void	gasnetc_bucket_unpin_by_addr(uintptr_t, size_t);

static void	gasnetc_bucket_victim_free(size_t);
static void	gasnetc_bucket_pin_register_wrapper(uintptr_t, size_t);
static void	gasnetc_bucket_trypin_by_bucket(uintptr_t, size_t);
static void	gasnetc_bucket_pin_by_list(uintptr_t *, size_t);
static void	gasnetc_bucket_unpin_deregister_wrapper(uintptr_t, size_t);
static void	gasnetc_bucket_tryunpin_by_bucket(uintptr_t, size_t);
static void	gasnetc_bucket_unpin_by_list(uintptr_t *, size_t);

#define GASNETC_BUCKET_VICTIM_MAX_SIZE	(0.5)	/* XXX percent of segment size
						 * ideally should be an env
						 * variable */
/* We only need to support 1023 reference counts from remote firehoses and at
 * most ~280 local reference counts from local operations using GM tokens.
 * 11 bits are sufficient */
#define GASNETC_BDESC_REFC_OFF		(11)
#define GASNETC_BDESC_REFC_MASK		(0x7ff)
#define GASNETC_BDESC_REFC_UNPINNED	GASNETC_BDESC_REFC_MASK
#define GASNETC_BDESC_PREV_MASK		(~GASNETC_BDESC_REFC_MASK)

/* Bucket descriptor macros */
/* #define GASNETC_BDESC_INDEX(bdptr) (assert(bdptr&(GASNETC_BUCKET_SIZE-1)==0), \
	      (((uintptr_t)(bdptr)-gasnetc_bucket_start)>>GASNETC_BUCKET_SHIFT)
 */
#define GASNETC_BDESC_INDEX(bdptr)                                            \
	      (((uintptr_t)(bdptr)-gasnetc_bucket_start)>>GASNETC_BUCKET_SHIFT)
#define GASNETC_BDESC_FROM_ADDR(addr)                                         \
			     (&gasnetc_bucket_table[GASNETC_BDESC_INDEX(addr)])
#define GASNETC_BDESC_TO_ADDR(bdptr)                                          \
	      (((uintptr_t)(bdptr)-gasnetc_bucket_start)<<GASNETC_BUCKET_SHIFT)

/* Reference count macros */
#define GASNETC_BDESC_REFC(bdptr) 	((bdptr)->refc_prev &                  \
						GASNETC_BDESC_REFC_MASK)
#define GASNETC_BDESC_REFC_INC(bdptr)	(assert(GASNETC_BDESC_REFC(bdptr) !=   \
						GASNETC_BDESC_REFC_MASK),      \
						(bdptr)->refc_prev++)
#define GASNETC_BDESC_REFC_DEC(bdptr)	(assert(GASNETC_BDESC_REFC(bdptr)!= 0),\
						((bdptr)->refc_prev--))
#define GASNETC_BDESC_REFC_SET(bdptr,v)	(bdptr)->refc_prev =                  \
						GASNETC_BDESC_PREV(bdptr) |    \
						(v & GASNETC_BDESC_REFC_MASK)
#define GASNETC_BDESC_REFC_ZERO(bdptr)	((bdptr)->refc_prev =                  \
						GASNETC_BDESC_PREV(refc))
#define GASNETC_BDESC_REFC_ISZERO(bdptr)	(GASNETC_BDESC_REFC(bdptr) == 0)

/* Pointer macros */
#define GASNETC_BDESC_NEXT(bdptr)	((bdptr)->next)
#define GASNETC_BDESC_NEXT_SET(bdptr,n)	((bdptr)->next = (n))
#define GASNETC_BDESC_NEXT_ZERO(bdptr)	((bdptr)->next = 0)
#define GASNETC_BDESC_PREV(bdptr)	((((bdptr)->refc_prev &                \
						GASNETC_BDESC_PREV_MASK)) >>   \
						GASNETC_BDESC_REFC_OFF)
#define GASNETC_BDESC_PREV_SET(bdptr,p)	((bdptr)->refc_prev =                  \
						GASNETC_BDESC_REFC(bdptr) |    \
						((p) << GASNETC_BDESC_REFC_OFF))
#define GASNETC_BDESC_PREV_ZERO(bdptr)	((bdptr)->refc_prev =                  \
						GASNETC_BDESC_REFC(bdptr))
#define GASNETC_BDESC_ISPINNED(bdptr)	(GASNETC_BDESC_REFC(bdptr) !=          \
						GASNETC_BDESC_REFC_UNPINNED)
#define GASNETC_BDESC_CONTIGUOUS(bdptr1,bdptr2)                                \
	((gasnetc_bucket_desc_t *)(bdptr2)-(gasnetc_bucket_desc_t *)(bdptr1)==1)


/* ------------------------------------------------------------------------ */
/* Local Bucket operations (pin/unpin) */
void
gasnetc_bucket_init(uintptr_t segbase, uintptr_t segsize)
{
	size_t			 num_buckets;
	gasnetc_bucket_desc_t	*table;

	assert(segsize > 0 && segbase > 0);
	assert(segsize % GASNETC_BUCKET_SIZE == 0);
	assert(segbase % GASNETC_BUCKET_SIZE == 0);

	/* add two extra buckets to use as head/tail for victim fifo */
	num_buckets = (segsize>>GASNETC_BUCKET_SHIFT) + 2;
	table = (gasnetc_bucket_desc_t *)
		gasneti_malloc(num_buckets*sizeof(gasnetc_bucket_desc_t));

	/* setup the initial bucket victim fifo queue, where the head's next
	 * pointer is the tail and the tail's previous pointer is the head
	 */
	gasnetc_bucket_victim_head_ptr = &table[0];
	gasnetc_bucket_victim_tail_ptr = &table[1];
	GASNETC_BDESC_NEXT_SET(&table[0], 1);
	GASNETC_BDESC_PREV_SET(&table[1], 0);

	/* the actual bucket table starts at 2 */
	gasnetc_bucket_table = &table[2];
	gasnetc_bucket_start = segbase;

	/* move VICTIM_MAX_SIZE to an environment variable */
	gasnetc_bucket_victim_max = (int)
	    (segsize*GASNETC_BUCKET_VICTIM_MAX_SIZE);
	gasnetc_bucket_victim_count = 0;
}

void
gasnetc_bucket_finalize()
{
	gasneti_free(gasnetc_bucket_victim_head_ptr);
}

/*
 * A wrapper to free 'num_buckets' from the victim fifo queue
 */
void
gasnetc_bucket_victim_free(size_t num_buckets)
{
	gasnetc_bucket_desc_t	*bdesc_cur, *bdesc_prev;
	uintptr_t		bucket_addr;
	int			i;

	assert(num_buckets >= gasnetc_bucket_victim_count);

	gasnetc_lock(&gasnetc_bucket_victim_lock);
	/* Start removing buckets from the tail, always trying to find
	 * contiguous buckets in order to minimize deregistration overhead */
	bdesc_cur = &gasnetc_bucket_table[
	    GASNETC_BDESC_PREV(gasnetc_bucket_victim_tail_ptr)];
	gasnetc_bucket_victim_count -= num_buckets;
	while (num_buckets > 0) {
		i = 1;
		bdesc_prev = 
		    &gasnetc_bucket_table[GASNETC_BDESC_PREV(bdesc_cur)];
		GASNETC_BDESC_PREV_ZERO(bdesc_cur);
		GASNETC_BDESC_NEXT_ZERO(bdesc_cur);
		GASNETC_BDESC_REFC_SET(bdesc_cur, GASNETC_BDESC_REFC_UNPINNED);

		while (num_buckets > 0 && 
		    GASNETC_BDESC_CONTIGUOUS(bdesc_cur, bdesc_prev)) {
			GASNETC_BDESC_PREV_ZERO(bdesc_prev);
			GASNETC_BDESC_NEXT_ZERO(bdesc_prev);
			GASNETC_BDESC_REFC_SET(bdesc_prev, 
			    GASNETC_BDESC_REFC_UNPINNED);
			bdesc_cur = bdesc_prev;
			bdesc_prev = 
			    &gasnetc_bucket_table[GASNETC_BDESC_PREV(bdesc_cur)];
			num_buckets--; 
			i++;
		}
		gasnetc_bucket_unpin_deregister_wrapper(
		    GASNETC_BDESC_TO_ADDR(bdesc_cur), i);
		bdesc_cur = bdesc_prev;
		num_buckets--;
	}
	/* set the new tail, which is _cur */
	GASNETC_BDESC_PREV_SET(gasnetc_bucket_victim_tail_ptr,
	    GASNETC_BDESC_INDEX(bdesc_cur));
	GASNETC_BDESC_NEXT_SET(bdesc_cur, 1);
	gasnetc_unlock(&gasnetc_bucket_victim_lock);
}

/*
 * Wrapper to gm_register_memory
 *
 * We first free buckets if there are too many (over threshold)
 * There are two stabs and registering memory, and returning 0 should be
 * considered fatal by the caller.  The first one tries to register memory
 * (notwithstanding the threshhold) while the second tries again after
 * freeing more buckets if possible.
 */
void
gasnetc_bucket_pin_register_wrapper(uintptr_t bucket_addr, size_t num_buckets)
{
	gm_status_t	status;

	assert(bucket_addr % GASNETC_BUCKET_SIZE == 0);

	if (gasnetc_bucket_victim_count >= gasnetc_bucket_victim_max)
		gasnetc_bucket_victim_free(MIN(num_buckets, 
		    gasnetc_bucket_victim_count));

	if (gm_register_memory(_gmc.port, (void *)bucket_addr, 
	    num_buckets << GASNETC_BUCKET_SHIFT) == GM_SUCCESS)
		return;
	else {
		/* failed, let us try to unregister more, if possible */
		if (gasnetc_bucket_victim_count < num_buckets)
		 	gasneti_fatalerror("Could not register memory");
		else {
			gasnetc_bucket_victim_free(num_buckets);
			if (gm_register_memory(_gmc.port, (void *)bucket_addr, 
			    num_buckets << GASNETC_BUCKET_SHIFT) == GM_SUCCESS)
				return;
			else
		 		gasneti_fatalerror("Could not register memory");
		}
	}
}	

/*
 * Wrapper to gm_unregister_memory
 *
 * No fancy stuff.  Registers 'num_buckets' starting at bucket_addr
 */
void
gasnetc_bucket_unpin_deregister_wrapper(uintptr_t bucket_addr, 
					size_t num_buckets)
{
	gm_status_t	status;

	assert(bucket_addr % GASNETC_BUCKET_SIZE == 0);
	if (gm_deregister_memory(_gmc.port, (void *)bucket_addr, 
	    num_buckets << GASNETC_BUCKET_SHIFT) == GM_SUCCESS)
		return;
	else
		 gasneti_fatalerror("Could not deregister memory");
}

/*
 * This version of pin simply attempts to wrap calls to local_pin_by_bucket by
 * providing the largest 'num_buckets_contiguous' possible.  In this case, the
 * caller has a region of memory to be pinned but doesn't know if it is pinned
 * or not.  We must try to do all we can to pin the memory.
 */
void
gasnetc_bucket_trypin_by_bucket(uintptr_t bucket_addr, size_t num_buckets)
{
	int		i,j;
	gasnetc_bucket_desc_t	*bdesc, *bdesc_cur, *bdesc_prev, *bdesc_next;

	assert(bucket_addr % GASNETC_BUCKET_SIZE == 0);

	gasnetc_lock(&gasnetc_bucket_lock);
	i = 0;
	bdesc = GASNETC_BDESC_FROM_ADDR(bucket_addr);
	while (i < num_buckets) {
		bdesc_cur = bdesc + i;
		if (GASNETC_BDESC_ISPINNED(bdesc_cur)) {
			/* If zero, remove from Victim FIFO queue */
			if (GASNETC_BDESC_REFC_ISZERO(bdesc_cur)) {
				/* bdesc.prev.next = bdesc.next;
				 * bdesc.next.prev = bdesc.prev;
				*/
				bdesc_prev = 
				    &gasnetc_bucket_table[
				     GASNETC_BDESC_PREV(bdesc_cur)];
				bdesc_next = 
				    &gasnetc_bucket_table[
				     GASNETC_BDESC_NEXT(bdesc_cur)];

				GASNETC_BDESC_NEXT_SET(bdesc_prev, 
				    GASNETC_BDESC_NEXT(bdesc_cur));
				GASNETC_BDESC_PREV_SET(bdesc_next,
				    GASNETC_BDESC_PREV(bdesc_cur));

				GASNETC_BDESC_NEXT_ZERO(bdesc_cur);
				GASNETC_BDESC_PREV_ZERO(bdesc_cur);
			}
			GASNETC_BDESC_REFC_INC(bdesc_cur);
			i++;
		}
		else {
			j = 1;
			GASNETC_BDESC_REFC_SET(bdesc_cur, 1);
			while ((i+j < num_buckets) && 
			    !GASNETC_BDESC_ISPINNED(bdesc_cur+j)) {
				GASNETC_BDESC_REFC_SET(bdesc_cur+j, 1);
				j++; 
			}
			gasnetc_bucket_pin_register_wrapper(
			    GASNETC_BDESC_TO_ADDR(bdesc_cur), j);
			i += j;
		}
	}
	gasnetc_unlock(&gasnetc_bucket_lock);
}

/*
 * Same as above, except there is a special handling to use the victim fifo
 * queue for refcounts
 */
void
gasnetc_bucket_tryunpin_by_bucket(uintptr_t bucket_addr, size_t num_buckets)
{
	int		i;
	gasnetc_bucket_desc_t	*bdesc, *bdesc_cur, *bdesc_next;

	assert(gasnetc_bucket_victim_max > 0);

	bdesc = GASNETC_BDESC_FROM_ADDR(bucket_addr);
	for (bdesc_cur = bdesc; bdesc_cur < bdesc+num_buckets; bdesc_cur++) {
		assert(GASNETC_BDESC_ISPINNED(bdesc_cur));
		/* XXX check this out.
		 * assert(!GASNETC_BDESC_REFC_ISZERO(bdesc_cur));
		 */
		GASNETC_BDESC_REFC_DEC(bdesc_cur);

		/* we might need to add it to the fifo queue */
		if (GASNETC_BDESC_REFC_ISZERO(bdesc_cur) &&
		    (gasnetc_bucket_victim_count <= gasnetc_bucket_victim_max)) {
			gasnetc_lock(&gasnetc_bucket_victim_lock);
			/* we always add through the head */
			bdesc_next = &gasnetc_bucket_table[
			    GASNETC_BDESC_NEXT(gasnetc_bucket_victim_head_ptr)];

			GASNETC_BDESC_NEXT_SET(bdesc_cur, 
			    GASNETC_BDESC_INDEX(bdesc_next));
			GASNETC_BDESC_PREV_ZERO(bdesc_cur);
			GASNETC_BDESC_PREV_SET(bdesc_next, 
			    GASNETC_BDESC_INDEX(bdesc_cur));
			GASNETC_BDESC_NEXT_SET(gasnetc_bucket_victim_head_ptr,
			    GASNETC_BDESC_INDEX(bdesc_cur));
			gasnetc_bucket_victim_count++;
			gasnetc_unlock(&gasnetc_bucket_victim_lock);
		}
		else {
			gasnetc_lock(&gasnetc_bucket_lock);
			gasnetc_bucket_unpin_deregister_wrapper(bucket_addr, 
			    num_buckets);
			gasnetc_unlock(&gasnetc_bucket_lock);
		}
	}
}

/*
 * From a source address and a length parameter, a certain number of
 * buckets are pinned and initialized to a reference count of 1
 *
 * Form 'A' of pinning memory
 *
 */
extern void
gasnetc_bucket_pin_by_addr(uintptr_t src, size_t nbytes) 
{
	uintptr_t	bucket_addr;
	size_t		num_buckets;

	bucket_addr = GASNETI_PAGE_ALIGN(src, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETI_PAGE_ROUNDUP(nbytes, GASNETC_BUCKET_SIZE) >> 
	    GASNETC_BUCKET_SHIFT;
	gasnetc_bucket_trypin_by_bucket(bucket_addr, num_buckets);
}

/*
 * Unpinning/decrementing by refcount will probably only be used in GM
 * callbacks, since pinning by address is only done for local to remote
 * DMA operations
 */
extern void
gasnetc_bucket_unpin_by_addr(uintptr_t src, size_t nbytes)
{
	uintptr_t	bucket_addr;
	size_t		num_buckets;

	bucket_addr = GASNETI_PAGE_ALIGN(src, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETI_PAGE_ROUNDUP(nbytes, GASNETC_BUCKET_SIZE) >> 
	    GASNETC_BUCKET_SHIFT;
	gasnetc_bucket_tryunpin_by_bucket(bucket_addr, num_buckets);
}

/*
 * This version of pin takes an array of sorted buckets (ascending) and
 * guarentees that the pages will be pinned after the call.
 */
void
gasnetc_bucket_pin_by_list(uintptr_t *bucket_list, 
			   size_t num_buckets_list)
{
	unsigned int	i,j;

	i = 0;
	while (i < num_buckets_list) {
		j = 1;
		while (i+j < num_buckets_list &&
		    GASNETC_BDESC_CONTIGUOUS(
		    GASNETC_BDESC_FROM_ADDR(bucket_list + i + j - 1),
		    GASNETC_BDESC_FROM_ADDR(bucket_list + i + j)))
			j++;
		gasnetc_bucket_trypin_by_bucket(bucket_list[i], j);
		i += j;
	}
}

/*
 * Idem, for unpinning buckets
 */
void
gasnetc_bucket_unpin_by_list(uintptr_t *bucket_list, 
			     size_t num_buckets_list)
{
	unsigned int	i,j;

	i = 0;
	while (i < num_buckets_list) {
		j = 1;
		while (i+j < num_buckets_list &&
		    GASNETC_BDESC_CONTIGUOUS(
		    GASNETC_BDESC_FROM_ADDR(bucket_list + i + j - 1),
		    GASNETC_BDESC_FROM_ADDR(bucket_list + i + j)))
			j++;
		gasnetc_bucket_tryunpin_by_bucket(bucket_list[i], j);
		i += j;
	}
}

/* ------------------------------------------------------------------------ */
/* Firehose (Remote Bucket) operations (pin/unpin) */
/*
 * F = M/((N-1)*B)
 * Fore the gm hash table api, keys are of the format:
 * (gasnetc_bucket_addr | node_id)
 *
 * XXX might have alignment errors, look into this.
 *
 */

/* Hash value, once looked up.
 * Its size is the value of two pointers.
 */
typedef
struct gasnetc_fh_data {
	/* bucket_refcount holds bucket address _and_ refcount
	 * assert refcount is < ... */
	uintptr_t		bucket_refcount;
	struct gasnetc_fh_data	*next;
}
gasnetc_fh_data_t;
typedef uintptr_t	gasnetc_fh_key_t;

static gasnetc_fh_data_t	*gasnetc_fh_victims;
static size_t			*gasnetc_fh_victim_count;
static struct gm_hash		*gasnetc_fh_hash;
static size_t			 gasnetc_fh_num;
static size_t			*gasnetc_fh_used;

uintptr_t	*gasnetc_firehose_buf = NULL;
size_t		 gasnetc_firehose_buf_num = 0;


#ifdef GASNETI_THREADS
gasnetc_lock_t		 gasnetc_fh_hash_lock   = GASNETC_LOCK_INITIALIZER;
gasnetc_lock_t		 gasnetc_fh_victim_lock = GASNETC_LOCK_INITIALIZER;
#endif

extern void	gasnetc_firehose_init(uintptr_t);
extern void	gasnetc_firehose_finalize();
extern int	gasnetc_firehose_build_list(gasnet_node_t, uintptr_t, size_t, 
					    size_t *, size_t *);
extern void	gasnetc_firehose_decrement_refcount(gasnet_node_t, uintptr_t, 
						    size_t);

#define GASNETC_FH_KEY_NODE_MASK	(GASNETC_BUCKET_SIZE-1)
#define GASNETC_FH_KEY_ADDR_MASK	~GASNETC_FH_KEY_NODE_MASK
#define GASNETC_FH_KEY_NODE(key)	((key) & GASNETC_FH_KEY_NODE_MASK)
#define GASNETC_FH_KEY_ADDR(key)	((key) & GASNETC_FH_KEY_ADDR_MASK)
#define GASNETC_FH_KEY(baddr,node) (assert((baddr&(GASNETC_BUCKET_SIZE-1))==0),\
					((baddr) | (node)))
#define GASNETC_FH_NEXT_END		((void *) -1)

#define GASNETC_FH_REFC_MASK		(GASNETC_BUCKET_SIZE-1)
#define GASNETC_FH_ADDR_MASK		~GASNETC_FH_REFC_MASK
#define GASNETC_FH_ADDR(fhptr) ((fhptr)->bucket_refcount & GASNETC_FH_ADDR_MASK)
#define GASNETC_FH_REFC(fhptr) ((fhptr)->bucket_refcount & GASNETC_FH_REFC_MASK)
#define GASNETC_FH_REFC_INC(fhptr)	(assert((fhptr)->bucket_refcount !=    \
						GASNETC_FH_REFC_MASK),         \
						(fhptr)->bucket_refcount++)
#define GASNETC_FH_REFC_DEC(fhptr)	(assert(GASNETC_FH_REFC(fhptr) != 0),  \
						(fhptr)->bucket_refcount--)
#define GASNETC_FH_REFC_SET(fhptr,d)	(assert(d <= GASNETC_FH_REFC_MASK),    \
						(fhptr)->bucket_refcount =     \
						(GASNETC_FH_ADDR(fhptr) | d))
#define GASNETC_FH_REFC_ZERO(fhptr)	((fhptr)->bucket_refcount &=           \
						GASNETC_FH_ADDR_MASK)
#define GASNETC_FH_REFC_ISZERO(fhptr)	(GASNETC_FH_REFC(fhptr) == 0)

extern void
gasnetc_firehose_init(uintptr_t	segsize)
{
	size_t	firehoses;
	int		i;

	assert(segsize % GASNETC_BUCKET_SIZE == 0);
	/* initilize firehose victim fifo for all nodes, initializing a dummy
	 * head for all nodes */
	gasnetc_fh_victims = (gasnetc_fh_data_t *) 
	    gasneti_malloc(sizeof(gasnetc_fh_data_t) * (gasnetc_nodes));
	for (i = 0; i < gasnetc_nodes; i++)
		gasnetc_fh_victims[i].next = GASNETC_FH_NEXT_END;
	gasnetc_fh_victim_count = (size_t *)
	    gasneti_malloc(sizeof(size_t) * (gasnetc_nodes));
	gasnetc_fh_used = (size_t *)
	    gasneti_malloc(sizeof(size_t) * (gasnetc_nodes));
	memset((void *) gasnetc_fh_used, 0, 
	    sizeof(size_t) * (gasnetc_nodes));

	firehoses = segsize >> GASNETC_BUCKET_SHIFT;
	gasnetc_fh_hash = 
	    gm_create_hash(gm_hash_compare_ptrs, gm_hash_hash_ptr,
	    0, sizeof(gasnetc_fh_data_t), firehoses, 0);
	if (gasnetc_fh_hash == NULL)
		gasneti_fatalerror("could not create firehose hash!\n");
	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose hash initialized to %d elems (segsize=%d)", firehoses,
	    segsize));
	gasnetc_fh_num = (size_t) (firehoses/(gasnetc_nodes-1));
	return;
}

extern void
gasnetc_firehose_finalize()
{
	free(gasnetc_fh_victims);
	free(gasnetc_fh_victim_count);
	free(gasnetc_fh_used);
	gm_destroy_hash(gasnetc_fh_hash);
}

GASNET_INLINE_MODIFIER(gasnetc_firehose_ispinned)
int
gasnetc_firehose_ispinned(gasnetc_fh_key_t key)
{
	gasnetc_fh_data_t	*fh_data;
	gasnet_node_t		node;
	int			ret = 0;

	gasnetc_lock(&gasnetc_fh_hash_lock);
	if ((fh_data = (gasnetc_fh_data_t *) 
	    gm_hash_find(gasnetc_fh_hash, (void *)key)) != NULL) {
		assert(fh_data->next == NULL);
		/* XXX we can't remove from the middle of the FIFO */
		GASNETC_FH_REFC_INC(fh_data);
		ret = 1;
	}
	gasnetc_unlock(&gasnetc_fh_hash_lock);
	return ret;
}

GASNET_INLINE_MODIFIER(gasnetc_firehose_unpin)
int
gasnetc_firehose_unpin(gasnetc_fh_key_t key)
{
	gasnetc_fh_data_t	*fh_data;
	gasnet_node_t		node;
	int			ret = 0;

	gasnetc_lock(&gasnetc_fh_hash_lock);
	if ((fh_data = (gasnetc_fh_data_t *) 
	    gm_hash_find(gasnetc_fh_hash, (void *)key)) != NULL) {
		assert(GASNETC_FH_REFC(fh_data) > 0);
		GASNETC_FH_REFC_DEC(fh_data);
		/* if refcount is zero and we were not already in the victim
		 * fifo, add ourselves */
		gasnetc_lock(&gasnetc_fh_victim_lock);
		if (fh_data->next == NULL && GASNETC_FH_REFC_ISZERO(fh_data)) {
			node = GASNETC_FH_KEY_NODE(key);
			assert(node > 0 && node < gasnetc_nodes);
			fh_data->next = gasnetc_fh_victims[node].next;
			gasnetc_fh_victims[node].next = fh_data;
			gasnetc_fh_victim_count[node]++;
		}
		gasnetc_unlock(&gasnetc_fh_victim_lock);
		ret = 1;
	}
	gasnetc_unlock(&gasnetc_fh_hash_lock);
	return ret;
}

GASNET_INLINE_MODIFIER(gasnetc_firehose_add)
void
gasnetc_firehose_add(gasnetc_fh_key_t key)
{
	gm_status_t		status;
	gasnetc_fh_data_t	data;
	gasnet_node_t		node;

	assert(gm_hash_find(gasnetc_fh_hash, (void *)key) == NULL);
	/* gasneti_assert_unlocked(&gasnetc_fh_hash_lock) */
	data.next = NULL;
	node = GASNETC_FH_KEY_NODE(key);
	gasnetc_lock(&gasnetc_fh_hash_lock);
	GASNETC_FH_REFC_SET(&data, 1);
	status = gm_hash_insert(gasnetc_fh_hash, (void *)key, (void *)&data);
	gasnetc_fh_used[node]++;
	gasnetc_unlock(&gasnetc_fh_hash_lock);
	if (status != GM_SUCCESS)
		gasneti_fatalerror("could not insert key in firehose hash");
	return;
}

GASNET_INLINE_MODIFIER(gasnetc_firehose_remove)
void
gasnetc_firehose_remove(gasnetc_fh_key_t key)
{
	gasnet_node_t	node;

	node = GASNETC_FH_KEY_NODE(key);
	gasnetc_lock(&gasnetc_fh_hash_lock);
	gasnetc_fh_used[node]--;
	if (gm_hash_remove(gasnetc_fh_hash, (void *)key) == NULL) {
		gasneti_fatalerror("key doesn't exist in firehose hash");
	}
	gasnetc_unlock(&gasnetc_fh_hash_lock);
	return;
}

/*
 * In attempting to find free firehoses from the 0-refcount victim firehose
 * FIFO, it is possible that entries in the fifo are dirty (their refcount is
 * not 0).  This arises when 0-refcount entries living in the victim FIFO queue
 * are reused (thus locked) in an outsanding operation.  This situtation could
 * be corrected by adding prev/next pointers but this leads to larger metadata.
 * In our case, we sacrifice a small amount of CPU time.
 */

GASNET_INLINE_MODIFIER(gasnetc_firehose_find_freevictim)
gasnetc_fh_data_t *
gasnetc_firehose_find_freevictim(gasnet_node_t node)
{
	gasnetc_fh_data_t	*data, *data_tmp;

	/* XXX gasnetc_assert_lock(&gasnetc_fh_victim_lock) */
	data = &gasnetc_fh_victims[node];
	while (data->next != GASNETC_FH_NEXT_END) {
		/* We always remove an element from the FIFO */
		data_tmp = data->next;
		data->next = data_tmp->next;
		data_tmp->next = NULL;

		/* If the reference count is zero, we can safely reuse this
		 * firehose, or else just remove the element from the FIFO
		 * and keep looking for a refcount==0
		 */
		if (GASNETC_FH_REFC_ISZERO(data_tmp)) {
			gasnetc_fh_victim_count[node]--;
			return data_tmp;
		}
	} 
	/* We walked through the victim fifo and didn't find anything */
	return NULL;
}

#if 0
/* disable for now . . */
int
gasnetc_firehose_bucket_compare(const void *k1, const void *k2)
{
	uintptr_t	a = *((uintptr_t *) k1);
	uintptr_t	b = *((uintptr_t *) k2);

	if (a > b)
		return 1;
	else if (a < b)
		return -1;
	else
		return 0;
}
#endif

/*
 * Firehose lookup by address checks to see if the local node has a firehose
 * to each bucket covered by (dest, dest+len) on remote node 'node'.  In case
 * one of the firehoses does not map to a required bucket, we must find a
 * replacement.  Returns a successful '0' if the node owns firehoses for all
 * buckets
 * 
 * XXX this version assumes we are using a linked list of 0-refcounts for each
 * node.
 *
 */
extern int
gasnetc_firehose_build_list(gasnet_node_t node, uintptr_t dest, 
			    size_t num_buckets, 
			    size_t *old_buckets, size_t *new_buckets)
{
	unsigned int			i,j,k;
	volatile gasnetc_fh_data_t	*victim;
	uintptr_t			bucket_addr, bucket_cur;
	uintptr_t			*firehose_old_buf;

	assert(node > 0 && node < gasnetc_nodes);
	assert(sizeof(uintptr_t)*num_buckets*2 < gasnet_AMMaxMedium());
	assert(gasnetc_firehose_buf != NULL && 
	       gasnetc_firehose_buf_num >= num_buckets*2);
	/* gasneti_assert_locked(&gasnetc_fh_victim_lock) */
	bucket_addr = GASNETI_PAGE_ALIGN(dest, GASNETC_BUCKET_SIZE);
	firehose_old_buf = gasnetc_firehose_buf + num_buckets;
	i = 0;
	j = 0;
	for (k = 0; k < num_buckets; k++) {
		bucket_cur = bucket_addr + (k<<GASNETC_BUCKET_SHIFT);
		/* if already have a firehose, go to the next bucket */
		if (gasnetc_firehose_ispinned(GASNETC_FH_KEY(bucket_cur,node)))
			continue;

		/* XXX look into querying fh_used. . */
		if (gasnetc_fh_used[node] >= gasnetc_fh_num) {
			uintptr_t	vic_addr;

			victim = gasnetc_firehose_find_freevictim(node);
			if (victim == NULL) {

				gasnetc_unlock(&gasnetc_fh_victim_lock);
				/* Overcomplicated polling loop to minimize
				 * locking overhead when looking for victims */
				while (1) {
					gasnetc_AMPoll();
					if (gasnetc_fh_victim_count[node] == 0)
						continue;
					gasnetc_lock(&gasnetc_fh_victim_lock);
					victim = 
					    gasnetc_firehose_find_freevictim(node);
					if (victim != NULL)
						break;
					gasnetc_unlock(&gasnetc_fh_victim_lock);
				}
				/* XXX gasneti_assert_locked(&gasnetc_fh_victim_lock);
				 * Once we return from a poll, it's possible
				 * that other threads decided to acquire the
				 * firehose lock and move a firehose over the
				 * current bucket desc
				 */
				if (gasnetc_firehose_ispinned(
				    GASNETC_FH_KEY(bucket_cur,node)))
					continue;
			}
			/* By now, we really know we need to move the firehose
			 * to the new location */
			vic_addr = GASNETC_FH_ADDR(victim);
			gasnetc_firehose_remove(GASNETC_FH_KEY(vic_addr, node));
			gasnetc_firehose_add(GASNETC_FH_KEY(bucket_cur,node));
			firehose_old_buf[i] = vic_addr;
			i++;
		}
		else {
			gasnetc_firehose_add(GASNETC_FH_KEY(bucket_cur,node));
		}
		gasnetc_firehose_buf[j] = bucket_cur;
		j++;
	}
	*old_buckets = i;
	*new_buckets = j;
	assert(j >= i);
#if 0
	disable for now. .
	/* XXX qsort might be too expensive for small sizes - i like it because
	 *     it uses the stack, no malloc */
	qsort(old_bucket_buf, i, sizeof(uintptr_t), 
	    gasnetc_firehose_bucket_compare);
	qsort(new_bucket_buf, j, sizeof(uintptr_t), 
	    gasnetc_firehose_bucket_compare);
#endif

	return j;
}

extern void
gasnetc_firehose_decrement_refcount(gasnet_node_t node, uintptr_t dest, 
				    size_t len)
{
	uintptr_t	bucket_cur;
	size_t	num_buckets;

	bucket_cur = GASNETI_PAGE_ALIGN(dest, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETI_PAGE_ROUNDUP(len, GASNETC_BUCKET_SIZE) >> 
	    GASNETC_BUCKET_SHIFT;

	do {
		gasnetc_firehose_unpin(GASNETC_FH_KEY(bucket_cur,node));
		bucket_cur += GASNETC_BUCKET_SIZE;
	} while (num_buckets--);
}

/* ------------------------------------------------------------------------ */
/* Firehose move handler, as implemented at the core level.
 * Users must provide their own reply handler through amrep_index parameter.
 * In this case, the extended API uses the firehose_move_req in order to
 * satisfy put requests.  As such, it supplies a reply handler at the extended
 * API level.
 */
GASNET_INLINE_MODIFIER(gasnetc_firehose_move_firehose_reqh_inner)
void
gasnetc_firehose_move_reqh_inner(gasnet_token_t token, void *addr,
				 size_t nbytes, 
				 gasnet_handlerarg_t amrep_index,
				 gasnet_handlerarg_t new_buckets, 
				 gasnet_handlerarg_t old_buckets,
				 gasnet_handlerarg_t old_bucket_off, 
				 void *context)
{
	uintptr_t	*old_bucket_list;

	assert(new_buckets > 0);
	assert(amrep_index > 0 && amrep_index <= 255);

	if (old_buckets > 0) {
		assert(old_bucket_off > 0);
		old_bucket_list = (uintptr_t *) (addr + old_bucket_off);
		gasnetc_bucket_unpin_by_list(old_bucket_list, old_buckets);
	}
	gasnetc_bucket_pin_by_list((uintptr_t *) addr, new_buckets);

	SHORT_REP(1,2,(token, amrep_index, PACK(context)));
}	
MEDIUM_HANDLER(gasnetc_firehose_move_reqh,5,6,
              (token,addr,nbytes, a0, a1, a2, a3, UNPACK(a4)     ),
              (token,addr,nbytes, a0, a1, a2, a3, UNPACK2(a4, a5)));
#endif
