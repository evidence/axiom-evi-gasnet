#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <gasnet.h>
#include <gasnet_core_internal.h>

#define MMAP_EPS (64 * GASNETC_MBYTE)

/* define/init global vars internal to lapi conduit here */
lapi_handle_t lapi_contect;

/* adjust our soft limits to 95% of the hard limit. */
size_t gasnetc_adjust_limits(void)
{
    struct rlimit rl;
    unsigned long long limit;
    
    if (getrlimit(lim,&rl) < 0) {
	perror("* * * WARNING: setmylimit getrlimit");
	return 0;
    }
    limit = (unsigned long long)(0.95*(double)(rl.rlim_max));
    rl.rlim_cur = limit;
    if (setrlimit(lim,&rl) < 0) {
	perror("* * * WARNING: setmylimit setrlimit");
	return 0;
    }
    return (size_t)limit;
}

/* return TRUE if we can anon mmap this amount of address space */
static int gasnetc_try_map(uintptr_t mmap_size)
{
    off_t   mmap_off = 0;
    int     mmap_prot, mmap_flags;
    void*   mmap_addr = NULL;

    
    mmap_prot = PROT_READ | PROT_WRITE;
    mmap_flags = MAP_ANONYMOUS | MAP_VARIABLE | MAP_PRIVATE;
    mmap_addr = mmap((void*)0, mmap_size, mmap_prot, mmap_flags, -1, mmap_off);
    if (mmap_addr == (void*)-1) {
	/* it failed */
	return 0;
    } 
    /* success... now unmap it */
    munmap(mmap_addr,mmap_size);
    return 1;
}

/* return TRUE if we can anon mmap this amount of address space */
int gasnetc_get_map(uintptr_t mmap_size, void **loc)
{
    off_t   mmap_off = 0;
    int     mmap_prot, mmap_flags;
    void*   mmap_addr = NULL;

    *loc = NULL;
    mmap_prot = PROT_READ | PROT_WRITE;
    mmap_flags = MAP_ANONYMOUS | MAP_VARIABLE | MAP_PRIVATE;
    mmap_addr = mmap((void*)0, mmap_size, mmap_prot, mmap_flags, -1, mmap_off);
    if (mmap_addr == (void*)-1) {
	/* it failed */
	return 0;
    } 
    /* success... */
    *loc = mmap_addr;
    return 1;
}

/* Try to determine the max amount of space we can mmap.
 * NOTE: We will only call this in the 32 bit case.
 */
uintprt_t gasnetc_compute_maxlocal(uintptr_t want)
{
    uintptr_t low, high, mid;
    uintptr_t eps;
    uintptr_t page_size;

    /* can we get what we want? */
    if (gasnetc_try_map(want)) {
	return want;
    }

    /* no, this is too high.  start search, but first
     * convert to number of pages rather than bytes.
     */
    page_size = getpagesize();
    high = want/page_size;

    /* figure out starting value for low */
    do {
	low  = high/2;
    } while (low > 0 && !gasnetc_try_map(low*page_size)) {
    if (low == 0) {
	return (size_t)0;
    }

    /* now do the binary search, but only be accurate to 128 MB */
    eps = MMAP_EPS/page_size;
    while (high - low > eps) {
	assert(low < high);
	mid = (high/2) + (low/2);
	/* avoid stupid infinite loops */
	if (mid <= low || mid >= high)
	    break;
	
	if (gasnetc_try_map(mid*page_size)) {
	    /* that worked, try a higher value next time */
	    low = mid;
	} else {
	    /* oops, try a smaller value */
	    high = mid;
	}
    }

    return low*page_size;
}

/* This is the async error handler we register with the LAPI layer */
void gasnetc_lapi_err_handler(lapi_handle_t *context, int *error_code,
			      lapi_err_t  *error_type, int *cur_node,
			      int *src_node)
{
    char msg[LAPI_MAX_ERR_STRING];

    LAPI_Msg_string(*error_code,msg);
    fprintf(stderr,"Async LAPI Error on node %d from node %d of type %s code %d [%s]\n",
	    *cur_node,*src_node,err_type_str[*error_type],*error_code,msg);
    exit(-1);
}
