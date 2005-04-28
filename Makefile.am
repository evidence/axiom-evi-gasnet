## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/Makefile.am,v $
#     $Date: 2005/04/28 21:15:19 $
# $Revision: 1.38 $
# Description: Makefile for GASNet 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

# Directories for automake to recurse into:
SUBDIRS =            \
	config-aux   \
        other

# Directories we manually recurse into:
CONDUIT_DIRS =       \
        elan-conduit \
        gm-conduit   \
        lapi-conduit \
	mpi-conduit  \
	sci-conduit  \
	shmem-conduit  \
	smp-conduit  \
	udp-conduit  \
	vapi-conduit

DIST_SUBDIRS = $(SUBDIRS) $(CONDUIT_DIRS)

configdir = $(includedir)
config_DATA = $(CONFIG_HEADER)
docdir = $(datadir)/doc/gasnet

headers =                  \
        gasnet.h           \
	gasnet_ammacros.h  \
	gasnet_atomicops.h \
	gasnet_atomicops_internal.h \
        gasnet_basic.h     \
        gasnet_handler.h   \
        gasnet_help.h      \
        gasnet_timer.h     \
	gasnet_tools.h     \
	gasnet_trace.h     

EXTRA_DIST =              \
        README            \
	ChangeLog	  \
	license.txt       \
	docs              \
        extended-ref      \
	template-conduit  \
	.autom4te.cfg     \
        Bootstrap         \
        unBootstrap       \
        configure.in      \
        acconfig.h        \
        acinclude.m4      \
        $(headers)        \
        gasnet_internal.h \
        gasnet_internal.c \
        gasnet_mmap.c 
#       tests handled below

include_HEADERS = $(headers)

force:

# echo an arbitrary make/environment variable
echovar: force
	@echo $(VARNAME)=\"$($(VARNAME))\"
	
test: tests-seq
tests: tests-seq

other-tests:
	@$(MAKE) -C $(top_builddir)/other all tests

other-run-tests:
	@$(MAKE) -C $(top_builddir)/other run-tests

tests-seq: other-tests
	@for conduit in $(CONDUITS) ; do                                      \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" tests-seq || exit 1; \
	done

tests-par: other-tests
	@for conduit in $(CONDUITS) ; do                                      \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" tests-par || exit 1; \
	done

tests-parsync: other-tests
	@for conduit in $(CONDUITS) ; do                                          \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" tests-parsync || exit 1; \
	done

run-tests: other-run-tests
	@for conduit in $(CONDUITS) ; do                                      \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" run-tests || exit 1; \
	done

run-tests-seq: tests-seq run-tests
run-tests-par: tests-par run-tests
run-tests-parsync: tests-parsync run-tests

# Build, clean, install, uninstall, etc. the conduits - in parallel if possible.
# Instead of automake's loop over subdir, we expand to one target per subdir.
# Care is taken w/ the 'clean-local' rule in case distclean has wiped the 
# local or conduit Makefiles (order depends on the version of automake).
all-local:
	@$(MAKE) DO_WHAT=all `for conduit in $(CONDUITS) ; do echo do-$$conduit; done`
clean-local:
	@if test -f Makefile -a -f smp-conduit/Makefile; then \
	  $(MAKE) DO_WHAT=clean `for conduit in $(CONDUITS); do echo do-$$conduit; done`; \
	 fi
check:
	@$(MAKE) DO_WHAT=check `for conduit in $(CONDUITS) ; do echo do-$$conduit; done`
install-data-local:
	@$(MAKE) DO_WHAT=install-data `for conduit in $(CONDUITS) ; do echo do-$$conduit; done`
	$(mkinstalldirs) $(DESTDIR)$(docdir)
	cp -f $(top_srcdir)/README $(DESTDIR)$(docdir)/README
	cp -f $(top_srcdir)/extended-ref/README $(DESTDIR)$(docdir)/README-extref
	cp -f $(top_srcdir)/license.txt $(DESTDIR)$(docdir)/license.txt
install-exec-local:
	@$(MAKE) DO_WHAT=install-exec `for conduit in $(CONDUITS) ; do echo do-$$conduit; done`
uninstall-local:
	@$(MAKE) DO_WHAT=uninstall `for conduit in $(CONDUITS) ; do echo do-$$conduit; done`
	rm -Rf $(DESTDIR)$(docdir)

do-elan:
	$(MAKE) -C elan-conduit $(DO_WHAT)
do-gm:
	$(MAKE) -C gm-conduit $(DO_WHAT)
do-lapi:
	$(MAKE) -C lapi-conduit $(DO_WHAT)
do-mpi:
	$(MAKE) -C mpi-conduit $(DO_WHAT)
do-sci:
	$(MAKE) -C sci-conduit $(DO_WHAT)
do-shmem:
	$(MAKE) -C shmem-conduit $(DO_WHAT)
do-smp:
	$(MAKE) -C smp-conduit $(DO_WHAT)
do-udp:
	$(MAKE) -C udp-conduit $(DO_WHAT)
do-vapi:
	$(MAKE) -C vapi-conduit $(DO_WHAT)


.PHONY: force clean tests test                                        \
        other-tests other-run-tests tests-seq tests-par tests-parsync \
        run-tests-seq run-tests-par run-tests-parsync                 \
        do-elan do-gm do-lapi do-mpi do-sci do-smp do-udp do-vapi


dist-hook:
	if test -f $(distdir)/docs/Makefile ; then \
          $(MAKE) -C $(distdir)/docs dist-hook ; \
        fi
	mkdir -p $(distdir)/tests  # need this special hack to handle tests or distcheck will fail
	fulldistdir=`cd $(distdir) ; @PWD_PROG@` export fulldistdir ; \
        cd $(srcdir)/tests ; for file in `find . -print` ; do \
 	  if test -d $$file -a ! -d $$fulldistdir/tests/$$file ; then \
	     mkdir -p $$fulldistdir/tests/$$file ; \
          fi; \
	  if test -f $$file -a ! -f $$fulldistdir/tests/$$file ; then \
	     cp -p $$file $$fulldistdir/tests/$$file ; \
          fi ; \
        done
	rm -Rf `find $(distdir) -name CVS -type d -print`
	rm -f `find $(distdir) -name .#\* -print`


