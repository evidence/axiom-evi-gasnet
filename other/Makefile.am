## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2002/06/01 14:24:57 $
# $Revision: 1.1 $
# Description: Makefile for GASNet other 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =          \
        ammpi      

EXTRA_DIST =              


tests: force
if USE_MPI_CONDUIT                           
	@$(MAKE) -C $(top_builddir)/other/ammpi tests
endif

.PHONY: force clean tests

force:
	
