#  $Archive:: /Ti/GASNet/tests/Makefile                                  $
#     $Date: 2002/06/01 14:24:57 $
# $Revision: 1.1 $
# Description: Makefile for GASNet tests
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

all:
	@echo This Makefile is not intended to be called directly
	@echo do a "make tests" within the core directory

testprograms =      \
        testbarrier \
        testgasnet  \
        testhsl     \
        testlarge   \
        testsmall   

tests: $(testprograms)
	
clean:
	rm -f $(testprograms)

test% : force
	${COMPILE} -I$(srcdir) -c -o $@.o $(srcdir)/$@.c ${COMPILEEND}
	${LINK} -o $@ $@.o ${LINKEND}

force:
	

.PHONY: force all

