## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/ammpi/Makefile.am,v $
#     $Date: 2005/05/04 08:12:52 $
# $Revision: 1.33 $
# Description: Makefile for AMMPI/GASNet 
# Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt
# NOTE: This Makefile is used to tie in with the GASNet build infrastructure
#       the stand-alone AMMPI product doesn't currently provide an automake framework

AUTOMAKE_OPTIONS = foreign 1.4 no-dependencies

if PLATFORM_ILP32
  apputils_flags =
else
  apputils_flags = -DAPPUTILS_OMIT_READWRITE
endif

CC = @MPI_CC@
CFLAGS = @MPI_CFLAGS@ $(apputils_flags)
LDFLAGS = @LDFLAGS@

platform_defines = @GASNET_MACHINE_DEFINES@ @INTTYPES_DEFINES@ -DAMMPI_DISABLE_AMTAGS

platform_libraries = @LIBS@ @MPI_LIBS@

VPATH = $(srcdir)
testdir = $(srcdir)/../amxtests
altincdir = $(srcdir)/..
INCLUDES = -I$(srcdir) -I$(altincdir) $(platform_defines)
compile = $(CC) $(CFLAGS) $(INCLUDES)
link = $(CC) $(CFLAGS) $(LDFLAGS) $(platform_defines) $(INCLUDES) 
linkend = $(platform_libraries)

if USE_MPI_CONDUIT
lib_LIBRARIES = libammpi.a
endif
headers = 	     \
    ammpi.h          \
    ammpi_internal.h \
    ammpi_spmd.h

libammpi_a_SOURCES = \
    $(headers)       \
    ammpi_ep.c       \
    ammpi_reqrep.c   \
    ammpi_spmd.c     

libammpi_a_DEPENDENCIES = $(libammpi_a_SOURCES:%.c=%.o)

EXTRA_DIST = \
	Makefile.common 	\
	Makefile.standalone 	\
	Makefile.titanium.in	\
	license.txt

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
    testam       		\
    testbounce    		\
    testbulk      		\
    testlatency   		\
    testlatencyM   		\
    testping      		\
    testreduce			\
    $(testprograms_ILP32only)

tests: apputils.o $(testprograms)

test%.o : $(testdir)/test%.c $(testdir)/*.h $(headers)
	$(compile) -DAMMPI -I$(testdir) -c $< -o $@

apputils.o : $(testdir)/apputils.c $(testdir)/apputils.h $(headers)
	$(compile) -DAMMPI -I$(testdir) -c $(testdir)/apputils.c -o apputils.o

test%: test%.o apputils.o libammpi.a 
	$(link) -o $@ $< apputils.o -L. -lammpi $(linkend)

TEST_NODES=2
AMMPI_RUNCMD='@MPIRUN_CMD@'
TEST_LINE=++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
AMMPI_RUNCMD_EXP=\
      TEST_RUN_P=`echo "$$TEST_RUN" | sed 's/ .*$$//'`  \
      TEST_RUN_A=`echo "$$TEST_RUN" | sed 's/^[^ ]* //'` \
      TEST_DOIT=`echo $(AMMPI_RUNCMD) | sed 's/%N/$(TEST_NODES)/;s@%C@'"$$TEST_RUN"'@;s@%P@'"$$TEST_RUN_P"'@;s@%A@'"$$TEST_RUN_A"'@;s/%V//'`
TEST_RUNCMD=$(AMMPI_RUNCMD_EXP) ; 				\
         if test -x "$$TEST_RUN_P" ; then                       \
            echo "  **** `basename $$TEST_RUN_P` **** "; 	\
            echo $$TEST_DOIT ; 					\
            $$TEST_DOIT ; 					\
            echo $(TEST_LINE) ;					\
         fi
TEST_ITERS=100
TEST_MODE=P
TEST_DEPTH=32
TEST_DUPLEX=H
AMMPI_NETWORKDEPTH=$(TEST_DEPTH)
run-tests: tests
	@echo $(TEST_LINE)
	@echo  Running AMMPI tests...
	@echo  If this fails to spawn a job, you may need to re-run with a
	@echo   command like: gmake run-tests AMMPI_RUNCMD=\"$(AMMPI_RUNCMD)\"
	@echo $(TEST_LINE)
	@TEST_RUN="./testping $(TEST_ITERS) $(TEST_MODE)" $(TEST_RUNCMD)
	@TEST_RUN="./testlatency $(TEST_ITERS) $(TEST_MODE)" $(TEST_RUNCMD)
	@TEST_RUN="./testlatencyM $(TEST_ITERS) $(TEST_MODE) 64" $(TEST_RUNCMD)
	@TEST_RUN="./testbulk $(TEST_ITERS) 1048576 $(TEST_MODE) $(TEST_DUPLEX)" $(TEST_RUNCMD)
	@TEST_RUN="./testam $(TEST_ITERS) $(TEST_MODE)" $(TEST_RUNCMD)
	@TEST_RUN="./testbounce $(TEST_ITERS) $(TEST_MODE)" $(TEST_RUNCMD)
	@TEST_RUN="./testreduce" $(TEST_RUNCMD)
	@TEST_RUN="./testgetput $(TEST_ITERS)" $(TEST_RUNCMD)
	@TEST_RUN="./testreadwrite $(TEST_ITERS)" $(TEST_RUNCMD)
	@echo TESTS COMPLETE

%.o: %.c ammpi.h ammpi_spmd.h ammpi_internal.h
	$(compile) -DAMMPI -o $@ -c $<

if USE_MPI_CONDUIT
if GNU_NM
check-exports: libammpi.a
	@echo Checking libammpi exports...
	@lib=libammpi.a ; \
            @NM@ --defined-only $$lib |                                             \
            grep -v -e ' [\._]*ammpi_' -e ' [\._]*AMMPI_' -e ' [\._]*AM_'           \
                    -e ' [\._]*gasnett_' -e ' [\._]*gasneti_'                       \
                    -e __FUNCTION__ -e __PRETTY_FUNCTION__ -e debug_info_seg |      \
            @PERL@ -n -e 'print if /^[0-9a-fA-F]+\s+[A-Z]\s+/' > .$$lib.exp;        \
          if test -s .$$lib.exp ; then                                              \
            cat .$$lib.exp ;                                                        \
            echo FAILED ;                                                           \
            failed=1 ;                                                              \
          else                                                                      \
            echo PASSED ;                                                           \
          fi ;                                                                      \
          rm -f .$$lib.exp ;                                                        \
          exit $$failed
else
check-exports: libammpi.a
	@echo check-exports test SKIPPED
endif

check: check-exports tests

clean_local_files = libammpi.a *.o core
clean-local:
	@for f in $(testprograms); do                                \
	  cmd="rm -f '$$f$(EXESUFFIX)'"; echo "$$cmd"; eval "$$cmd" || exit $$? ; \
	 done;
else
clean_local_files =
clean-local:
	@: #empty rule
endif

CLEANFILES = $(clean_local_files)

force:

.PHONY: force tests run-tests

