/*   $Archive:: /Ti/GASNet/acconfig.h                                      $ */
/*      $Date: 2002/09/07 07:33:40 $ */
/*  $Revision: 1.7 $ */
/*  Description: GASNet acconfig.h (or config.h)                             */
/*  Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>                  */

#ifndef _INCLUDE_GASNET_CONFIG_H_
#define _INCLUDE_GASNET_CONFIG_H_

@TOP@

/* using the MIPSPro C compiler (which doesn't seem to have other identifying markers) */
#undef MIPSPRO_COMPILER

/* Defined to be the inline function modifier supported by the C compiler (if supported) */
#undef INLINE_MODIFIER

/* Functions may be declared "static inline" */
#undef STATIC_INLINE_WORKS

/* Variable sized stack arrays  */
#undef VARARRAY_WORKS

/* have mmap() */
#undef HAVE_MMAP

/* has usleep() */
#undef HAVE_USLEEP

/* has nanosleep() */
#undef HAVE_NANOSLEEP

/* has nsleep() */
#undef HAVE_NSLEEP

/* has sched_yield() */
#undef HAVE_SCHED_YIELD

/* has __builtin_expect */
#undef HAVE_BUILTIN_EXPECT

/* has __func__ function name defined */
#undef HAVE_FUNC

/* various OS and machine definitions */
#undef UNIX
#undef LINUX
#undef FREEBSD
#undef SOLARIS
#undef UNICOS
#undef CRAYT3E
#undef AIX
#undef OSF
#undef IRIX
#undef CYGWIN

/* GASNet segment definition */
#undef GASNET_SEGMENT_FAST
#undef GASNET_SEGMENT_LARGE
#undef GASNET_SEGMENT_EVERYTHING

@BOTTOM@

/* special GCC features */
#if ! defined (__GNUC__) && ! defined (__attribute__)
#define __attribute__(flags)
#endif

#if defined(__GNUC__)
#define GASNET_NORETURN __attribute__((__noreturn__))
#else
#define GASNET_NORETURN 
#endif



#endif
