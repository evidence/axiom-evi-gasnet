#  $Archive:: /Ti/GASNet/docs/Makefile                                   $
#     $Date: 2002/06/13 06:06:11 $
# $Revision: 1.3 $
# Description: Makefile for GASNet documentation
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>

# The GASNet specification in distributed in 3 primary formats (text, html and postscript)
# note that older version of makeinfo and TeX may have trouble correctly generating gasnet.ps

all: gasnet.txt gasnet.html gasnet.ps

# ------------------------------------------------------------------------------------
# TeX Postscript
gasnet.dvi: gasnet.texi
	texi2dvi -c -q gasnet.texi

gasnet.ps: gasnet.dvi
	rm -f gasnet.ps
	dvips -f gasnet.dvi > gasnet.ps

gasnet.pdf: gasnet.dvi
	rm -f gasnet.pdf
	dvipdf gasnet.dvi 

gasnet2.pdf: gasnet.texi
	rm -f gasnet2.pdf
	texi2dvi -c -p -o gasnet2.pdf gasnet.texi

# ------------------------------------------------------------------------------------
# Text
gasnet.txt: gasnet.texi
	makeinfo --no-headers --no-validate gasnet.texi > gasnet.txt
# ------------------------------------------------------------------------------------
# Monolithic HTML
gasnet.html: gasnet.texi
	makeinfo --html --no-split --no-headers --no-validate gasnet.texi > gasnet.html
# ------------------------------------------------------------------------------------
# Segmented HTML (not really supported)
gasnet: gasnet.texi
	makeinfo --html --no-validate gasnet.texi
# ------------------------------------------------------------------------------------
# info file (not really supported)
gasnet.info: gasnet.texi
	makeinfo gasnet.texi

info: gasnet.info
	info -f gasnet.info
# ------------------------------------------------------------------------------------
unix:
	txt2unix gasnet.texi
# ------------------------------------------------------------------------------------
clean:
	rm -f gasnet.info gasnet.html gasnet.ps gasnet.dvi gasnet.txt gasnet.pdf gasnet2.pdf
	rm -f gasnet.ky gasnet.pg gasnet.toc gasnet.tmp gasnet.tp gasnet.vr gasnet.pg gasnet.log gasnet.cp gasnet.aux gasnet.fn gasnet.fns gasnet.cps
	rm -Rf gasnet

.PHONY: unix info gasnet clean
