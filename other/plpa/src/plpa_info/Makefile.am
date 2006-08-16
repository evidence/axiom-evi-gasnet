#
# Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
#                         University Research and Technology
#                         Corporation.  All rights reserved.
# Copyright (c) 2004-2005 The Regents of the University of California.
#                         All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

# Default to empty
bin_PROGRAMS=

AM_CPPFLAGS = -I$(top_srcdir)/src/libplpa

# See which mode we're building in
if PLPA_BUILD_STANDALONE
bin_PROGRAMS += plpa_info
endif

plpa_info_SOURCES = plpa_info.c
plpa_info_LDADD = $(top_builddir)/src/libplpa/libplpa.la
