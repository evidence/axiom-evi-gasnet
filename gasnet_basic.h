/*  $Archive:: /Ti/GASNet/gasnet_basic.h                                  $
 *     $Date: 2002/12/26 03:43:15 $
 * $Revision: 1.12 $
 * Description: GASNet basic header utils
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_BASIC_H
#define _GASNET_BASIC_H

/* ------------------------------------------------------------------------------------ */
/* include files that may conflict with macros defined later */
#if defined(IRIX)
  #include <sys/param.h>
#endif

/* ------------------------------------------------------------------------------------ */
/* define unambiguous integer types */
#ifndef _INTTYPES_DEFINED
#define _INTTYPES_DEFINED
#if defined(HAVE_INTTYPES_H)
  #include <inttypes.h>
#elif defined(WIN32) && defined(_MSC_VER)
  typedef __int8             int8_t;
  typedef unsigned __int8   uint8_t;
  typedef __int16           int16_t;
  typedef unsigned __int16 uint16_t;
  typedef __int32           int32_t;
  typedef unsigned __int32 uint32_t;
  typedef __int64           int64_t;
  typedef unsigned __int64 uint64_t;

  typedef unsigned int    uintptr_t; /* unsigned type big enough to hold any pointer offset */
#elif defined(CRAYT3E)
  typedef char               int8_t;
  typedef unsigned char     uint8_t;
  typedef short             int16_t; /* This is 32-bits, should be 16 !!! */
  typedef unsigned short   uint16_t; /* This is 32-bits, should be 16 !!! */
  typedef short             int32_t;
  typedef unsigned short   uint32_t;
  typedef int               int64_t;
  typedef unsigned int     uint64_t;

  typedef unsigned int    uintptr_t; /* unsigned type big enough to hold any pointer offset */
#elif defined(CYGWIN)
  #include <sys/types.h>
  typedef u_int8_t     uint8_t;
  typedef u_int16_t   uint16_t; 
  typedef u_int32_t   uint32_t;
  typedef u_int64_t   uint64_t;

  typedef unsigned int    uintptr_t; /* unsigned type big enough to hold any pointer offset */
#else
  /* try to determine them automatically */
  #if SIZEOF_CHAR == 8
    typedef          char  int8_t;
    typedef unsigned char uint8_t;
  #else
    #error Cannot find an 8-bit type for your platform
  #endif

  #if SIZEOF_CHAR == 16
    typedef          char  int16_t;
    typedef unsigned char uint16_t;
  #elif SIZEOF_SHORT == 16
    typedef          short  int16_t;
    typedef unsigned short uint16_t;
  #elif SIZEOF_INT == 16
    typedef          int  int16_t;
    typedef unsigned int uint16_t;
  #else
    #error Cannot find a 16-bit type for your platform
  #endif

  #if SIZEOF_SHORT == 32
    typedef          short  int32_t;
    typedef unsigned short uint32_t;
  #elif SIZEOF_INT == 32
    typedef          int  int32_t;
    typedef unsigned int uint32_t;
  #elif SIZEOF_LONG == 32
    typedef          long  int32_t;
    typedef unsigned long uint32_t;
  #else
    #error Cannot find a 32-bit type for your platform
  #endif

  #if SIZEOF_INT == 64
    typedef          int  int64_t;
    typedef unsigned int uint64_t;
  #elif SIZEOF_LONG == 64
    typedef          long  int64_t;
    typedef unsigned long uint64_t;
  #elif SIZEOF_LONG_LONG == 64
    typedef          long long  int64_t;
    typedef unsigned long long uint64_t;
  #else
    #error Cannot find a 64-bit type for your platform
  #endif
#endif
#endif
/* ------------------------------------------------------------------------------------ */

#if SIZEOF_VOID_P == 4
  #define GASNETI_PTR32
#elif SIZEOF_VOID_P == 8
  #define GASNETI_PTR64
#else
  #error GASNet currently only supports 32-bit and 64-bit platforms
#endif

  /* miscellaneous macro helpers */
