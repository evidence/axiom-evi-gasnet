 /usr/bin/ar cru /global/u1/s/stewart/gasnet/GASNet-1.16.1/gemini-conduit/libgasnet-gemini-seq.a gasnet_core.o gasnet_extended.o gc.o gc_utils.o gasnet_tools.o plpa_api_probe.o plpa_dispatch.o gasnet_extended_refvis.o gasnet_extended_refcoll.o gasnet_coll_putget.o gasnet_coll_eager.o gasnet_coll_rvous.o gasnet_coll_team.o gasnet_coll_hashtable.o gasnet_internal.o gasnet_trace.o gasnet_mmap.o gasnet_diagnostic.o 

/opt/cray/xt-asyncpe/4.9/bin/cc -target=linux  -I"/global/u1/s/stewart/gasnet/GASNet-1.16.1/tests"   -DGASNET_SEQ    -I/global/u1/s/stewart/gasnet/GASNet-1.16.1 -I/global/u1/s/stewart/gasnet/GASNet-1.16.1/gemini-conduit -I/global/u1/s/stewart/gasnet/GASNet-1.16.1/other   -I/global/u1/s/stewart/gasnet/GASNet-1.16.1/extended-ref -I/global/u1/s/stewart/gasnet/GASNet-1.16.1    -g3 -Wall -Wno-unused -Wno-address -Wpointer-arith -Wnested-externs -Wwrite-strings -Wdeclaration-after-statement -Wmissing-format-attribute      -c -o testcore2.o "/global/u1/s/stewart/gasnet/GASNet-1.16.1/tests"/testcore2.c
/opt/cray/xt-asyncpe/4.9/bin/cc -target=linux  -g3 -Wall -Wno-unused -Wno-address -Wpointer-arith -Wnested-externs -Wwrite-strings -Wdeclaration-after-statement -Wmissing-format-attribute     -o testcore2 testcore2.o  -L/global/u1/s/stewart/gasnet/GASNet-1.16.1/gemini-conduit    -lgasnet-gemini-seq    -L/opt/gcc/4.5.2/snos/lib/gcc/x86_64-suse-linux/4.5.2 -lgcc -lm 
