## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2003/11/08 23:34:19 $
# $Revision: 1.4 $
# Description: Makefile for GASNet other 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =          \
        ammpi      

EXTRA_DIST =       \
	firehose   \
	perlstart


tests: force
if USE_MPI_CONDUIT                           
	@$(MAKE) -C $(top_builddir)/other/ammpi tests
endif

.PHONY: force clean tests

force:
	
