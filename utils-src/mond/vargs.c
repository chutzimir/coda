#ifndef _BLURB_
#define _BLURB_
/*
                           CODA FILE SYSTEM
                             Release 3.0
                      School of Computer Science
                      Carnegie Mellon University
                          Copyright 1987-95

This  code  may  be used outside Carnegie Mellon University only under
the terms of the Coda license.  All other use is illegal.
*/


static char *rcsid = "$Header$";
#endif /*_BLURB_*/


#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <libc.h>

#ifdef __cplusplus
}
#endif __cplusplus

#include <stdarg.h>
#include "util.h"

extern FILE *LogFile;
extern int LogLevel;
extern void Log_Done(void);

/* Print an error message and then exit. */
void Die(char *fmt ...) {
    static int dying = 0;

    if (!dying) {
	/* Avoid recursive death. */
	dying = 1;

	/* Log the message, with an indication that it is fatal. */
	LogMsg(-1,LogLevel,LogFile," ***** Fatal Error");
	va_list ap;
	va_start(ap, fmt);
	LogMsg(-1,LogLevel,LogFile,fmt,ap);
	va_end(ap);

    }

    /* Leave a core file. */
    kill(getpid(), SIGFPE);

    /* NOTREACHED */
}

