/* $Id: gasnet_core_misc.c,v 1.1 2002/06/10 07:54:52 csbell Exp $
 * $Date: 2002/06/10 07:54:52 $
 * $Revision: 1.1 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet_core_internal.h>

#ifdef DEBUG
#define GASNETC_BUFDESC_PTR(x) (((x) - _gmc.dma_bufs) % GASNETC_AM_LEN == 0 ?	\
				gmc.bd_ptr[((x - _gm.dma_bufs) >> GASNETC_AM_SIZE)] :	\
				0)
#else
#define GASNETC_BUFDESC_PTR(x) (_gmc.bd_ptr[((x - _gm.dma_bufs) >> GASNETC_AM_SIZE)])
#endif
#define GASNETC_TOKEN_PTR(x)	(gasnet_token_t) GASNETC_BUFDESC_PTR(x)
#define	_BUFSZ	128
#define BOARD	0

/*
 * Memory management for token_lo buffers (AMRequests)
 */
void
gasnetc_sendbuf_init()
{
	int	stoks, rtoks, rtoks_lo, rtoks_hi;

	assert(_gmc.port != NULL);

	stoks = gm_num_send_tokens(_gmc.port);
	_gmc.tokens.max = stoks;
	_gmc.tokens.hi = _gmc.tokens.lo = _gmc.tokens.total = 0;

	rtoks = gm_num_receive_tokens(_gmc.port) / 2;
	rtoks_lo = rtoks/2;
	rtoks_hi = rtoks - rtoks_lo;

	/* We need to allocate the following types of buffers
	 *
	 * 1. 1 TransientBuf DMA
	 * 2. stoks send DMA buffers
	 * 3. rtoks receive DMA buffers
	 *
	 * each of these have a buffer_desc_t attached to them.
	 */

	_gmc.bd_list_num = 1 + rtoks + stoks;

	/* 
	 * Allocate and register DMA buffers 
	 */ 
	_gmc.dma_bufs = gm_alloc_pages(GASNETC_AM_LEN * _gmc.bd_list_num);
	if (_gmc.dma_bufs == NULL) exit(-1);
	gm_register_memory(_gmc.port, _gmc.dma_bufs, 
			GASNETC_AM_LEN * _gmc.token.max);

	_gmc.bd_ptr = gasneti_malloc(_gmc.bd_list_num * sizeof(gasnet_bufdesc_t));

	/* give the first buffer to TransientBuf */
	_gmc.TransientBuf = _gmc.bd_ptr;
	_gmc.TransientBuf->sendbuf = _gmc.dma_bufs;

	/* Indexes for buffer descriptors 
	 * 0      : Transient buffer DMA (although unused through index) 
	 * 1-stoks: Send buffer DMA (up to 1-stoks-1 used by AMReply) 
	 * stoks-stoks+rtoks_hi:  Receive buffer DMAs
	 */

	for (i = 1; i <= rtoks_hi; i++) { 
		gm_provide_receive_buffer(_gmc.port,
			_gmc.bd_ptr + (i+stoks)*GASNET_AM_LEN,
			GASNET_AM_SIZE, GM_HI_PRIORITY);
	}
	for (i = rtoks_lo+1, i <= rtoks; i++) {
		gm_provide_receive_buffer(_gmc.port,
			_gmc.bd_ptr + (i+stoks)*GASNET_AM_LEN,
			GASNET_AM_SIZE, GM_LOW_PRIORITY);
	}

	/* 
	 * Fix the FIFO for AMRequests, using an array-based stack up to
	 * stoks - 1
	 */ 
	_gmc.reqs_fifo_bd = gasneti_malloc(sizeof(int) * stoks-1);

	for (i = 0; i < stoks-1; i++)
		_gmc.reqs_fifo_bd[i] = i+1;
	reqs_fifo_cur = i;
}

void
gasnetc_sendbuf_finalize()
{
	gm_free_pages(_gmc.dma_bufs);
	gasneti_free(_gmc.bd_ptr);
	gasneti_free(_gmc.reqs_fifo_bd);
}

void	
gm_node_compare(const void *k1, const void *k2)
{
	gasnetc_gm_nodes_rev_t	*a = (gasnetc_gm_nodes_rev_t *) k1;
	gasnetc_gm_nodes_rev_t	*b = (gasnetc_gm_nodes_rev_t *) k2;

	if (a->id > b->id)
		return 1;
	else if (a->id == b->id)
		return 0;
	else
		return -1;
}

void
gasnetc_tokensend_AMRequest(void *buf, uint16_t len, 
		uint32_t id, uint32_t port,
		gm_send_completion_callback_t callback, uint64_t dest_addr)
{
	int	sent = 0;

	while (!sent) {
		while (!GASNETC_TOKEN_LO_AVAILABLE)
			gasnetc_poll();

		GASNETC_GM_MUTEX_LOCK;
		if (GASNETC_TOKEN_LO_AVAILABLE) {
			gasnetc_gm_send_AMRequest(buf, len, id, port, callback,
					dest_addr);
			GASNETC_GM_MUTEX_UNLOCK;
			sent = 1;
		}
		else {
			GASNETC_GM_MUTEX_UNLOCK;
			goto acquire_lo;
		}
	}
}

