/* $Id: gasnet_core_misc.c,v 1.11 2002/06/26 21:03:29 csbell Exp $
 * $Date: 2002/06/26 21:03:29 $
 * $Revision: 1.11 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet_core_internal.h>

#define	_BUFSZ	127
#define BOARD	0

void
gasnetc_sendbuf_init()
{
	int	i, j;
	int	stoks, rtoks, rtoks_lo, rtoks_hi;
	int	dma_size;

	assert(_gmc.port != NULL);

	stoks = gm_num_send_tokens(_gmc.port);
	_gmc.token.max = stoks;
	_gmc.token.hi = _gmc.token.lo = _gmc.token.total = 0;

	rtoks = gm_num_receive_tokens(_gmc.port);
	rtoks_lo = rtoks/2;
	rtoks_hi = rtoks - rtoks_lo;
	_gmc.token.receive = rtoks;

	/* We need to allocate the following types of DMA'd buffers:
	 * 1. 1 AMReplyBuf (handling replies after an AMMediumRequest) 
	 * 2. (stoks-1) AMRequest bufs
	 * 3. rtoks_lo AMRequest receive bufs
	 * 4. rtoks_hi AMReply receive bufs
	 * 5. 1 Scratch DMA bufs (System Messages)
	 *
	 * Note that each of these have a bufdesc_t attached to them
	 */
	_gmc.bd_list_num = 1 + rtoks + stoks-1 + 1;
	dma_size = _gmc.bd_list_num << GASNETC_AM_SIZE;
	GASNETI_TRACE_PRINTF(C, ("TOKENS: send=%d, receive=%d, AMRequest=%d, "
	    "AMReply=%d", stoks, rtoks, rtoks_lo, rtoks_hi) );
	GASNETI_TRACE_PRINTF(C, ("TOKENS: total=%d, total_dma=%d bytes",
	    _gmc.bd_list_num, dma_size) );

	/* Allocate and register DMA buffers */ 
	_gmc.dma_bufs = gm_alloc_pages(dma_size);
	if_pf (_gmc.dma_bufs == NULL)
		gasneti_fatalerror("gm_alloc_pages(%d) %s", dma_size,
		   gasneti_current_loc);
	gm_register_memory(_gmc.port, _gmc.dma_bufs, dma_size);

	/* Allocate the AMRequest send buffer pool stack */
	_gmc.reqs_pool = (int *) 
	    gasneti_malloc(sizeof(int) * (stoks-1));

	/* Allocate a buffer descriptor (bufdesc_t) for each DMA'd buffer
	 * and fill in id/sendbuf for cheap reverse lookups */
	_gmc.bd_ptr = (gasnetc_bufdesc_t *)
	    gasneti_malloc(_gmc.bd_list_num * sizeof(gasnetc_bufdesc_t));
	for (i = 0; i < _gmc.bd_list_num; i++) {
		_gmc.bd_ptr[i].id = i;
		_gmc.bd_ptr[i].sendbuf = 
		    _gmc.dma_bufs + (i<<GASNETC_AM_SIZE);
	}
	/* stoks-1 AMRequest send in FIFO */
	for (i = rtoks, j = 0; i < rtoks+stoks-1; i++, j++)
		_gmc.reqs_pool[j] = i;
	_gmc.reqs_pool_cur = _gmc.reqs_pool_max = j-1;
	/* fifo_max is the last possible fifo element */

	GASNETI_TRACE_PRINTF(C, 
	    ("0-%d:AMRequest LOW\t%d-%d:AMReply HIGH\t%d-%d:AMRequest FIFO",
	    rtoks_lo-1, rtoks_lo, rtoks-1, rtoks, rtoks+stoks-2) );

	/* use the omitted send token for the AMReply buf */
	_gmc.AMReplyBuf = &_gmc.bd_ptr[i]; 
	_gmc.scratchBuf = _gmc.dma_bufs + ((++i)<<GASNETC_AM_SIZE);
	_gmc.ReplyCount = 0;
}

