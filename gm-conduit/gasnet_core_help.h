/* $Id: gasnet_core_help.h,v 1.5 2002/06/30 00:32:50 csbell Exp $
 * $Date: 2002/06/30 00:32:50 $
 * $Revision: 1.5 $
 * Description: GASNet gm conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

extern gasnet_node_t 		gasnetc_mynode;
extern gasnet_node_t 		gasnetc_nodes;

/* handler prototypes */
typedef void (*gasnetc_HandlerShort) (void *token, ...);
typedef void (*gasnetc_HandlerMedium)(void *token, void *buf, int nbytes, ...);
typedef void (*gasnetc_HandlerLong)  (void *token, void *buf, int nbytes, ...);

/* AM Header stores the following fields
 *
 * 0b76543210
 *
 * 7-6: AM Message type (00=small, 01=medium, 10=long, 11=system)
 * 5-1: AM Number of arguments (00000=0, 00001=1, 00010=2, ...)
 * 0:   AM Request/Reply (0=request, 1=reply)
 *
 * AM Index stores the handler index and is one byte next to the 
 * Header
 */


#define GASNETC_AM_SHORT	0x00
#define GASNETC_AM_MEDIUM	0x01
#define GASNETC_AM_LONG		0x02
#define GASNETC_AM_SYSTEM	0x03

#define	GASNETC_AM_REPLY	0x00
#define GASNETC_AM_REQUEST	0x01

#define GASNETC_AM_SIZE		12 
#define GASNETC_AM_LEN		(1<<GASNETC_AM_SIZE)
#define GASNETC_AM_PACKET	(GASNETC_AM_LEN-8)
#define GASNETC_SYS_SIZE	5
#define GASNETC_AM_MAX_ARGS	16
#define GASNETC_AM_MAX_HANDLERS 255

#define GASNETC_AM_SHORT_ARGS_OFF	4
#define GASNETC_AM_MEDIUM_ARGS_OFF	4
#define GASNETC_AM_LONG_ARGS_OFF	(8+sizeof(uintptr_t))

#define GASNETC_AM_MEDIUM_HEADER_PAD(numargs) (((numargs)%2==1) ? 0 : 4)
/* XXX need something at compile time for sizeof(uintptr_t) */
#define GASNETC_AM_LONG_PAD(numargs) (((numargs)%2==1) ? 0 : 4)
/* The case for uintptr_t = 8 bytes would be the opposite:
 * See Long Header description in gasnetc_write_AMBufferLong
 * #define GASNETC_AM_LONG_PAD(numargs) (((numargs)%2==0) ? 0 : 4)
 */
#define GASNETC_AM_SHORT_HEADER_LEN(numargs)                     \
			((numargs)*4 + GASNETC_AM_SHORT_ARGS_OFF)
#define GASNETC_AM_MEDIUM_HEADER_LEN(numargs)                     \
			((numargs)*4 + GASNETC_AM_MEDIUM_ARGS_OFF \
			+ GASNETC_AM_MEDIUM_HEADER_PAD(numargs))
#define GASNETC_AM_LONG_HEADER_LEN(numargs)                     \
			((numargs)*4 + GASNETC_AM_LONG_ARGS_OFF \
			+ GASNETC_AM_LONG_PAD(numargs))

/* -------------------------------------------------------------------------- */
#define GASNETC_AM_MEDIUM_MAX                                     \
		(GASNETC_AM_PACKET -                              \
		 GASNETC_AM_MEDIUM_HEADER_LEN(GASNETC_AM_MAX_ARGS))
#define GASNETC_AM_LONG_REPLY_MAX	                        \
		(GASNETC_AM_PACKET -                            \
		 GASNETC_AM_LONG_HEADER_LEN(GASNETC_AM_MAX_ARGS))
#define GASNETC_AM_LONG_REQUEST_MAX                             \
		(3*GASNETC_AM_PACKET + GASNETC_AM_PACKET -      \
		 GASNETC_AM_LONG_HEADER_LEN(GASNETC_AM_MAX_ARGS))

/* -------------------------------------------------------------------------- */
#define GASNETC_AM_NUMARGS(c)   (((c) >> 1) & 0x1f)
#define GASNETC_SYS_INDEX(c)	((c) & 0x3f) 
#define GASNETC_AM_TYPE(c)      ((c) >> 6)
#define GASNETC_AM_IS_SYSTEM(c)  ((GASNETC_AM_SYSTEM & ((c)>>6)) == \
		                  GASNETC_AM_SYSTEM)
#define GASNETC_AM_IS_REQUEST(c) (!GASNETC_AM_IS_SYSTEM((c)) && \
		                  ((c) & GASNETC_AM_REQUEST))
#define GASNETC_AM_IS_REPLY(c)   (!GASNETC_AM_IS_SYSTEM((c)) && \
		                  !((c) & GASNETC_AM_REQUEST))