gasnetc_bufdesc_t *
gasnetc_AMRequestBuf_block() {
	int	bufd_idx = -1;

	while (bufd_idx < 0) {
		while (_gmc.reqs_fifo_cur < 1)
			gasnetc_poll();

		GASNETC_REQUESTFIFO_MUTEX_LOCK;
		if (_gmc.reqs_fifo_cur > 0) {
			bufd_idx = _gmc.reqs_fifo[++_gmc.reqs_fifo_cur];
			GASNETC_REQUESTFIFO_MUTEX_UNLOCK;
		}
		else {
			GASNETC_REQUESTFIFO_MUTEX_UNLOCK;  /* can't get bufd */
			gasnetc_poll();
		}
	}

	return (gasnetc_bufdesc_t *) 
		_gmc.bd_ptr[bufd_idx*sizeof(gasnetc_bufdesc_t)];
}

int
gasnetc_gmpiconf_init(struct gm_port **p)
{

	FILE		*fp;
	char		line[_BUFSZ];
	char		gmconf[_BUFSZ+1];
	char		gmhost[_BUFSZ+1], hostname[_BUFSZ+1];
	char		**hostnames;
	char		*homedir;
	int		lnum = 0, gmportnum, i;
	int		thisport = 0, thisid = 0, numnodes = 0, thisnode = -1;
	gm_status_t	status;
	struct gm_port	*p;

	if ((homedir = getenv("HOME")) == NULL)
		GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"Couldn't find $HOME directory");

	snprintf(gmconf, _BUFSZ, "%s/.gmpi/conf", homedir);

	if (gethostname(hostname,_BUFSZ) < 0)
		GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"Couldn't get local hostname");

	if ((fp = fopen(gmconf, "r")) == NULL) {
		fprintf(stderr, "%s unavailable\n", gmconf);
		GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"Couldn't open GMPI configuration file");
	}

	/* must do gm_init() from this point on since gm_host_name_to_node_id
	 * must use the port
	 */

	while (fgets(line, _BUFSZ, fp)) {
	
		if (lnum == 0) {
	      		if ((sscanf(line, "%d\n", &numnodes)) < 1) 
				GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"job size not found in GMPI config file");
	      		else if (_thispe.numpes < 1) {
				GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"invalid number of nodes in GMPI config file");

			_gmc.gm_nodes = (gasnetc_gm_nodes_t *) 
				gasneti_malloc_inhandler(numnodes*
					sizeof(gasnetc_gm_nodes_t));

			_gmc.gm_nodes_rev = (gasnetc_gm_nodes_rev_t *) 
				gasneti_malloc_inhandler(numnodes *
					sizeof(gasnetc_gm_nodes_rev_t));

			hostnames = (char **) =
				gasneti_malloc_inhandler((numnodes+1) *
					MAXHOSTNAMELEN);

			hostnames[numnodes+1] = NULL;
			lnum++;
	      	}

		else if (lnum <= numnodes) {
			if ((sscanf(line,"%s %d\n",gmhost,&gmportnum)) == 2) {
				if (gmportnum < 1 || gmportnum > 7)
					GASNETI_RETURN_ERRR(
					GASNET_ERR_RESOURCE, 
					"Invalid GM port");

				_gmc.gm_nodes[lnum-1].port = gmportnum;
				memcpy(hostnames[lnum-1][0], 
					(void *)gmhost, MAXHOSTNAMELEN);

				if (strcasecmp(gmhost, hostname) == 0) {
					DPRINTF(("%s will bind to port %d\n", 
						hostname, gmportnum));

					thisnode = lnum-1;
					thisport = gmportnum;
				}
			}
			lnum++;
		}
	}
	
	fclose(fp);

	if (numnodes == 0 || thisnode == -1)
		GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
			"could not find myself in GMPI config file");
	gm_init();
	status = gm_open(&p,BOARD,thisport,"GASNet GMPI Emulation Bootstrap", 
			    GM_API_VERSION_1_4);
	if (status != GM_SUCCESS) 
		GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"could not open GM port");
	status = gm_get_node_id(p, &thisid);
	if (status != GM_SUCCESS)
		GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"could not get GM node id");

	for (i = 0; i < numnodes; i++) {
		_gmc.gm_nodes[i].id = 
		      gm_host_name_to_node_id(p, hostnames[i]);

		if (_gmc.gm_nodes[i].id == GM_NO_SUCH_NODE_ID) {
			fprintf(stderr, "%s has no id! Check mapper\n",
					_gmc.gm_nodes[i].id);
			GASNETI_RETURN_ERRR(GASNET_ERR_RESOURCE, 
				"Unknown GMid or GM mapper down");
		}
		_gmc.gm_nodes_rev[i].id = _gmc.gm_nodes[i].id;
		_gmc.gm_nodes_rev[i].node = (gasnet_node_t) i;

		DPRINTF("%d> %s (gm %d, port %d)\n", 
				i, hostnames[i], 
				_gmc.gm_nodes[i].id, _gmc.gm_nodes[i].port);

	}
	gasnetc_mynode = thisnode;
	gasnetc_nodes = numnodes;
	gasneti_free_inhandler(hostnames);
	_gmc.port = p;
	return GASNET_OK;
}
