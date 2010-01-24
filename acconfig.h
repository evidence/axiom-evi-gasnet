/*    $Source: /Users/kamil/work/gasnet-cvs2/gasnet/acconfig.h,v $ */
/*      $Date: 2010/01/24 03:21:22 $ */
/*  $Revision: 1.147 $ */
/*  Description: GASNet acconfig.h (or config.h)                             */
/*  Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>                  */
/* Terms of use are as specified in license.txt */

#ifndef _INCLUDE_GASNET_CONFIG_H_
#define _INCLUDE_GASNET_CONFIG_H_
#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

@TOP@

#undef GASNETI_BUILD_ID
#undef GASNETI_CONFIGURE_ARGS
#undef GASNETI_SYSTEM_TUPLE
#undef GASNETI_SYSTEM_NAME
#undef GASNETI_CROSS_COMPILING

/* version identifiers */
#undef GASNET_RELEASE_VERSION_MAJOR
#undef GASNET_RELEASE_VERSION_MINOR
#undef GASNET_RELEASE_VERSION_PATCH
#undef GASNETI_RELEASE_VERSION
#undef GASNETI_SPEC_VERSION_MAJOR
#undef GASNETI_SPEC_VERSION_MINOR
#undef GASNETIT_SPEC_VERSION_MAJOR
#undef GASNETIT_SPEC_VERSION_MINOR

/* configure-detected conduits */
#undef GASNETI_CONDUITS

/* CC attributes support */
#undef GASNETI_HAVE_CC_ATTRIBUTE
#undef GASNETI_HAVE_CC_ATTRIBUTE_ALWAYSINLINE
#undef GASNETI_HAVE_CC_ATTRIBUTE_NOINLINE
#undef GASNETI_HAVE_CC_ATTRIBUTE_MALLOC
#undef GASNETI_HAVE_CC_ATTRIBUTE_WARNUNUSEDRESULT
#undef GASNETI_HAVE_CC_ATTRIBUTE_USED
#undef GASNETI_HAVE_CC_ATTRIBUTE_UNUSED
#undef GASNETI_HAVE_CC_ATTRIBUTE_MAYALIAS
#undef GASNETI_HAVE_CC_ATTRIBUTE_NORETURN
#undef GASNETI_HAVE_CC_ATTRIBUTE_PURE
#undef GASNETI_HAVE_CC_ATTRIBUTE_CONST
#undef GASNETI_HAVE_CC_ATTRIBUTE_FORMAT
#undef GASNETI_HAVE_CC_ATTRIBUTE_FORMAT_FUNCPTR

/* CXX attributes support */
#undef GASNETI_HAVE_CXX_ATTRIBUTE
#undef GASNETI_HAVE_CXX_ATTRIBUTE_ALWAYSINLINE
#undef GASNETI_HAVE_CXX_ATTRIBUTE_NOINLINE
#undef GASNETI_HAVE_CXX_ATTRIBUTE_MALLOC
#undef GASNETI_HAVE_CXX_ATTRIBUTE_WARNUNUSEDRESULT
#undef GASNETI_HAVE_CXX_ATTRIBUTE_USED
#undef GASNETI_HAVE_CXX_ATTRIBUTE_UNUSED
#undef GASNETI_HAVE_CXX_ATTRIBUTE_MAYALIAS
#undef GASNETI_HAVE_CXX_ATTRIBUTE_NORETURN
#undef GASNETI_HAVE_CXX_ATTRIBUTE_PURE
#undef GASNETI_HAVE_CXX_ATTRIBUTE_CONST
#undef GASNETI_HAVE_CXX_ATTRIBUTE_FORMAT
#undef GASNETI_HAVE_CXX_ATTRIBUTE_FORMAT_FUNCPTR