void
gasnetc_sendbuf_finalize()
{
	if (_gmc.dma_bufs != NULL)
		gm_free_pages(_gmc.dma_bufs, 
		    _gmc.bd_list_num << GASNETC_AM_SIZE);
	if (_gmc.bd_ptr != NULL)
		gasneti_free(_gmc.bd_ptr);
	if (_gmc.reqs_pool != NULL)
		gasneti_free(_gmc.reqs_pool);
}

void
gasnetc_provide_receive_buffers()
{
	int i;
	int rtoks_lo, rtoks_hi;
	
	rtoks_lo = _gmc.token.receive/2;
	rtoks_hi = _gmc.token.receive - rtoks_lo;

	/* Extra check to make sure we recovered all our receive
	 * and send * tokens once the system is bootstrapped */
	assert(_gmc.token.max == gm_num_send_tokens(_gmc.port));
	assert(_gmc.token.receive == gm_num_receive_tokens(_gmc.port));

	/* Provide GM with rtoks_lo AMRequest receive LOW */
	for (i = 0; i < rtoks_lo; i++) { 
		gm_provide_receive_buffer(_gmc.port,
		    _gmc.dma_bufs + (i<<GASNETC_AM_SIZE), GASNETC_AM_SIZE, 
		    GM_LOW_PRIORITY);
	}
	/* Provide GM with rtoks_hi AMReply receive HIGH */
	for (i = rtoks_lo; i < _gmc.token.receive; i++) {
		gm_provide_receive_buffer(_gmc.port,
		    _gmc.dma_bufs + (i<<GASNETC_AM_SIZE), GASNETC_AM_SIZE, 
		    GM_HIGH_PRIORITY);
	}
}


int	
gasnetc_gm_nodes_compare(const void *k1, const void *k2)
{
	gasnetc_gm_nodes_rev_t	*a = (gasnetc_gm_nodes_rev_t *) k1;
	gasnetc_gm_nodes_rev_t	*b = (gasnetc_gm_nodes_rev_t *) k2;

	if (a->id > b->id || a->port > b->port)
		return 1;
	else if (a->id == b->id && a->port == b->port)
		return 0;
	else
		return -1;
}

void
gasnetc_tokensend_AMRequest(void *buf, uint16_t len, 
		uint32_t id, uint32_t port,
		gm_send_completion_callback_t callback, 
		void *callback_ptr, uintptr_t dest_addr)
{
	int sent = 0;

	while (!sent) {
		/* don't force locking when polling */
		while (!GASNETC_TOKEN_LO_AVAILABLE())
			gasnetc_AMPoll();

		GASNETC_GM_MUTEX_LOCK;
		/* assure last poll was successful */
		if (GASNETC_TOKEN_LO_AVAILABLE()) {
			gasnetc_gm_send_AMRequest(buf, len, id, port, callback,
			    callback_ptr, dest_addr);
			_gmc.token.lo += 1;
			_gmc.token.total += 1;
			GASNETC_GM_MUTEX_UNLOCK;
			sent = 1;
		}
		else {
			GASNETC_GM_MUTEX_UNLOCK;
		}
	}
}

gasnetc_bufdesc_t *
gasnetc_AMRequestPool_block() 
{
	int	bufd_idx = -1;

	/* Since every AMRequest send must go through the Pool, use this
	 * as an entry point to make progress in the Receive queue */
	gasnetc_AMPoll();

	while (bufd_idx < 0) {
		while (_gmc.reqs_pool_cur < 1)
			gasnetc_AMPoll();

		GASNETC_REQUEST_POOL_MUTEX_LOCK;
		if_pt (_gmc.reqs_pool_cur > 0) {
			bufd_idx = _gmc.reqs_pool[--_gmc.reqs_pool_cur];
			/*
			GASNETI_TRACE_PRINTF(C, 
			    ("AMRequestPool (%d/%d) gave bufdesc id %d",
	    		    _gmc.reqs_pool_cur, _gmc.reqs_pool_max,
	    		    _gmc.reqs_pool[_gmc.reqs_pool_cur]) );
			 */
			GASNETC_REQUEST_POOL_MUTEX_UNLOCK;
		}
		else 
			GASNETC_REQUEST_POOL_MUTEX_UNLOCK;  /* can't get bufd */
	}
	assert(bufd_idx < _gmc.bd_list_num);
	assert(_gmc.bd_ptr[bufd_idx].sendbuf != NULL);
	return (gasnetc_bufdesc_t *) &_gmc.bd_ptr[bufd_idx];
}

