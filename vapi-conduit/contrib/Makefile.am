## Process this file with automake to produce Makefile.in       -*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/contrib/Attic/Makefile.am,v 1.1 2003/11/09 00:53:46 phargrov Exp $
# Description: Makefile for GASNet VAPI spawner
# Terms of use are as specified in vapi-conduit/license.txt

AUTOMAKE_OPTIONS = foreign 1.4

target = gasnetrun_vapi
$(target).pl: $(top_srcdir)/mpi-conduit/contrib/gasnetrun_mpi.pl
	cp $< $@

if USE_VAPI_CONDUIT
scripts = $(target) $(target).pl
else
scripts =
endif
bin_SCRIPTS = $(scripts)
