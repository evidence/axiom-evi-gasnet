# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/tests/Attic/Makefile,v 1.6 2002/12/19 18:31:54 bonachea Exp $
# Description: Makefile for GASNet tests
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

all:
	@echo This Makefile is not intended to be called directly
	@echo do a "make tests" within the core directory

include $(configfile)

testprograms =      \
	testam      \
        testbarrier \
	testcore1   \
        testgasnet  \
        testhsl     \
        testlarge   \
	testmisc    \
        testsmall

tests: $(testprograms)
	
clean:
	rm -f $(testprograms)

test% : force
	$(GASNET_CC) -I$(srcdir) $(GASNET_CFLAGS) -c -o $@.o $(srcdir)/$@.c
	$(GASNET_LD) $(GASNET_LDFLAGS) -o $@ $@.o $(GASNET_LIBS)

force:
	

.PHONY: force all