/* MPI_CC attributes support */
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_ALWAYSINLINE
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_NOINLINE
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_MALLOC
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_WARNUNUSEDRESULT
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_USED
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_UNUSED
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_MAYALIAS
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_NORETURN
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_PURE
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_CONST
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_FORMAT
#undef GASNETI_HAVE_MPI_CC_ATTRIBUTE_FORMAT_FUNCPTR

/* identification of the C compiler used at configure time */
#undef GASNETI_PLATFORM_COMPILER_IDSTR
#undef GASNETI_PLATFORM_COMPILER_FAMILYID
#undef GASNETI_PLATFORM_COMPILER_ID
#undef GASNETI_PLATFORM_COMPILER_VERSION

/* identification of the C++ compiler used at configure time */
#undef GASNETI_PLATFORM_CXX_IDSTR
#undef GASNETI_PLATFORM_CXX_FAMILYID
#undef GASNETI_PLATFORM_CXX_ID
#undef GASNETI_PLATFORM_CXX_VERSION

/* identification of the MPI C compiler used at configure time */
#undef GASNETI_PLATFORM_MPI_CC_IDSTR
#undef GASNETI_PLATFORM_MPI_CC_FAMILYID
#undef GASNETI_PLATFORM_MPI_CC_ID
#undef GASNETI_PLATFORM_MPI_CC_VERSION

/* Defined to be the inline function modifier supported by the C compiler (if supported) */
#undef CC_INLINE_MODIFIER

/* C, C++ and MPI_CC compilers 'restrict' keywords (or empty) */
#undef GASNETI_CC_RESTRICT
#undef GASNETI_CXX_RESTRICT
#undef GASNETI_MPI_CC_RESTRICT

/* true iff GASNETI_RESTRICT may be applied to types which are not pointer types until after typedef expansion */
#undef GASNETI_CC_RESTRICT_MAY_QUALIFY_TYPEDEFS
#undef GASNETI_CXX_RESTRICT_MAY_QUALIFY_TYPEDEFS
#undef GASNETI_MPI_CC_RESTRICT_MAY_QUALIFY_TYPEDEFS

/* Functions may be declared "static inline" */
#undef STATIC_INLINE_WORKS

/* have mmap() */
#undef HAVE_MMAP

/* mmap supporting flags */
#undef HAVE_MAP_NORESERVE
#undef HAVE_MAP_ANON
#undef HAVE_MAP_ANONYMOUS

/* --with-segment-mmap-max value (if given) */
#undef GASNETI_MMAP_MAX_SIZE

/* --with-max-threads value (if given) */
#undef GASNETI_MAX_THREADS_CONFIGURE

/* has usleep() */
#undef HAVE_USLEEP

/* has nanosleep() */
#undef HAVE_NANOSLEEP

/* has nsleep() */
#undef HAVE_NSLEEP

/* has sched_yield() */
#undef HAVE_SCHED_YIELD

/* have sysctl machdep.tsc_freq */
#undef GASNETI_HAVE_SYSCTL_MACHDEP_TSC_FREQ

/* has Portable Linux Processor Affinity */
#undef HAVE_PLPA

/* have ptmalloc's mallopt() options */
#undef HAVE_PTMALLOC

/* have declarations/definitions */
#undef HAVE_SETENV_DECL
#undef HAVE_UNSETENV_DECL
#undef HAVE_SNPRINTF_DECL
#undef HAVE_VSNPRINTF_DECL
#undef HAVE_ISBLANK_DECL
#undef HAVE_ISASCII_DECL
#undef HAVE_TOASCII_DECL

/* Forbidden to use fork(), popen() and system()? */
#undef GASNETI_NO_FORK

/* building Process SHared Memory support? */
#undef GASNET_PSHM
#undef HAVE_SHM_OPEN

/* support for backtracing */
#undef HAVE_EXECINFO_H
#undef HAVE_BACKTRACE
#undef HAVE_BACKTRACE_SYMBOLS
#undef HAVE_PRINTSTACK
#undef ADDR2LINE_PATH
#undef GDB_PATH
#undef PGDBG_PATH
#undef IDB_PATH
#undef LADEBUG_PATH
#undef DBX_PATH