#ifndef BEGIN_EXTERNC
  #ifdef __cplusplus
    #define BEGIN_EXTERNC extern "C" {
    #define END_EXTERNC }
  #else
    #define BEGIN_EXTERNC 
    #define END_EXTERNC 
  #endif
#endif

#ifndef _STRINGIFY
#define _STRINGIFY_HELPER(x) #x
#define _STRINGIFY(x) _STRINGIFY_HELPER(x)
#endif

#ifndef _CONCAT
#define _CONCAT_HELPER(a,b) a ## b
#define _CONCAT(a,b) _CONCAT_HELPER(a,b)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MIN
#define MIN(x,y)  ((x)<(y)?(x):(y))
#endif
#ifndef MAX
#define MAX(x,y)  ((x)>(y)?(x):(y))
#endif

/* splitting and reassembling 64-bit quantities */
#define GASNETI_MAKEWORD(hi,lo) ((((uint64_t)(hi)) << 32) | (((uint64_t)(lo)) & 0xFFFFFFFF))
#define GASNETI_HIWORD(arg)     ((uint32_t)(((uint64_t)(arg)) >> 32))
#define GASNETI_LOWORD(arg)     ((uint32_t)((uint64_t)(arg)))

#define GASNETI_PRAGMA(x) _Pragma ( #x )

#if defined(STATIC_INLINE_WORKS)
  #define GASNET_INLINE_MODIFIER(fnname) static CC_INLINE_MODIFIER
#elif defined(CC_INLINE_MODIFIER)
  #define GASNET_INLINE_MODIFIER(fnname) CC_INLINE_MODIFIER
#elif defined(_CRAYC)
  /* CrayC has a really #&#$&! stupidly designed #pragma for inlining functions 
     that requires providing the function name 
     (the only way to request inlining a particular fn from C) */
  #define GASNET_INLINE_MODIFIER(fnname) GASNETI_PRAGMA(_CRI inline fnname)
#else
  #define GASNET_INLINE_MODIFIER(fnname) static
#endif

/* ------------------------------------------------------------------------------------ */
/* GASNETI_IDENT() takes a unique identifier and a textual string and embeds the textual
   string in the executable file
 */
#define _GASNETI_IDENT(identName, identText)                   \
  extern char volatile identName[];                            \
  char volatile identName[] = identText;                       \
  extern char *_get_##identName() { return (char*)identName; } \
  static int _dummy_##identName = sizeof(_dummy_##identName)
#if defined(_CRAYC)
  #define GASNETI_IDENT(identName, identText) \
    GASNETI_PRAGMA(_CRI ident identText);     \
    _GASNETI_IDENT(identName, identText)
#elif defined(__xlC__)
    /* #pragma comment(user,"text...") 
         or
       _Pragma ( "comment (user,\"text...\")" );
       are both supposed to work according to compiler docs, but both appear to be broken
     */
  #define GASNETI_IDENT(identName, identText)   \
    _GASNETI_IDENT(identName, identText)
#else
  #define GASNETI_IDENT _GASNETI_IDENT
#endif
/* ------------------------------------------------------------------------------------ */
/* Branch prediction:
   these macros return the value of the expression given, but pass on
   a hint that you expect the value to be true or false.
   Use them to wrap the conditional expression in an if stmt when
   you have strong reason to believe the branch will frequently go
   in one direction and the branch is a bottleneck
 */
#ifndef PREDICT_TRUE
  #if defined(HAVE_BUILTIN_EXPECT)
   #define PREDICT_TRUE(exp)  __builtin_expect( (exp), 1 )
   #define PREDICT_FALSE(exp) __builtin_expect( (exp), 0 )
  #else
   #define PREDICT_TRUE(exp)  (exp)
   #define PREDICT_FALSE(exp) (exp)
  #endif
#endif

/* if with branch prediction */
#ifndef if_pf
  /* cast to uintptr_t avoids warnings on some compilers about passing 
     non-integer arguments to __builtin_expect(), and we don't use (int)
     because on some systems this is smaller than (void*) and causes 
     other warnings
   */
  #define if_pf(cond) if (PREDICT_FALSE((uintptr_t)(cond)))
  #define if_pt(cond) if (PREDICT_TRUE((uintptr_t)(cond)))
#endif

#endif
