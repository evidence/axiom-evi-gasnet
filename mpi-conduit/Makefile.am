## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2002/06/01 14:24:57 $
# $Revision: 1.1 $
# Description: Makefile for GASNet MPI conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

AUTOMAKE_OPTIONS = foreign 1.4

if HAVE_PTHREAD
thread_defines = @GASNET_THREAD_DEFINES@
endif

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir)        \
           -I$(top_srcdir)/extended-ref -I$(top_srcdir)/other/ammpi 
	   

filelist =                   \
      gasnet_core.c          \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h 

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c          \
      $(top_srcdir)/extended-ref/gasnet_extended.c 

EXTRA_DIST = $(filelist)

libgasnet_sources = $(srcdir)/gasnet_core.c $(othersourcefiles)
libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.h                \
                         $(top_srcdir)/extended-ref/*.h       \
			 $(top_srcdir)/other/ammpi/*.h
libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-mpi-par.a libgasnet-mpi-parsync.a
endif
libraries = libgasnet-mpi-seq.a $(threaded_libraries)

VPATH=$(srcdir)

libgasnet-mpi-seq.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-mpi-par.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-mpi-parsync.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_MPI_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_srcdir)/tests/Makefile clean

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h $(top_srcdir)/other/ammpi/ammpi*.h 

install-data-local: $(headers)
	$(mkinstalldirs) $(includedir)/mpi-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p` \
	    echo " $(INSTALL_DATA) $$p $(includedir)/mpi-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(includedir)/mpi-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(includedir)/mpi-conduit/*.h

endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

TESTLIBS = -L$(top_builddir)/other/ammpi -lammpi \
           $(MPI_LIBS) \
	   $(LIBGCC) $(LIBS) $(LIBM)

tests-seq: force libgasnet-mpi-seq.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  COMPILE="$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_SEQ" \
	  LINK="$(MPI_CC) $(MPI_CFLAGS)"                     \
	  LINKEND="-L$(top_builddir)/mpi-conduit -lgasnet-mpi-seq $(TESTLIBS)" 

tests-par: force libgasnet-mpi-par.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  COMPILE="$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PAR $(thread_defines)" \
	  LINK="$(MPI_CC) $(MPI_CFLAGS)"                     \
	  LINKEND="-L$(top_builddir)/mpi-conduit -lgasnet-mpi-par $(TESTLIBS) $(GASNET_THREAD_LIBS)" 

tests-parsync: force libgasnet-mpi-parsync.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	         \
	  srcdir="$(top_srcdir)/tests"			         \
	  COMPILE="$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PARSYNC $(thread_defines)" \
	  LINK="$(MPI_CC) $(MPI_CFLAGS)"                         \
	  LINKEND="-L$(top_builddir)/mpi-conduit -lgasnet-mpi-parsync $(TESTLIBS) $(GASNET_THREAD_LIBS) " 

force:
	