/* This function is not thread safe as it is guarenteed to be called
 * from only one thread, during initialization */
void
gasnetc_SysBarrier()
{
	int		count = 1;
	uintptr_t	*scratchPtr;

	scratchPtr = (uintptr_t *) _gmc.scratchBuf;

	if (gasnetc_mynode == 0) {
		while (count < gasnetc_nodes) {
			gm_provide_receive_buffer(_gmc.port, 
			    (void *) &scratchPtr, GASNETC_SYS_SIZE, 
			    GM_HIGH_PRIORITY);
			if (gasnetc_SysPoll((void *)&count) != BARRIER_GATHER)
				gasneti_fatalerror("System Barrier did not "
				    "receive a BARRIER_GATHER! fatal");
		}
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, BARRIER_NOTIFY);
		gasnetc_gm_send_AMSystem_broadcast((void *) scratchPtr, 1,
		    gasnetc_callback_AMReply_NOP, NULL, 0);
		return;
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, BARRIER_GATHER);
		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
					"unexpected message while "
					"recuperating tokens");
		}
		gasnetc_gm_send_AMSystem((void *) scratchPtr, 1,
			_gmc.gm_nodes[0].id, _gmc.gm_nodes[0].port, 
			gasnetc_callback_AMReply_NOP, NULL);
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnetc_Sysbarrier: Sent GATHER, waiting for NOTIFY") );
		gm_provide_receive_buffer(_gmc.port, (void *) &scratchPtr, 
		    GASNETC_SYS_SIZE, GM_HIGH_PRIORITY);
		if (gasnetc_SysPoll(NULL) != BARRIER_NOTIFY)
			gasneti_fatalerror("expected BARRIER_NOTIFY, fatal");
		return;
	}
}
			
/* Again, not thread-safe for the same reasons as above. */
void
gasnetc_gm_send_AMSystem_broadcast(void *buf, size_t len,
		gm_send_completion_callback_t callback,
		void *callback_ptr, int recover)
{
	int			i;
	int			token_hi;
	gasnetc_sysmsg_t	sysmsg;

	assert(buf != NULL);
	assert(len >= 1);
	assert(callback != NULL);
	assert(gasnetc_mynode == 0);

	if (recover) 
		token_hi = _gmc.token.hi;

	GASNETI_TRACE_PRINTF(C, ("gm_send_AMSystemBroadcast len=%d data=0x%x",
	     len, *((uint8_t *)buf)) );

	for (i = 1; i < gasnetc_nodes; i++) {
		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
		gasnetc_gm_send_AMSystem(buf, len, _gmc.gm_nodes[i].id,
		    _gmc.gm_nodes[i].port, callback, callback_ptr);
	}

	if (recover) {
		while (_gmc.token.hi != token_hi) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
	}
	return;
}

