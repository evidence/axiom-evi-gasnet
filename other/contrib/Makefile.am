## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/contrib/Makefile.am,v $
#     $Date: 2005/05/16 23:57:50 $
# $Revision: 1.7 $
# Description: Makefile for GASNet other/contrib
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =         

EXTRA_DIST = $(dotpl_DATA)

dotpldir = $(bindir)
dotpl_DATA = gasnet_trace.pl

bin_SCRIPTS = gasnet_trace

all-local: $(bin_SCRIPTS) $(dotpl_DATA)

$(top_builddir)/other/perlstart: $(top_srcdir)/other/perlstart.in
	@cd $(top_builddir)/other && $(MAKE) perlstart

gasnet_trace: $(top_builddir)/other/perlstart gasnet_trace.pl

# Files to copy from srcdir to builddir if they differ
if BUILD_IS_SRC
files_we_copy = 
else
files_we_copy = $(dotpl_DATA)
$(files_we_copy): force
	@srcfile="$(srcdir)/`basename $@`" ; \
         if test "$$srcfile" -nt "$@" ; then \
          echo cp -f "$$srcfile" . ;         \
          cp -f "$$srcfile" . ;              \
         fi
endif
DISTCLEANFILES = $(files_we_copy)

.PHONY: force

force:
	
