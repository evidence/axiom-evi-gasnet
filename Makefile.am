## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2002/07/08 13:00:31 $
# $Revision: 1.4 $
# Description: Makefile for GASNet 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =            \
        other        \
        elan-conduit \
        gm-conduit   \
        lapi-conduit \
	mpi-conduit 

configdir = $(includedir)
config_DATA = $(CONFIG_HEADER)

headers =                 \
	gasnet.h          \
	gasnet_basic.h    \
	gasnet_handler.h  \
	gasnet_help.h     \
	gasnet_internal.h

EXTRA_DIST =              \
        README            \
	docs              \
        extended-ref      \
	template-conduit  \
        tests             \
        Bootstrap         \
        unBootstrap       \
        configure.in      \
        acconfig.h        \
        acinclude.m4      \
        $(headers)        \
        gasnet_internal.c 

include_HEADERS = $(headers)

force:
	

tests: force
	@$(MAKE) -C $(top_builddir)/other tests
if USE_MPI_CONDUIT
	@$(MAKE) -C $(top_builddir)/mpi-conduit tests-seq
endif
if USE_GM_CONDUIT
	@$(MAKE) -C $(top_builddir)/gm-conduit tests-seq
endif
if USE_ELAN_CONDUIT
	@$(MAKE) -C $(top_builddir)/elan-conduit tests-seq
endif
if USE_LAPI_CONDUIT
	@$(MAKE) -C $(top_builddir)/lapi-conduit tests-seq
endif

.PHONY: force clean tests