/* Allocate segments */
void *
gasnetc_segment_gather(uintptr_t sbrk_local)
{
	uintptr_t	*scratchPtr;
	uintptr_t	sbrk_global;
	size_t		pagesize;

	scratchPtr = (uintptr_t *) _gmc.scratchBuf;
	pagesize = gasneti_getSystemPageSize();

	assert((_gmc.token.hi == 0) && (_gmc.token.total == 0));
	assert(_gmc.gm_nodes[0].id > 0);

	if (gasnetc_mynode == 0) {
		int count = 1;
		uintptr_t sbrk_high = GASNETC_ALIGN(sbrk(0), pagesize);
		GASNETI_TRACE_PRINTF(C, ("SBRK(0) = 0x%x, aligned to 0x%x\n",
		    (uintptr_t) sbrk(0), sbrk_high) );
		while (count < gasnetc_nodes) {
			gm_provide_receive_buffer(_gmc.port, 
			    (void *) &scratchPtr, GASNETC_SYS_SIZE, 
			    GM_HIGH_PRIORITY);
			if (gasnetc_SysPoll((void *) &sbrk_global) != SBRK_TOP)
				gasneti_fatalerror("expected SBRK_TOP, fatal");
			if (sbrk_global > sbrk_high)
				sbrk_high = sbrk_global;
		}
		GASNETI_TRACE_PRINTF(C, ("SBRK HIGH = 0x%x\n", sbrk_high) );
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SBRK_BASE);
		scratchPtr[1] = sbrk_high;
		gasnetc_gm_send_AMSystem_broadcast((void *) scratchPtr, 
		    2*sizeof(uintptr_t), gasnetc_callback_AMReply_NOP, 
		    NULL, 0);

		return (void *) sbrk_high;
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SBRK_TOP);

		if (sbrk_local == 0) {
			scratchPtr[1] = GASNETC_ALIGN(sbrk(0), pagesize);
			GASNETI_TRACE_PRINTF(C, ("SBRK(0) = 0x%x, aligned to 0x%x\n",
			    (uintptr_t) sbrk(0), scratchPtr[1]));
		}
		else
			scratchPtr[1] = sbrk_local;
		assert(scratchPtr[1] > 0);

		gasnetc_gm_send_AMSystem((void *) scratchPtr, 
		    2*sizeof(uintptr_t), _gmc.gm_nodes[0].id, 
		    _gmc.gm_nodes[0].port, gasnetc_callback_AMReply_NOP, NULL);

		if (gasnetc_SysPoll((void *) &sbrk_global) != SBRK_BASE)
			gasneti_fatalerror("expected SBRK_BASE, fatal");
		return (void *) sbrk_global;
	}
}