#define GASNETC_AM_TYPE_STRING(buf)                                            \
                (GASNETC_AM_TYPE(buf) == GASNETC_AM_SHORT ? "Short" :	       \
                        (GASNETC_AM_TYPE(buf) == GASNETC_AM_MEDIUM ? "Medium": \
                                (GASNETC_AM_TYPE(buf) == GASNETC_AM_LONG ?     \
				 "Long" : "Error!")))
#define GASNETC_ALIGN(p,P)	((uintptr_t)(p)&~ ((uintptr_t)(P)-1))
#define GASNETC_GMNODE(id,port) gasnetc_gm_nodes_search(gm_ntoh_u16((id)), \
				    (uint16_t) gm_ntoh_u8((port)))
/* -------------------------------------------------------------------------- */
#define GASNETC_ASSERT_AMSHORT(buf, type, handler, args, req) 		\
	do { 	assert(buf != NULL); 					\
		assert(type >= GASNETC_AM_SHORT && 			\
				type <= GASNETC_AM_SYSTEM);		\
		assert(req == 0 || req == 1);				\
		assert(numargs >= 0 && numargs <= 16);			\
	} while (0)

#define GASNETC_ASSERT_AMMEDIUM(buf, type, handler, args, req, len, src) \
	do {	GASNETC_ASSERT_AMSHORT(buf, type, handler, args, req);   \
		assert(len > 0);					 \
		assert(src != NULL);					 \
	} while (0)

#define GASNETC_ASSERT_AMLONG(buf, type, handler, args, req, len, src, dest) \
	do {	GASNETC_ASSERT_AMMEDIUM(buf, type, handler, args, req, len,  \
			                src);				     \
		assert(dest != NULL);					     \
	} while (0)

/* -------------------------------------------------------------------------- */
/* Debug, tracing */
#ifdef TRACE
#define _GASNETC_GMNODE_REPLY gasnetc_gm_nodes_search(			       \
				gm_ntoh_u16((bufd)->e->recv.sender_node_id),   \
				(uint16_t)                                     \
				    gm_ntoh_u8((bufd)->e->recv.sender_port_id))

/* Generic Trace debug for AM handlers (gasnet_core) or elsewhere */
#define GASNETC_TRACE_SHORT(reqrep, type, dest, token, idx, args)	       \
		GASNETI_TRACE_PRINTF(C,("%s%s\t%d token=0x%x index=%d args=%d",\
			#reqrep, #type, dest, (uintptr_t) token, idx, args)) 

#define GASNETC_TRACE_MEDIUM(reqrep, type, dest, token, idx, args, sAddr,len)  \
		GASNETI_TRACE_PRINTF(C,("%s%s\t%d token=0x%x index=%d args=%d "\
			"src=0x%x len=%d", #reqrep, #type, dest,               \
			(uintptr_t) token, idx, args, (uintptr_t) sAddr, len))

#define GASNETC_TRACE_LONG(reqrep, type, dest, token, idx, args, sAddr, dAddr, \
		len) GASNETI_TRACE_PRINTF(C,("%s%s\t%d token=0x%x index=%d "   \
			"args=%d src=0x%x dst=0x%x len=%d", #reqrep, #type,    \
			dest, (uintptr_t) token, idx, args, (uintptr_t) sAddr, \
			(uintptr_t) dAddr, len))

/* Formats AM-only debug tracing for _GASNET_TRACE functions, can only be
 * called from gasnet_AM...M() functions from gasnet_core.c
 */
#define _GASNETC_AMTRACE_SHORT(reqrep, type, dest, token) 		       \
		GASNETC_TRACE_SHORT(reqrep, type, dest, token, handler, numargs)
#define _GASNETC_AMTRACE_MEDIUM(reqrep, type, dest, token) 		       \
		GASNETC_TRACE_MEDIUM(reqrep, type, dest, token, handler,       \
		numargs, source_addr, nbytes)
#define _GASNETC_AMTRACE_LONG(reqrep, type, dest, token) 		       \
		GASNETC_TRACE_LONG(reqrep, type, dest, token, handler, numargs,\
		source_addr, dest_addr, nbytes)

#define GASNETC_AMTRACE_ReplyShort(str) _GASNETC_AMTRACE_SHORT(str,            \
		AMReplyShort, _GASNETC_GMNODE_REPLY, token)
#define GASNETC_AMTRACE_RequestShort(str) _GASNETC_AMTRACE_SHORT(str,          \
		AMRequestShort, dest, 0)

#define GASNETC_AMTRACE_ReplyMedium(str) _GASNETC_AMTRACE_MEDIUM(str,          \
		AMReplyMedium, _GASNETC_GMNODE_REPLY, token)
#define GASNETC_AMTRACE_RequestMedium(str) _GASNETC_AMTRACE_MEDIUM(str,        \
		AMReplyMedium, dest, 0)

