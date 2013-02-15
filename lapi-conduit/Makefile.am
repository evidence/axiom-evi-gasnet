## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/lapi-conduit/Attic/Makefile.am,v 1.1 2002/07/03 19:41:24 welcome Exp $
# Description: Makefile for GASNet lapi conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir)        \
           -I$(top_srcdir)/extended-ref  
	   

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
                         $(top_srcdir)/extended-ref/*.h       
libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-lapi-par.a libgasnet-lapi-parsync.a
endif
libraries = libgasnet-lapi-seq.a $(threaded_libraries)

VPATH=$(srcdir)

libgasnet-lapi-seq.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-lapi-par.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-lapi-parsync.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_LAPI_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_srcdir)/tests/Makefile clean       \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="lapi-seq.mak"

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h 
makefile_fragments = lapi-seq.mak lapi-parsync.mak lapi-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(includedir)/lapi-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(includedir)/lapi-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(includedir)/lapi-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " grep -v "###NOINSTALL###" $$p > $(includedir)/lapi-conduit/$$filename"; \
	    grep -v "###NOINSTALL###" $$p > $(includedir)/lapi-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(includedir)/lapi-conduit/*.h $(includedir)/lapi-conduit/*.mak

endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-lapi-seq.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="lapi-seq.mak"

tests-parsync: force libgasnet-lapi-parsync.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="lapi-parsync.mak"

tests-par: force libgasnet-lapi-par.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="lapi-par.mak"

force:
	