/* have pthread_setconcurrency */
#undef HAVE_PTHREAD_SETCONCURRENCY

/* has pthread_kill_other_threads_np() */
#undef HAVE_PTHREAD_KILL_OTHER_THREADS_NP

/* has __thread thread-local-storage support */
#undef GASNETI_HAVE_TLS_SUPPORT

/* pause instruction, if any */
#undef GASNETI_PAUSE_INSTRUCTION

/* How to name MIPS assembler temporary register in inline asm, if at all */
#undef GASNETI_HAVE_MIPS_REG_1
#undef GASNETI_HAVE_MIPS_REG_AT

/* has ARM kernel-level support for cmpxchg */
#undef GASNETI_HAVE_ARM_CMPXCHG

/* has ARM kernel-level support for membar */
#undef GASNETI_HAVE_ARM_MEMBAR

/* has x86 EBX register (not reserved for GOT) */
#undef GASNETI_HAVE_X86_EBX

/* has support (toolchain and cpu) for ia64 cmp8xchg16 instruction */
#undef GASNETI_HAVE_IA64_CMP8XCHG16

/* has support (toolchain and cpu) for x86_64 cmpxchg16b instruction */
#undef GASNETI_HAVE_X86_CMPXCHG16B

/* gcc/gas support "LDCW,CO" psuedo-op on PA-RISC */
#undef GASNETI_HAVE_PARISC_LDCW_CO

/* has __builtin_expect */
#undef HAVE_BUILTIN_EXPECT

/* has __builtin_prefetch */
#undef HAVE_BUILTIN_PREFETCH

/* has __func__ function name defined */
#undef HAVE_FUNC

/* portable inttypes support */
#undef HAVE_INTTYPES_H
#undef HAVE_STDINT_H
#undef HAVE_SYS_TYPES_H
#undef COMPLETE_INTTYPES_H
#undef COMPLETE_STDINT_H
#undef COMPLETE_SYS_TYPES_H

/* Linux PR_SET_PDEATHSIG support */
#undef HAVE_PR_SET_PDEATHSIG

/* forcing use of "non-native" implementations: */
#undef GASNETI_FORCE_GENERIC_ATOMICOPS
#undef GASNETI_FORCE_OS_ATOMICOPS
#undef GASNETI_FORCE_TRUE_WEAKATOMICS
#undef GASNETI_FORCE_GENERIC_SEMAPHORES
#undef GASNETI_FORCE_YIELD_MEMBARS
#undef GASNETI_FORCE_SLOW_MEMBARS
#undef GASNETI_FORCE_GETTIMEOFDAY
#undef GASNETI_FORCE_POSIX_REALTIME

/* forcing UP build, even if build platform is a multi-processor */
#undef GASNETI_UNI_BUILD

/* force memory barriers on GASNet local (loopback) puts and gets */
#undef GASNETI_MEMSYNC_ON_LOOPBACK

/* throttle polling threads in multi-threaded configurations to reduce contention */
#undef GASNETI_THROTTLE_FEATURE_ENABLED

/* auto-detected mmap data page size */
#undef GASNETI_PAGESIZE
#undef GASNETI_PAGESHIFT

/* auto-detected shared data cache line size */
#undef GASNETI_CACHE_LINE_BYTES
#undef GASNETI_CACHE_LINE_SHIFT

/* udp-conduit default custom spawn command */
#undef GASNET_CSPAWN_CMD

/* platform is an SGI Altix multiprocessor */
#undef GASNETI_ARCH_ALTIX

/* platform is an IBM BlueGene/P multiprocessor */
#undef GASNETI_ARCH_BGP

/* platform is a SiCortex multiprocessor */
#undef GASNETI_ARCH_SICORTEX

/* have (potentially buggy) SiCortex ICE9A CPU */
#undef GASNETI_ARCH_ICE9A

