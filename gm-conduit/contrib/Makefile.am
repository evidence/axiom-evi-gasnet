## Process this file with automake to produce Makefile.in       -*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/contrib/Attic/Makefile.am,v 1.7 2003/11/08 23:34:17 bonachea Exp $
# Description: Makefile for GASNet gm conduit
# Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

builddir = $(top_builddir)/gm-conduit/contrib

bin_SCRIPTS = gasnetrun_gm.pl gasnetrun_gm

# make sure certain files get rebuilt if their source files changed.
all-local: $(builddir)/gasnetrun_gm $(builddir)/gasnetrun_gm.pl

EXTRA_DIST = gasnetrun_gm.pl

distclean-local:
	@if test "`cd $(builddir);pwd`" != "`cd $(srcdir);pwd`"; then \
            echo rm -f $(builddir)/gasnetrun_gm.pl; \
            rm -f $(builddir)/gasnetrun_gm.pl; \
         fi;

if USE_GM_CONDUIT
else
# Yuk - apparently bin_SCRIPTS cannot be conditionally defined, so
# we use this hack to prevent them from being installed on non-gm systems
install-exec-local:
	rm -f $(DESTDIR)$(bindir)/gasnetrun_gm
	rm -f $(DESTDIR)$(bindir)/gasnetrun_gm.pl
endif

$(builddir)/gasnetrun_gm.pl: force
	@if test "`cd $(builddir);pwd`" != "`cd $(srcdir);pwd`"; then \
            echo cp -f $(srcdir)/gasnetrun_gm.pl $(builddir); \
            cp -f $(srcdir)/gasnetrun_gm.pl $(builddir); \
         fi;

force:


.PHONY: force

