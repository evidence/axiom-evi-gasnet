/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <plpa.h>


int main(int argc, char *argv[]) 
{
    int i;
    int ret = 0;
    int need_help = 0;

    for (i = 1; i < argc; ++i) {
        if (0 == strcmp("--version", argv[i])) {
            printf("PLPA version %s\n", PACKAGE_VERSION);
            exit(0);
        } else if (0 == strcmp("--help", argv[i])) {
            need_help = 1;
            ret = 0;
            break;
        } else {
            printf("%s: unrecognized option: %s\n",
                   argv[0], argv[i]);
            need_help = 1;
            ret = 1;
        }
    }

    if (need_help) {
        printf("usage: %s [--version] [--help]\n", argv[0]);
        return ret;
    }
    
    switch (PLPA_NAME(api_probe)()) {
    case PLPA_PROBE_OK:
        printf("PLPA_PROBE_OK\n");
        break;
    case PLPA_PROBE_NOT_SUPPORTED:
        printf("PLPA_PROBE_NOT_SUPPORTED\n");
        break;
    default:
        printf("PLPA_PROBE_UNKNOWN\n");
        break;
    }

    return 0;
}
