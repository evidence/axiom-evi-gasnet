## Process this file with automake to produce Makefile.in       -*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/mpi-conduit/contrib/Makefile.am,v $
#     $Date: 2005/04/29 04:22:55 $
# $Revision: 1.10 $
# Description: Makefile for GASNet MPI spawner
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

target = gasnetrun_mpi

# Need a bin_DATA rule, but automake disallows that.  So, fake it.
dotpldir = $(bindir)

# Make sure the perl script and shell wrapper are always current locally.
# This ensures we can run from the build directory if needed.
$(top_builddir)/other/perlstart: $(top_srcdir)/other/perlstart.in
	@$(MAKE) -C $(top_builddir)/other perlstart
$(target): $(top_builddir)/other/perlstart
all-local: $(target).pl $(target)

# Files to copy from srcdir to builddir if they differ
if BUILD_IS_SRC
files_we_copy =
else
files_we_copy = $(target).pl
$(files_we_copy): force
	cp -f "$(srcdir)/$@" .
endif
DISTCLEANFILES = $(files_we_copy)

# Ensure the .pl is distributed, even if not installed
EXTRA_DIST = $(target).pl

# Conditionally install the .pl and its wrapper
if USE_MPI_CONDUIT
scripts = $(target)
data = $(target).pl
else
scripts =
data =
endif
bin_SCRIPTS = $(scripts)
dotpl_DATA = $(data)

.PHONY: force
force:

