# Firehose Makefile fragment
# This Makefile fragment is used to build GASNet conduits 
# it is not meant to be used directly 
#
# To use in a conduit Makefile.am, set fh_type to either "region" or
# "page" (no quotes, no whitespace) and then include this fragment.
# Example:
#    fh_type=region
#    include $(top_srcdir)/other/firehose/firehose.mak
#
# This fragment defines the following variables for use
# in the corresponding CONDUIT_* variables.  For instance,
# one should include $(fh_sourcelist) in CONDUIT_SOURCELIST
#
# fh_sourcelist     - C files to build normally
# fh_extralibcflags - CFLAGS need for firehose headers
# fh_extraheaders   - firehose headers to install
# fh_extradeps      - dependencies on firehose .c and .h files
# fh_special_objs   - firehose .o files with "abonormal" build requirements
#
# fh_srcdir is also set, but probably isn't needed outside this fragment

fh_srcdir = $(top_srcdir)/other/firehose
fh_sourcelist = $(fh_srcdir)/firehose_hash.c
fh_extraheaders = $(fh_srcdir)/firehose_trace.h
fh_extralibcflags = -I$(fh_srcdir)
fh_extradeps = $(fh_srcdir)/*.[ch]
fh_special_objs =                                   \
	$(builddir)/firehose-$(THREAD_MODEL).o      \
	$(builddir)/firehose_$(fh_type)-$(THREAD_MODEL).o

# Some Firehose source files need $(FH_CFLAGS) to disable strict/ANSI aliasing
# optimizations.  By building them as "special" we can avoid disabling these
# useful optimizations for the entire conduit.
$(builddir)/firehose-$(THREAD_MODEL).o: force
	@CC@ $(LIBCFLAGS) $(FH_CFLAGS) -o $@ -c $(fh_srcdir)/firehose.c
$(builddir)/firehose_$(fh_type)-$(THREAD_MODEL).o: force
	@CC@ $(LIBCFLAGS) $(FH_CFLAGS) -o $@ -c $(fh_srcdir)/firehose_$(fh_type).c

