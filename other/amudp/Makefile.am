## Process this file with automake to produce Makefile.in	-*- makefile -*-
# NOTE: This Makefile is used to tie in with the GASNet build infrastructure
#       the stand-alone AMMPI product doesn't currently provide an automake framework

AUTOMAKE_OPTIONS = foreign 1.4 no-dependencies

if PLATFORM_ILP32
  apputils_flags =
else
  apputils_flags = -DAMUDP_OMIT_READWRITE
endif

CC  = @CC@
CCFLAGS = $(apputils_flags)
CXX = @CXX@
CXXFLAGS = @CXXFLAGS@

endianness = @ENDIANNESS@
platform_defines = @GASNET_MACHINE_DEFINES@ @BUILDCONFIG_DEFINES@ -DAMUDP_DISABLE_AMTAGS -DAMUDP_ENV_PREFIX=GASNET
platform_libraries = @LIBS@ @GASNET_UDP_LIBS@

INCLUDES = -I$(srcdir) ${platform_defines} $(endianness)
compile = ${CC} ${CCFLAGS} ${INCLUDES}
link = $(CXX) ${CXXFLAGS} ${INCLUDES} 
linkend = ${platform_libraries}

if USE_UDP_CONDUIT
lib_LIBRARIES = libamudp.a
endif
libamudp_a_SOURCES = \
	amudp.h			\
	amudp_ep.cpp		\
	amudp_internal.h	\
	amudp_reqrep.cpp	\
	amudp_spawn.cpp		\
	amudp_spmd.cpp		\
	amudp_spmd.h		\
	apputils.h		\
	exc.cpp			\
	exc.h			\
	sig.cpp			\
	sig.h			\
	sockaddr.h		\
	socket.h		\
	socklist.cpp		\
	socklist.h		\
	sockutil.cpp		\
	sockutil.h

EXTRA_DIST =        \
    apputils.c      \
    apputils.h      \
    testamudp.c     \
    testbounce.c    \
    testbulk.c      \
    testgetput.c    \
    testlatency.c   \
    testping.c      \
    testreadwrite.c \
    testreduce.c    

# ---------- test programs -------------
# all the test executables
if PLATFORM_ILP32
  testprograms_ILP32only = 	\
    testgetput    		\
    testreadwrite 
else
  testprograms_ILP32only = 
endif

testprograms =    		\
    testamudp     		\
    testbounce    		\
    testbulk      		\
    testlatency   		\
    testping      		\
    testreduce			\
    $(testprograms_ILP32only)

tests: ${testprograms}

test%: test%.o apputils.o libamudp.a 
	${link} -o $@ $< apputils.o -L. -lamudp ${linkend}

%.o: %.c apputils.h amudp.h amudp_spmd.h
	${compile} -o $@ -c $<

if USE_UDP_CONDUIT
clean-local:
	rm -f libamudp.a $(testprograms) *.o core
endif

force:

.PHONY: force clean-local tests

