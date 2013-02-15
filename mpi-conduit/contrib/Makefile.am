## Process this file with automake to produce Makefile.in       -*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/mpi-conduit/contrib/Makefile.am,v 1.1 2003/11/09 00:53:43 phargrov Exp $
# Description: Makefile for GASNet MPI spawner
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

target = gasnetrun_mpi

if USE_MPI_CONDUIT
scripts = $(target) $(target).pl
else
scripts =
endif
bin_SCRIPTS = $(scripts)

# Ensure the .pl is distributed, even if not installed
EXTRA_DIST = $(target).pl
