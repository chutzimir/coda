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


/*
 * dummy module for non-plumber mond
 */

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <stdio.h>

void SetMallocCheckLevel(int foo) {
    ;
}

void plumber(FILE *foo) {
    ;
}

#ifdef __cplusplus
}
#endif __cplusplus

void newSetCheckLevel(int foo)
{
    ;
}

void newPlumber(FILE *foo)
{
    ;
}

