#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2002/06/01 14:24:57 $
# $Revision: 1.1 $
# Description: Makefile for AMMPI
# Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>

# uncomment one of the two lines below based on environment

include Makefile.${OSTYPE}.${MACHTYPE} # use prebaked Makefiles
#include ./src/Makefile.titanium # work within Titanium's autoconf build infrastructure

# Uncomment one set of lines below to get debug or release configuration 

#ccflags = -DDEBUG ${ccdebugflags}
ccflags = -DNDEBUG ${ccoptflags}

#-------------------------------------------------------------------------------------
# You should never need to change things below this line

VPATH = $(srcdir)
includes = -I$(srcdir)
libraries = ${platform_libraries}
Ccompile = $(CC) -c ${ccflags} ${platform_defines} ${includes}
link = $(CC) ${ccflags} ${platform_defines} ${includes} 
linkend = ${libraries}

# all the test executables
testprograms = testammpi testgetput testreadwrite testping testlatency testbulk testbounce testreduce

# all the library objects and headers
objects=ammpi_ep.o ammpi_reqrep.o ammpi_spmd.o 
headers=Makefile* ammpi.h ammpi_internal.h ammpi_spmd.h

.PHONY: all tests clean veryclean

all: libammpi.a

# ---------- AMMPI library -------------
libammpi.a: ${objects}
	${ar} cru $@ ${objects}
	${ranlib} $@

# ---------- build targets -------------
# main target
tests: ${testprograms}

# method to convert .cpp to .o (more reliable than ".cpp.o" method)
%.o : %.c ${headers}
	${Ccompile} $< -o $@

%.o : %.cpp ${headers}
	${CXXcompile} $< -o $@

# delete compiling byproducts
clean:
	rm -f *.o core gmon.out

veryclean: clean 
	rm -f ${testprograms} libammpi.a
	@echo Done verycleaning.


# ---------- test programs -------------
test% : test%.o apputils.o libammpi.a ${libufxp}
	${link} -o $@ $< apputils.o -L. -lammpi ${linkend}

