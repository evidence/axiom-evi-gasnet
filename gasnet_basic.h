/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_basic.h,v $
 *     $Date: 2006/03/28 05:54:24 $
 * $Revision: 1.55 $
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
/* must precede everything else to ensure correct operation */
#include "portable_inttypes.h"

/* include files that may conflict with macros defined later */
#ifdef HAVE_SYS_PARAM_H
  #include <sys/param.h>
#endif

#if SIZEOF_VOID_P == 4
  #define GASNETI_PTR32
  #define GASNETI_PTR_CONFIG 32bit
#elif SIZEOF_VOID_P == 8
  #define GASNETI_PTR64
  #define GASNETI_PTR_CONFIG 64bit
#else
  #error GASNet currently only supports 32-bit and 64-bit platforms
#endif

/* miscellaneous macro helpers */
#ifdef __cplusplus
  #define GASNETI_BEGIN_EXTERNC extern "C" {
  #define GASNETI_EXTERNC       extern "C" 
  #define GASNETI_END_EXTERNC   }
#else
  #define GASNETI_BEGIN_EXTERNC 
  #define GASNETI_EXTERNC       
  #define GASNETI_END_EXTERNC 
#endif

#if defined(__cplusplus)
  /* bug 1206: the restrict keyword is not part of the C++ spec, and many C++
     compilers lack it -- so define it away to nothing, which should always be safe */
  #undef GASNETI_RESTRICT
  #define GASNETI_RESTRICT
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

#ifdef __MTA__
   #include <machine/runtime.h>
   #define _gasneti_sched_yield() mta_yield()
#elif defined(HAVE_SCHED_YIELD) && !defined(__blrts__) && !defined(__LIBCATAMOUNT__)
   #include <sched.h>
   #define _gasneti_sched_yield() sched_yield()
#else
   #include <unistd.h>
   #define _gasneti_sched_yield() (sleep(0),0)
#endif
#define gasneti_sched_yield() _gasneti_sched_yield()

#include <stddef.h> /* get standard types, esp size_t */

/* splitting and reassembling 64-bit quantities */
#define GASNETI_MAKEWORD(hi,lo) ((((uint64_t)(hi)) << 32) | (((uint64_t)(lo)) & 0xFFFFFFFF))
#define GASNETI_HIWORD(arg)     ((uint32_t)(((uint64_t)(arg)) >> 32))
#define GASNETI_LOWORD(arg)     ((uint32_t)((uint64_t)(arg)))

/* alignment macros */
#define GASNETI_POWEROFTWO(P)    (((P)&((P)-1)) == 0)

#define GASNETI_ALIGNDOWN(p,P)    (gasneti_assert(GASNETI_POWEROFTWO(P)), \
                                   ((uintptr_t)(p))&~((uintptr_t)((P)-1)))
#define GASNETI_ALIGNUP(p,P)     (GASNETI_ALIGNDOWN((uintptr_t)(p)+((uintptr_t)((P)-1)),P))

#define GASNETI_PAGE_ALIGNDOWN(p) (GASNETI_ALIGNDOWN(p,GASNET_PAGESIZE))
#define GASNETI_PAGE_ALIGNUP(p)   (GASNETI_ALIGNUP(p,GASNET_PAGESIZE))

/* special GCC features */
#if ! defined(GASNETI_HAVE_GCC_ATTRIBUTE) && \
    ! defined (__GNUC__) && ! defined (__attribute__)
  #define __attribute__(flags)
#endif

#define GASNETI_PRAGMA(x) _Pragma ( #x )

#if GASNETI_HAVE_GCC_ATTRIBUTE_WARNUNUSEDRESULT /* Warn if return value is ignored */
  #define GASNETI_WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
#else
  #define GASNETI_WARN_UNUSED_RESULT
#endif

#if GASNETI_HAVE_GCC_ATTRIBUTE_MALLOC 
  /* assert return value is unaliased, and should not be ignored */
  #define GASNETI_MALLOC __attribute__((__malloc__)) GASNETI_WARN_UNUSED_RESULT
#else
  #define GASNETI_MALLOC GASNETI_WARN_UNUSED_RESULT
#endif
/* pragma version of GASNETI_MALLOC */
#ifdef __SUNPRO_C
  #define GASNETI_MALLOCP(fnname) GASNETI_PRAGMA(returns_new_memory(fnname))
#else
  #define GASNETI_MALLOCP(fnname)
#endif

#if GASNETI_HAVE_GCC_ATTRIBUTE_NORETURN
  #define GASNETI_NORETURN __attribute__((__noreturn__))
#else
  #define GASNETI_NORETURN 
#endif
/* pragma version of GASNETI_NORETURN */
#ifdef __SUNPRO_C
  #define GASNETI_NORETURNP(fnname) GASNETI_PRAGMA(does_not_return(fnname))
#elif defined(__xlC__) && 0
  /* this *should* work but it causes bizarre compile failures, so disable it */
  #define GASNETI_NORETURNP(fnname) GASNETI_PRAGMA(leaves(fnname))
#else
  #define GASNETI_NORETURNP(fnname)
#endif

#if GASNETI_HAVE_GCC_ATTRIBUTE_PURE
  /* pure function: one with no effects except the return value, and 
   * return value depends only on the parameters and/or global variables.
   * prohibited from performing volatile accesses, compiler fences, I/O,
   * changing any global variables (including statically scoped ones), or
   * calling any functions that do so
   */
  #define GASNETI_PURE __attribute__((__pure__))
#else
  #define GASNETI_PURE 