#define GASNETC_AMTRACE_ReplyLong(str) _GASNETC_AMTRACE_LONG(str, AMReplyLong, \
		_GASNETC_GMNODE_REPLY, token)
#define GASNETC_AMTRACE_RequestLong(str) _GASNETC_AMTRACE_LONG(str,	       \
		AMReplyLong, dest, 0)

#define GASNETC_AMTRACE_ReplyLongAsyncM GASNETC_AMTRACE_ReplyLongM
#define GASNETC_AMTRACE_RequestLongAsyncM GASNETC_AMTRACE_RequestLongM

#define GASNETC_TRACE_FIFO(bufd) do {					       \
		uint8_t idx = *((uint8_t *)(bufd)->sendbuf+1);		       \
		uint8_t args = GASNETC_AM_NUMARGS(*recv_buf);		       \
		switch (*((uint8_t *)(bufd)->sendbuf)) {		       \
			case GASNETC_AM_SHORT:				       \
				GASNETC_TRACE_SHORT(SendFifo, AMRequestShort,  \
				_GASNETC_GMNODE_REPLY, (bufd), idx, args);     \
				break; 					       \
			case GASNETC_AM_MEDIUM:				       \
				GASNETC_TRACE_MEDIUM(SendFifo, AMRequestMedium,\
				_GASNETC_GMNODE_REPLY, (bufd), idx, args,      \
				(bufd)->sendbuf+			       \
				GASNETC_AM_MEDIUM_HEADER_LEN(args),	       \
				(bufd)->len); break;			       \
			case GASNETC_AM_LONG:				       \
				GASNETC_TRACE_LONG(SendFifo, AMRequestLong,    \
				_GASNETC_GMNODE_REPLY, (bufd), idx, args,      \
				(bufd)->rmda_off, len); break;		       \
			default:					       \
				gasneti_fatalerror("FIFO can't trace!");       \
				break;					       \
		}							       \
	} while (0)
#else
/* NO tracing enabled */
#define GASNETI_TRACE_PRINTF(type,args)
#define GASNETC_TRACE_SHORT(reqrep, type, dest, token, idx, args)
#define GASNETC_TRACE_MEDIUM(reqrep, type, dest, token, idx, args, sAddr,len) 
#define GASNETC_TRACE_LONG(reqrep, type, dest, token, idx, args, sAddr, dAddr, \
		len)
#define GASNETC_AMTRACE_ReplyShort(str) 
#define GASNETC_AMTRACE_RequestShort(str) 
#define GASNETC_AMTRACE_ReplyMedium(str) 
#define GASNETC_AMTRACE_RequestMedium(str) 
#define GASNETC_AMTRACE_ReplyLong(str) 
#define GASNETC_AMTRACE_RequestLong(str) 
#define GASNETC_AMTRACE_ReplyLongAsyncM
#define GASNETC_AMTRACE_RequestLongAsyncM
#define GASNETC_TRACE_FIFO(bufd)
#endif

/* -------------------------------------------------------------------------- */
#define GASNETC_ASSERT_BUFDESC_PTR(bufd, ptr) do {                   \
		assert((bufd)->id ==                                 \
		    (((ptr) - _gmc.dma_bufs) >> GASNETC_AM_SIZE));   \
		} while (0)
#define GASNETC_BUFDESC_PTR(x) &_gmc.bd_ptr[                              \
				(((x) - _gmc.dma_bufs) >> GASNETC_AM_SIZE)]
#define GASNETC_GM_RECV_PTR(e,fast)				\
	(fast) ? (uint8_t *) gm_ntohp((e)->recv.message) :	\
	    (uint8_t *) gm_ntohp((e)->recv.buffer)

#define GASNETC_SYSHEADER_WRITE(buf, index)				\
	*((uint8_t *)(buf)) = (0xc0 | ((index) & 0x3f))

#define GASNETC_SYSHEADER_READ(buf, sysmsg)				\
	sysmsg = (gasnetc_sysmsg_t) (*((uint8_t *)(buf)) & 0x3f)

#define GASNETC_AMHEADER_WRITE(buf, type, args, req) 			       \
	*((uint8_t *)(buf)) = (((uint8_t)(type)<< 6) | ((uint8_t)(args)<< 1) | \
				        ((uint8_t)(req) & 0x01) )
#define GASNETC_AMHANDLER_WRITE(buf, handler)				\
	*((uint8_t *)(buf)) = (uint8_t)(handler)
#define GASNETC_AMHANDLER_READ(buf, handler)				\
	(uint8_t)(handler) = *((uint8_t *)(buf))

#define GASNETC_AMDESTADDR_WRITE(buf, dest_addr)         		\
	*((uintptr_t *)(buf)) = (uintptr_t)(dest_addr)
