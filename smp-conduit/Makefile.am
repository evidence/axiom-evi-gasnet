## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/Makefile.am,v 1.12 2004/03/11 08:46:15 bonachea Exp $
# Description: Makefile for GASNet smp conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

CONDUIT_NAME=smp

# any conduit-specific subdirectories containing Makefile.am's
SUBDIRS = 

# complete list of files in the conduit directory
# include all headers, documentation, etc. 
# and any subdirectories not containing Makefile.am's
CONDUIT_FILELIST =           \
      gasnet_core.c          \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h \
      README

# list of conduit core and extended .c source files 
# to be compiled into libgasnet on the compiler command line
CONDUIT_SOURCELIST =          \
      $(srcdir)/gasnet_core.c \
      $(top_srcdir)/extended-ref/gasnet_extended.c 

# additional -I or -D directives needed by this specific conduit
# other than the standard GASNet includes and flags
CONDUIT_EXTRALIBCFLAGS = 

# additional conduit header files to install from external, non-standard directories
CONDUIT_EXTRAHEADERS = 

# additional file dependencies not mentioned elsewhere 
# that should force libgasnet rebuild on update
CONDUIT_EXTRADEPS = 

# -------- Do not modify anything below this line --------

if HAVE_PTHREAD
libgasnet_smp_par_a_SOURCES =
libgasnet_smp_parsync_a_SOURCES =
threaded_libraries = libgasnet-smp-par.a libgasnet-smp-parsync.a
endif
libgasnet_smp_seq_a_SOURCES =
libraries = libgasnet-smp-seq.a $(threaded_libraries)

include $(top_builddir)/other/Makefile-conduit.mak

libgasnet-smp-seq.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-seq

libgasnet-smp-par.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-par

libgasnet-smp-parsync.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-parsync

if USE_SMP_CONDUIT  
lib_LIBRARIES = $(libraries) 
all: do-all
clean-local: do-clean-local
install-data-local: do-install-data-local
uninstall-local: do-uninstall-local
else
$(top_srcdir)/gasnet.h: do-error
endif