int
gasnetc_gmpiconf_init()
{

	FILE		*fp;
	char		line[_BUFSZ+1];
	char		gmconf[_BUFSZ+1];
	char		gmhost[_BUFSZ+1], hostname[MAXHOSTNAMELEN+1];
	char		**hostnames;
	char		*homedir;
	int		lnum = 0, gmportnum, i;
	int		thisport = 0, thisid = 0, numnodes = 0, thisnode = -1;
	gm_status_t	status;
	struct gm_port	*p;

	if ((homedir = getenv("HOME")) == NULL)
		GASNETI_RETURN_ERRR(RESOURCE, "Couldn't find $HOME directory");

	snprintf(gmconf, _BUFSZ, "%s/.gmpi/conf", homedir);

	if (gethostname(hostname,_BUFSZ) < 0)
		GASNETI_RETURN_ERRR(RESOURCE, "Couldn't get local hostname");

	if ((fp = fopen(gmconf, "r")) == NULL) {
		fprintf(stderr, "%s unavailable\n", gmconf);
		GASNETI_RETURN_ERRR(RESOURCE, 
		    "Couldn't open GMPI configuration file");
	}

	/* must do gm_init() from this point on since gm_host_name_to_node_id
	 * must use the port
	 */

	while (fgets(line, _BUFSZ, fp)) {
	
		if (lnum == 0) {
	      		if ((sscanf(line, "%d\n", &numnodes)) < 1) 
				GASNETI_RETURN_ERRR(RESOURCE, 
				    "job size not found in GMPI config file");
	      		else if (numnodes < 1) 
				GASNETI_RETURN_ERRR(RESOURCE, 
				    "invalid numnodes in GMPI config file");

			_gmc.gm_nodes = (gasnetc_gm_nodes_t *) 
			    gasneti_malloc(numnodes*sizeof(gasnetc_gm_nodes_t));

			_gmc.gm_nodes_rev = (gasnetc_gm_nodes_rev_t *) 
			    gasneti_malloc(numnodes *
			    sizeof(gasnetc_gm_nodes_rev_t));

			hostnames = (char **)
			    gasneti_malloc((numnodes+1)*sizeof(char *));
			hostnames[numnodes] = NULL;
			for (i = 0; i < numnodes; i++) {
				hostnames[i] =
				gasneti_malloc(MAXHOSTNAMELEN);
			}
			lnum++;
	      	}

		else if (lnum <= numnodes) {
			if ((sscanf(line,"%s %d\n",gmhost,&gmportnum)) == 2) {
				if (gmportnum < 1 || gmportnum > 7)
					GASNETI_RETURN_ERRR(RESOURCE, 
					    "Invalid GM port");

				assert(gmhost != NULL);

				_gmc.gm_nodes[lnum-1].port = gmportnum;
				memcpy(&hostnames[lnum-1][0], 
				    (void *)gmhost, MAXHOSTNAMELEN);

				if (strcasecmp(gmhost, hostname) == 0) {
					GASNETI_TRACE_PRINTF(C,
					    ("%s will bind to port %d\n", 
					    hostname, gmportnum) );
					thisnode = lnum-1;
					thisport = gmportnum;
				}
			}
                        else {
				printf("couldn't parse: %s\n", line);
			}
			lnum++;
		}
	}
	
	fclose(fp);

	if (numnodes == 0 || thisnode == -1)
		GASNETI_RETURN_ERRR(RESOURCE, 
		    "could not find myself in GMPI config file");
	gm_init();
	status = 
		gm_open(&p,BOARD,thisport,"GASNet GMPI Emulation Bootstrap", 
		    GM_API_VERSION_1_4);
	if (status != GM_SUCCESS) 
		GASNETI_RETURN_ERRR(RESOURCE, "could not open GM port");
	status = gm_get_node_id(p, &thisid);
	if (status != GM_SUCCESS)
		GASNETI_RETURN_ERRR(RESOURCE, "could not get GM node id");

	for (i = 0; i < numnodes; i++) {
		_gmc.gm_nodes[i].id = 
		    gm_host_name_to_node_id(p, hostnames[i]);

		if (_gmc.gm_nodes[i].id == GM_NO_SUCH_NODE_ID) {
			fprintf(stderr, "%d has no id! Check mapper\n",
			    _gmc.gm_nodes[i].id);
			GASNETI_RETURN_ERRR(RESOURCE, 
			    "Unknown GMid or GM mapper down");
		}
		_gmc.gm_nodes_rev[i].id = _gmc.gm_nodes[i].id;
		_gmc.gm_nodes_rev[i].node = (gasnet_node_t) i;

		GASNETI_TRACE_PRINTF(C, ("%d> %s (gm %d, port %d)\n", 
		    i, hostnames[i], _gmc.gm_nodes[i].id, 
		    _gmc.gm_nodes[i].port));

	}

	gasnetc_mynode = thisnode;
	gasnetc_nodes = numnodes;
	for (i = 0; i < numnodes; i++)
		gasneti_free_inhandler(hostnames[i]);
	gasneti_free_inhandler(hostnames);

	/* sort out the gm_nodes_rev for bsearch, glibc qsort uses recursion,
	 * so stack memory in order to complete the sort.  We want to minimize
	 * the number of mallocs
	 */
	qsort(_gmc.gm_nodes_rev, numnodes, sizeof(gasnetc_gm_nodes_rev_t),
	    gasnetc_gm_nodes_compare);
	_gmc.port = p;
	return GASNET_OK;
}

void
gasnetc_AM_InitHandler()
{
	int	i;

	for (i = 0; i < GASNETC_AM_MAX_HANDLERS; i++) 
		_gmc.handlers[i] = (gasnetc_handler_fn_t) abort;  

	return;
}

int
gasnetc_AM_SetHandler(gasnet_handler_t handler, gasnetc_handler_fn_t func)
{
	if (!handler || func == NULL)
		GASNETI_RETURN_ERRR(BAD_ARG, "Invalid handler paramaters set");
		
	_gmc.handlers[handler] = func;
	return GASNET_OK;
}

int
gasnetc_AM_SetHandlerAny(gasnet_handler_t *handler, gasnetc_handler_fn_t func)
{
	int	i;

	if (handler == NULL || func == NULL)
		GASNETI_RETURN_ERRR(BAD_ARG, "Invalid handler paramaters set");

	for (i = 1; i < GASNETC_AM_MAX_HANDLERS; i++) {
		if (_gmc.handlers[i] == abort) {
			_gmc.handlers[i] = func;
			*handler = i;
			return GASNET_OK;
		}
	}
	return GASNET_OK;
}
