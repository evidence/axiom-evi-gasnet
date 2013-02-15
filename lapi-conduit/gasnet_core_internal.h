/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_internal.h         $
 *     $Date: 2002/07/03 19:41:24 $
 * $Revision: 1.1 $
 * Description: GASNet lapi conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>

/* ------------------------------------------------------------------ */
/* ---------------------  Simple macros here   ---------------------- */
#define GASNETC_KBYTE  1024L
#define GASNETC_MBYTE  1048576L
#define GASNETC_GBYTE  1073741824L

/* ------------------------------------------------------------------ */
/* ---------------------  external decls here  ---------------------- */
extern gasnet_seginfo_t *gasnetc_seginfo;
extern lapi_handle_t lapi_context;

/* ------------------------------------------------------------------ */
/* -----  lapi conduit specific data structures defifnitions here --- */
typedef enum {
    GASNETC_AM_SHORT,
    GASNETC_AM_MEDIUM,
    GASNETC_AM_LONG,
    GASNETC_AM_LONGASYNC
} gasnetc_am_msg_t;

/* AM token structure.  Passed to LAPI handlers to
 * implement AM calls.
 */
typedef struct {
    gasnetc_node_t      src_node;
    gasnetc_node_t      dest_node;
    uint_t              num_args;
    gasnet_handlerarg_t args[16];
    gasnet_handler_t    AM_handler;
    gasnetc_am_msg_t    msg_type;
    void*               data_loc;
    size_t              data_size;
} gasnet_token_rec;


/* ------------------------------------------------------------------ */
/* -----------  lapi conduit specific functions here  --------------- */
extern int gasnetc_get_map(uintptr_t mmap_size, void **loc);
extern uintprt_t gasnetc_compute_maxlocal(uintptr_t want);
extern size_t gasnetc_adjust_limits(void);
extern void gasnetc_lapi_err_handler(lapi_handle_t *context, int *error_code,
				     lapi_err_t  *error_type, int *cur_node,
				     int *src_node);

/* ------------------------------------------------------------------ */
/* ------------  big ugly macros and other stuff here  -------------- */

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ---------------------------------------------------------------- */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {                               \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasneti_ErrorName(retcode), retcode);                \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ----------------------------------------------------------------- */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-99 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */


#define L_CHECK(func)           \
if ((rc = func) != LAPI_SUCCESS) {   \
   LAPI_Msg_string(rc,err_msg_str);       \
   fprintf(stderr,"LAPI Error in %s, [%s] rc = %d\n",#func,err_msg_str,rc); \
}



/* -------------------------------------------------------------------
 * Dan's ugly macros to run the "argument number specific" AM handler.
 */

#define RUN_HANDLER_SHORT(phandlerfn, token, pArgs, numargs) do {                       \
  assert(phandlerfn);                                                                   \
  if (numargs == 0) (*(AMMPI_HandlerShort)phandlerfn)((void *)token);                   \
  else {                                                                                \
    uint32_t *args = (uint32_t *)(pArgs); /* eval only once */                          \
    switch (numargs) {                                                                  \
      case 1:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0]); break;         \
      case 2:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1]); break;\
      case 3:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2]); break; \
      case 4:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3]); break; \
      case 5:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4]); break; \
      case 6:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
      case 7:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
      case 8:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
      case 9:  (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
      case 10: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
      case 11: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
      case 12: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
      case 13: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
      case 14: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
      case 15: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
      case 16: (*(AMMPI_HandlerShort)phandlerfn)((void *)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
      default: abort();                                                                 \
      }                                                                                 \
    }                                                                                   \
  } while (0)
/* ------------------------------------------------------------------------------------ */
#define _RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs, pData, datalen) do {   \
  assert(phandlerfn);                                                         \
  if (numargs == 0) (*phandlerfn)(token, pData, datalen);                     \
  else {                                                                      \
    uint32_t *args = (uint32_t *)(pArgs); /* eval only once */                \
    switch (numargs) {                                                        \
      case 1:  (*phandlerfn)(token, pData, datalen, args[0]); break;           \
      case 2:  (*phandlerfn)(token, pData, datalen, args[0], args[1]); break;  \
      case 3:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2]); break; \
      case 4:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3]); break; \
      case 5:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4]); break; \
      case 6:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
      case 7:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
      case 8:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
      case 9:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
      case 10: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
      case 11: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
      case 12: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
      case 13: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
      case 14: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
      case 15: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
      case 16: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
      default: abort();                                                                 \
      }                                                                                 \
    }                                                                                   \
  } while (0)

#define RUN_HANDLER_MEDIUM(phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    assert(((uintptr_t)pData) % 8 == 0);  /* we guarantee double-word alignment for data payload of medium xfers */ \
    _RUN_HANDLER_MEDLONG((AMMPI_HandlerMedium)phandlerfn, (void *)token, pArgs, numargs, (void *)pData, (int)datalen); \
    } while(0)

#define RUN_HANDLER_LONG(phandlerfn, token, pArgs, numargs, pData, datalen)             \
  _RUN_HANDLER_MEDLONG((AMMPI_HandlerLong)phandlerfn, (void *)token, pArgs, numargs, (void *)pData, (int)datalen)

#endif