#endif
/* pragma version of GASNETI_PURE */
#if defined(__xlC__)
  #define GASNETI_PUREP(fnname) GASNETI_PRAGMA(isolated_call(fnname))
#else
  #define GASNETI_PUREP(fnname) 
#endif

#if GASNETI_HAVE_GCC_ATTRIBUTE_CONST
  /* const function: a more restricted form of pure function, with all the
   * same restrictions, except additionally the return value must NOT
   * depend on global variables or anything pointed to by the arguments
   */
  #define GASNETI_CONST __attribute__((__const__))
#else
  #define GASNETI_CONST GASNETI_PURE
#endif
/* pragma version of GASNETI_CONST */
#ifdef __SUNPRO_C
  #define GASNETI_CONSTP(fnname) GASNETI_PRAGMA(no_side_effect(fnname))
#else
  #define GASNETI_CONSTP(fnname) GASNETI_PUREP(fnname)
#endif

#if GASNETI_HAVE_GCC_ATTRIBUTE_ALWAYSINLINE && !GASNET_DEBUG
  /* bug1525: gcc's __always_inline__ attribute appears to be maximally aggressive */
  #define GASNETI_ALWAYS_INLINE __attribute__((__always_inline__))
#else
  #define GASNETI_ALWAYS_INLINE
#endif

#if defined(__cplusplus)
  #define GASNETI_INLINE(fnname) inline GASNETI_ALWAYS_INLINE
#elif defined(STATIC_INLINE_WORKS)
  #define GASNETI_INLINE(fnname) static CC_INLINE_MODIFIER GASNETI_ALWAYS_INLINE
#elif defined(CC_INLINE_MODIFIER)
  #define GASNETI_INLINE(fnname) CC_INLINE_MODIFIER GASNETI_ALWAYS_INLINE
#elif defined(_CRAYC)
  /* CrayC has a really #&#$&! stupidly designed #pragma for inlining functions 
     that requires providing the function name 
     (the only way to request inlining a particular fn from C) */
  #define GASNETI_INLINE(fnname) GASNETI_PRAGMA(_CRI inline fnname) static 
#elif defined(__MTA__)
  #define GASNETI_INLINE(fnname) GASNETI_PRAGMA(mta inline) static
#else
  #define GASNETI_INLINE(fnname) static GASNETI_ALWAYS_INLINE
#endif

/* ------------------------------------------------------------------------------------ */
/* GASNETI_IDENT() takes a unique identifier and a textual string and embeds the textual
   string in the executable file
 */
#define _GASNETI_IDENT(identName, identText)                         \
  extern char volatile identName[];                                  \
  char volatile identName[] = identText;                             \
  extern char *_##identName##_identfn() { return (char*)identName; } \
  static int _dummy_##identName = sizeof(_dummy_##identName)
#if defined(_CRAYC)
  #define GASNETI_IDENT(identName, identText) \
    GASNETI_PRAGMA(_CRI ident identText);     \
    _GASNETI_IDENT(identName, identText)
#elif defined(__HP_cc) && defined(__ia64) /* bug 1490 */
  #define GASNETI_IDENT(identName, identText) \
    GASNETI_PRAGMA(VERSIONID identText);      \
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
  #if defined(__GNUC__) && defined(HAVE_BUILTIN_EXPECT)
    /* cast to uintptr_t avoids warnings on some compilers about passing 
       non-integer arguments to __builtin_expect(), and we don't use (int)
       because on some systems this is smaller than (void*) and causes 
       other warnings
     */
   #define PREDICT_TRUE(exp)  __builtin_expect( ((uintptr_t)(exp)), 1 )
   #define PREDICT_FALSE(exp) __builtin_expect( ((uintptr_t)(exp)), 0 )
  #else
   #define PREDICT_TRUE(exp)  (exp)
   #define PREDICT_FALSE(exp) (exp)
  #endif
#endif

/* if with branch prediction */
#ifndef if_pf
#ifdef __MTA__
  /* MTA's pragma mechanism is buggy, so allow it to be selectively disabled */
  #define GASNETT_MTA_PRAGMA_EXPECT_ENABLED(x) _Pragma(x)
  #define GASNETT_MTA_PRAGMA_EXPECT_DISABLED(x) 
  #define GASNETT_MTA_PRAGMA_EXPECT_OVERRIDE GASNETT_MTA_PRAGMA_EXPECT_ENABLED
  #define if_pf(cond) GASNETT_MTA_PRAGMA_EXPECT_OVERRIDE("mta expect false") if (cond)
  #define if_pt(cond) GASNETT_MTA_PRAGMA_EXPECT_OVERRIDE("mta expect true")  if (cond)
#else
  #define if_pf(cond) if (PREDICT_FALSE(cond))
  #define if_pt(cond) if (PREDICT_TRUE(cond))
#endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Non-binding prefetch hints:
   These macros take a single address expression and provide a hint to prefetch the
   corresponding memory to L1 cache for either reading or for writing.
   These are non-binding hints and so the argument need not always be a valid pointer.
   For instance, GASNETI_PREFETCH_{READ,WRITE}_HINT(NULL) is explicitly permitted.
   The macros may expand to nothing, so the argument must not have side effects.
 */
#if HAVE_BUILTIN_PREFETCH
  #define GASNETI_PREFETCH_READ_HINT(P) __builtin_prefetch((P),0)
  #define GASNETI_PREFETCH_WRITE_HINT(P) __builtin_prefetch((P),1)
#else
  #define GASNETI_PREFETCH_READ_HINT(P)
  #define GASNETI_PREFETCH_WRITE_HINT(P)
#endif


#endif
