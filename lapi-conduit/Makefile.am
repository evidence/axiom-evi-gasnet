## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/lapi-conduit/Attic/Makefile.am,v 1.23 2004/03/11 08:46:11 bonachea Exp $
# Description: Makefile for GASNet lapi conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

CONDUIT_NAME=lapi

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
      gasnet_extended.c      \
      gasnet_extended_fwd.h  \
      gasnet_extended_internal.h \
      README		     \
      STATUS                 \
      license.txt

# list of conduit core and extended .c source files 
# to be compiled into libgasnet on the compiler command line
CONDUIT_SOURCELIST =          \
      $(srcdir)/gasnet_core.c \
      $(srcdir)/gasnet_extended.c 

# additional include directives needed by this specific conduit
# other than the standard GASNet directories
CONDUIT_EXTRAINCLUDES = 

# additional conduit header files to install from directories in CONDUIT_EXTRAINCLUDES
CONDUIT_EXTRAHEADERS = 

# additional file dependencies not mentioned elsewhere 
# that should force libgasnet rebuild on update
CONDUIT_EXTRADEPS = 

# -------- Do not modify anything below this line --------

if HAVE_PTHREAD
libgasnet_lapi_par_a_SOURCES =
libgasnet_lapi_parsync_a_SOURCES =
threaded_libraries = libgasnet-lapi-par.a libgasnet-lapi-parsync.a
endif
libgasnet_lapi_seq_a_SOURCES =
libraries = libgasnet-lapi-seq.a $(threaded_libraries)

include $(top_builddir)/other/Makefile-conduit.mak

libgasnet-lapi-seq.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-seq

libgasnet-lapi-par.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-par

libgasnet-lapi-parsync.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-parsync

if USE_LAPI_CONDUIT  
lib_LIBRARIES = $(libraries) 
all: do-all
clean-local: do-clean-local
install-data-local: do-install-data-local
uninstall-local: do-uninstall-local
else
$(top_srcdir)/gasnet.h: do-error
endif


