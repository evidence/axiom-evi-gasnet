## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/Makefile.am,v $
#     $Date: 2005/01/21 22:05:51 $
# $Revision: 1.17 $
# Description: Makefile for GASNet other 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =          \
        ammpi      \
	amudp	   \
	contrib    \
	testspawn


EXTRA_DIST =       \
	amxtests   \
	firehose   \
	ssh-spawner\
	mpi-spawner

include_HEADERS = \
	portable_inttypes.h

if USE_UDP_CONDUIT                           
  UDP_dir=$(top_builddir)/other/amudp
endif
if USE_MPI_CONDUIT                           
  MPI_dir=$(top_builddir)/other/ammpi
endif
testdirs= $(UDP_dir) $(MPI_dir)

tests: 
	@list='$(testdirs)'; for dir in $$list ; do \
	  $(MAKE) -C "$$dir" tests ;                \
	done

run-tests: 
	@list='$(testdirs)'; for dir in $$list ; do \
	  $(MAKE) -C "$$dir" run-tests ;            \
	done

all-local: Makefile-conduit.mak perlstart

.PHONY: force clean tests run-tests

force:
	