/* have (potentially buggy) MIPS R10000 multiprocessor */
#undef GASNETI_ARCH_SGI_IP27

/* have working UltraSPARC ISA (lacks an associated builtin preprocessor macro) */
#undef GASNETI_ARCH_ULTRASPARC

/* Have working PPC64 ISA (lacks an associated builtin preprocessor macro) */
#undef GASNETI_ARCH_PPC64
/* Tune for a PPC970 cpu (should not crash other PPCs) */
#undef GASNETI_TUNE_PPC970

/* Type to use as socklen_t */
#undef GASNET_SOCKLEN_T

/* GASNet build configuration */
#undef GASNET_DEBUG
#undef GASNET_NDEBUG
#undef GASNET_TRACE
#undef GASNET_STATS
#undef GASNET_SRCLINES
#undef GASNET_DEBUG_VERBOSE
#undef GASNET_USE_STRICT_PROTOTYPES

/* GASNet segment definition */
#undef GASNET_SEGMENT_FAST
#undef GASNET_SEGMENT_LARGE
#undef GASNET_SEGMENT_EVERYTHING

/* Override to disable default segment alignment */
#undef GASNETI_DISABLE_ALIGNED_SEGMENTS

/* GASNet ref-extended settings */
#undef GASNETE_USE_AMDISSEMINATION_REFBARRIER

/* GASNet gm-conduit broken 2.x versions */
#undef GASNETC_GM_ENABLE_BROKEN_VERSIONS
#undef GASNETC_GM_MPI_COMPAT

/* GASNet vapi-conduit features and bug work-arounds */
#undef HAVE_VAPI_FMR
#undef GASNETC_VAPI_POLL_LOCK
#undef GASNETC_VAPI_RCV_THREAD
#undef GASNETC_VAPI_MAX_HCAS

/* GASNet ibv-conduit features and bug work-arounds */
#undef GASNETC_IBV_POLL_LOCK
#undef GASNETC_IBV_RCV_THREAD
#undef GASNETC_IBV_MAX_HCAS

/* GASNet lapi-conduit specific */
#undef GASNETC_LAPI_FEDERATION
#undef GASNETC_LAPI_COLONY
#undef GASNETC_LAPI_VERSION_A
#undef GASNETC_LAPI_VERSION_B
#undef GASNETC_LAPI_VERSION_C
#undef GASNETC_LAPI_VERSION_D
#undef GASNETC_LAPI_RDMA

/* GASNet elan-conduit specific */
#undef GASNETC_ELAN3
#undef GASNETC_ELAN4
#undef ELAN_VERSION_MAJOR
#undef ELAN_VERSION_MINOR
#undef ELAN_VERSION_SUB
#undef ELAN_DRIVER_VERSION
#undef ELAN4_KERNEL_PATCH
#undef HAVE_RMS_RMSAPI_H
#undef HAVE_RMS_KILLRESOURCE
#undef RMS_RCONTROL_PATH
#undef SLURM_SCANCEL_PATH
#undef HAVE_SLURM_SLURM_H
#undef HAVE_SLURM_KILL_JOB
#undef HAVE_ELAN_QUEUEMAXSLOTSIZE
#undef HAVE_ELAN_DONE
#undef HAVE_ELAN_QUEUETXINIT

/* GASNet portals-conduit specific */
#undef HAVE_PCTMBOX_H
#undef HAVE_PMI_CNOS
#undef GASNETC_PORTALS_MISSING_EQ_HANDLER_T

/* GASNet bug1389 detection/work-around */
#undef GASNETI_BUG1389_WORKAROUND

@BOTTOM@

/* these get us 64-bit file declarations under several Unixen */
/* they must come before the first include of features.h (included by many system headers) */
/* define them even on platforms lacking features.h */
#define _LARGEFILE64_SOURCE 1
#define _LARGEFILE_SOURCE 1
#ifdef HAVE_FEATURES_H
# include <features.h>
#endif

#endif
