## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/Attic/Makefile.am,v 1.3 2002/06/13 10:09:32 csbell Exp $
# Description: Makefile for GASNet gm conduit
# Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@
gm_include = @GM_INCLUDE@
platform_libraries = @LIBS@ -L@GM_LIB@
platform_defines = @GASNET_MACHINE_DEFINES@
linkend = -lgm

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir) -I$(gm_include) \
           -I$(top_srcdir)/extended-ref $(platform_defines)
	   

filelist = gasnet_core.c gasnet_core_receive.c gasnet_core_misc.c \
           gasnet_core.h gasnet_core_fwd.h gasnet_core_help.h \
	   gasnet_core_types.h gasnet_core_internal.h 

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c          \
      $(top_srcdir)/extended-ref/gasnet_extended.c 

EXTRA_DIST = $(filelist)

libgasnet_sources = $(srcdir)/gasnet_core.c $(srcdir)/gasnet_core_receive.c \
                    $(srcdir)/gasnet_core_misc.c $(othersourcefiles)
libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.h                \
                         $(top_srcdir)/extended-ref/*.h       
libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o \
                    gasnet_core_receive.o gasnet_core_misc.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-gm-par.a libgasnet-gm-parsync.a
endif
libraries = libgasnet-gm-seq.a $(threaded_libraries)

VPATH=$(srcdir)

libgasnet-gm-seq.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-gm-par.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-gm-parsync.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(INCLUDES) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_GM_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_srcdir)/tests/Makefile clean

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h 
makefile_fragments = gm-seq.mak gm-parsync.mak gm-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(includedir)/gm-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(includedir)/gm-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(includedir)/gm-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " grep -v "###NOINSTALL###" $$p > $(includedir)/gm-conduit/$$filename"; \
	    grep -v "###NOINSTALL###" $$p > $(includedir)/gm-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(includedir)/gm-conduit/*.h $(includedir)/gm-conduit/*.mak

endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-gm-seq.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="gm-seq.mak"

tests-parsync: force libgasnet-gm-parsync.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="gm-parsync.mak"

tests-par: force libgasnet-gm-par.a
	@$(MAKE) -f $(top_srcdir)/tests/Makefile tests	     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="gm-par.mak"

force:
	