#define GASNETC_AMDESTADDR_READ(buf, dest_addr)                     	\
	(uintptr_t)(dest_addr) = *((uintptr_t *)(buf))

#define GASNETC_AMLENGTH_WRITE(buf, len) *((uint16_t *)(buf)) = (uint16_t)(len)
#define GASNETC_AMLENGTH_READ(buf, len)	 (uint16_t)(len) = *((uint16_t *)(buf))
#define GASNETC_AMLENGTH_WRITE4(buf, len) *((uint32_t *)(buf)) = (uint32_t)(len)
#define GASNETC_AMLENGTH_READ4(buf, len) (uint32_t)(len) = *((uint32_t *) (buf))

#define GASNETC_ARGS_WRITE(buf, argptr, numargs)			\
	do { 	int _i; int32_t *_pbuf = (int32_t *) buf;		\
		for (_i = 0; _i < (numargs); _i++)	{		\
			_pbuf[_i] = (int32_t) va_arg((argptr), int);  	\
		}							\
	   } while (0)

#define GASNETC_AMPAYLOAD_WRITE(dest, src, len)	memcpy((dest),(src),(len))
#define GASNETC_AMPAYLOAD_READ GASNETC_AMPAYLOAD_WRITE

/* -------------------------------------------------------------------------- */
#define GASNETC_RUN_HANDLER_SHORT(pfn, token, pArgs, numargs) do { 	       \
	assert(pfn);						               \
  	if (numargs == 0) (*(gasnetc_HandlerShort)pfn)((void *)token); 	       \
	else {								       \
    		uint32_t *args = (uint32_t *)(pArgs); /* eval only once */     \
    		switch (numargs) {					       \
		case 1: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0]);  \
			break;						       \
		case 2: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1]); break;	       			       \
		case 3: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2]); break; 			       \
		case 4: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3]); break; 		       \
	     	case 5: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4]); break;   	       \
	     	case 6: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2],  args[3], args[4], args[5]); break;  \
	     	case 7: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6]); \
			break;	       					       \
	     	case 8: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7]); break;   				       \
	     	case 9: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8]); break; 			       \
	     	case 10:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9]); break; 		       \
	     	case 11:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10]); break; 	       \
	     	case 12:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11]); break; \
	     	case 13:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12]); break; 				       \
	     	case 14:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12], args[13]); break; 			       \
	     	case 15:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12], args[13], args[14]); break; 		       \
	     	case 16:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12], args[13], args[14], args[15]); break;        \
	     	default: abort();  					       \
	     }								       \
	   }								       \
	 } while (0)

/* -------------------------------------------------------------------------- */
#define _GASNETC_RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs,        \
		pData, datalen) do {					       \
	assert(phandlerfn);						       \
  	if (numargs == 0) (*phandlerfn)(token, pData, datalen);	               \
	else {								       \
    		uint32_t *args = (uint32_t *)(pArgs); /* eval only once */     \
    		switch (numargs) {					       \
      		case 1: (*phandlerfn)(token, pData, datalen, args[0]); break;  \
		case 2: (*phandlerfn)(token, pData, datalen, args[0], args[1]);\
			break;						       \
		case 3: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2]); break; 				       \
		case 4: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3]); break; 			       \
	     	case 5: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4]); break; 		       \
	     	case 6: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5]); break;          \
	     	case 7: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			 args[2], args[3], args[4], args[5], args[6]);         \
			 break;						       \
	     	case 8: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7]); break; 				       \
	     	case 9: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8]); break; 			       \
	     	case 10:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9]); break; 		       \
	     	case 11:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10]); break;         \
	     	case 12:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11]);      \
			  break; 					       \
	     	case 13:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12]); break; 				       \
	     	case 14:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12], args[13]); break; 			       \
	     	case 15:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6], 	       \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12], args[13], args[14]); break; 	       \
	     	case 16:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12], args[13], args[14], args[15]); break;      \
	     	default: abort();  					       \
	     }								       \
	   }								       \
	 } while (0)

/* -------------------------------------------------------------------------- */
#define GASNETC_RUN_HANDLER_MEDIUM(phandlerfn, token, pArgs, numargs, pData,   \
		                   datalen)				       \
        _GASNETC_RUN_HANDLER_MEDLONG((gasnetc_HandlerMedium)phandlerfn,        \
				 (void *)token, pArgs, numargs, (void *)pData, \
				 (int)datalen)

#define GASNETC_RUN_HANDLER_LONG(phandlerfn, token, pArgs, numargs, pData,     \
		                   datalen)				       \
        _GASNETC_RUN_HANDLER_MEDLONG((gasnetc_HandlerLong)phandlerfn,          \
				 (void *)token, pArgs, numargs, (void *)pData, \
				 (int)datalen)
/* -------------------------------------------------------------------------- */

END_EXTERNC

#endif
