## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/elan-conduit/Attic/Makefile.am,v 1.7 2003/01/05 04:44:02 bonachea Exp $
# Description: Makefile for GASNet elan conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt 

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir) \
           -I$(top_srcdir)/extended-ref                  \
	   -I@ELAN_INCLUDE@
	   

filelist =                   \
      gasnet_core.c          \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h \
      gasnet_core_dump.c     \
      gasnet_core_reqrep.c 

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c          

EXTRA_DIST = $(filelist)

libgasnet_sources =                  \
      $(srcdir)/gasnet_core.c        \
      $(srcdir)/gasnet_core_dump.c   \
      $(srcdir)/gasnet_core_reqrep.c \
      $(srcdir)/gasnet_extended.c    \
      $(othersourcefiles)

libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/*.h      
			  
libgasnet_objects =      \
      gasnet_core.o      \
      gasnet_internal.o  \
      gasnet_extended.o  \
      gasnet_core_dump.o \
      gasnet_core_reqrep.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-elan-par.a libgasnet-elan-parsync.a
endif
libraries = libgasnet-elan-seq.a $(threaded_libraries)

VPATH=$(srcdir)

ELAN_FLAGS=@ELAN_FLAGS@

libgasnet-elan-seq.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(ELAN_FLAGS) $(INCLUDES) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-elan-par.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(ELAN_FLAGS) $(INCLUDES) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-elan-parsync.a: $(libgasnet_dependencies)
	$(CC) $(CFLAGS) $(ELAN_FLAGS) $(INCLUDES) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_ELAN_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h                             \
  $(top_srcdir)/extended-ref/gasnet_extended.h      \
  $(top_srcdir)/extended-ref/gasnet_extended_help.h

makefile_fragments = elan-seq.mak elan-parsync.mak elan-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(includedir)/elan-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(includedir)/elan-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(includedir)/elan-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " grep -v "###NOINSTALL###" $$p > $(includedir)/elan-conduit/$$filename"; \
	    grep -v "###NOINSTALL###" $$p > $(includedir)/elan-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(includedir)/elan-conduit/*.h $(includedir)/elan-conduit/*.mak

endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-elan-seq.a
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="elan-seq.mak"

tests-parsync: force libgasnet-elan-parsync.a
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="elan-parsync.mak"

tests-par: force libgasnet-elan-par.a
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="elan-par.mak"

force:
	


