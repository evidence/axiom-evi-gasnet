//   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/amudp/sig.h,v $
//     $Date: 2004/08/26 04:53:50 $
// $Revision: 1.2 $
// Description: signal handling module
// Copyright 1999, Dan Bonachea

#ifndef _SIG_H
#define _SIG_H

#include <signal.h>

#ifdef IRIX
#define signal(a,b) bsd_signal(a,b)
#endif

typedef void (*LPSIGHANDLER)(int);

// all signal handlers must match prototype above
// can also use special signal handlers:
//   SIG_DFL restore default signal handler
//   SIG_IGN to ignore signals

typedef enum {  ST_PROGRAM_ERROR, // program errors that will continus to occur if ignored 
                ST_TERM_INT,      // interrupt signal from the terminal (stop or kill)
                ST_SYS_INT,       // interrupt signal from the system (kill, hangup, etc.)
                ST_FATAL,         // interrupts that cannot be caught or ignored
                ST_OTHER,         // everything else 
                ST_ALL_CATCHABLE  // special flag to represent all signals except ST_FATAL
              } SIGTYPE;


// register a signal handler
// it will be passed the signal as a parameter
// the handler must re-register itself to continue receiving signals
LPSIGHANDLER reghandler(int sigtocatch, LPSIGHANDLER fp);

// register a signal handler for all catchable signals 
void regallhandler(LPSIGHANDLER fp, SIGTYPE sigtype=ST_ALL_CATCHABLE);

// return a string decribing a signal
const char* sigstr(int sig);

SIGTYPE getsigtype(int sig);


#endif
