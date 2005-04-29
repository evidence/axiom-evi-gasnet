## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/contrib/Makefile.am,v $
#     $Date: 2005/04/29 04:23:01 $
# $Revision: 1.5 $
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
	@$(MAKE) -C $(top_builddir)/other perlstart

gasnet_trace: $(top_builddir)/other/perlstart gasnet_trace.pl

# Files to copy from srcdir to builddir if they differ
if BUILD_IS_SRC
files_we_copy = 
else
files_we_copy = $(dotpl_DATA)
$(files_we_copy): force
	cp -f "$(srcdir)/$@" .
endif
DISTCLEANFILES = $(files_we_copy)

.PHONY: force

force:
	
