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

#include <sys/file.h>
#include <stdio.h>
#include <libc.h>
#include <errno.h>

#ifdef __cplusplus
}
#endif __cplusplus

main(int argc, char **argv)
{
    if (argc < 2) {
	fprintf (stderr,"Usage: sqlunlock <filename>\n");
	exit (-1);
    }
    for (int i = 1; i<argc; i++) {
	char *file = argv[i];
	int fd = open(file,O_RDWR,0);
	if (fd < 0) {
	    fprintf (stderr, "Could not open file %s; error #%d\n",
		     file,errno);
	    break;
	}
	int locked = flock(fd, (LOCK_SH | LOCK_NB));
	if (!locked) {
	    flock(fd, LOCK_UN);
	    fprintf (stderr, "File %s not locked\n",file);
	    break;
	}
	if (errno != EWOULDBLOCK) {
	    fprintf (stderr, "Lock test for file %s failed [%d]\n",
		     file,errno);
	    break;
	}
	if (flock(fd,LOCK_UN)) {
	    fprintf (stderr, "Unlock of file %s failed [%d]\n",
		     file,errno);
	    break;
	}
    }
}
	
