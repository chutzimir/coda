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


//
// mondmon.c
//
// watchdogs the mond data collector
//
//

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <libc.h>
#include <signal.h>
#include <sys/wait.h>
#include <strings.h>
#include <stdio.h>

#ifdef __cplusplus
}
#endif __cplusplus

#include "util.h"

#define  STREQ(a, b) (strcmp((a), (b)) == 0)

void CheckSyntax(int, char **);
void ShutDown(void);
void Pause(void);
void Continue(void);

int shutdown = 0;
int dopause = 0;

main(int argc, char *argv[])
{
    int notfirst = 0;

    LogMsg(0,0,stdout,"Mondmon: log started, pid = %d",getpid());
    CheckSyntax(argc,argv);
    if (argc < 1)
	exit(-1);
    (void) signal(SIGTERM, (void (*)(int))ShutDown);
    (void) signal(SIGPIPE, (void (*)(int))Continue);
    (void) signal(SIGTSTP, (void (*)(int))Pause);
    
    while (!shutdown)
    {
	if (dopause) {
	    LogMsg(0,0,stdout,"Mondmon: paused");
	    pause();
	    notfirst = 0;
	    LogMsg(0,0,stdout,"Mondmon: continued");
	    continue;
	}
	argv[0] = "mond";
	LogMsg(0,0,stdout,"Mondmon: Attempting to start mond");
	int child = fork();
	if (child) {
	    notfirst++;
	    while(child != wait(0))
		;
	    LogMsg(0,0,stdout,"Mondmon: mond died");
	} else {
	    if (execve("/usr/mond/bin/mond",argv,0)) {
		LogMsg(0,0,stdout,"Monmon: execve mond failed");
		exit(0);
	    }
	}
	if (notfirst) {
	    sleep(30);
	    continue;
	}
    }
    LogMsg(0,0,stdout,"Mondmon: finished");
}

void CheckSyntax(int argc, char *argv[])
{
    /* check the command line syntax to make sure mond won't choke */
    for (int i = 1; i < argc; i++) {
	if ((STREQ(argv[i], "-r")) || (STREQ(argv[i],"-R"))) {
	    continue;
	}
	if (STREQ(argv[i], "-nospool")) {
	    continue;
	}
	if (STREQ(argv[i], "-wd")) {		/* working directory */
	    i++;
	    continue;
	}
	else if	(STREQ(argv[i], "-mondp")) {	/* vmon/smon port */
	    i++;
	    continue;
	}
	else if	(STREQ(argv[i], "-d")) {	/* debug */
	    i++;
	    continue;
	}
	else if	(STREQ(argv[i], "-b")) {	/* buffer size */
	    i++;
	    continue;
	}
	else if	(STREQ(argv[i], "-l")) {	/* listeners */
	    i++;
	    continue;
	}
	else if	(STREQ(argv[i], "-w")) {	/* low water mark */
	    i++;
	    continue;
	}
	else if	(STREQ(argv[i], "-ui")) {	/* low water mark */
	    i++;
	    continue;
	}
	else {
	    LogMsg(0,0,stdout,"Unrecognized command line option, %s\n",argv[i]);
	    printf("usage: mondmon [[-wd workingdir] [-mondp port number] [-d debuglevel]\n");
	    printf("                [-b buffersize] [-l listeners] [-w lowWaterMark]\n");
	    printf("                [-ui utility interval]\n");
	    exit(1000);
	}
    }
}

void Pause(void)
{
    LogMsg(0,0,stdout,"Mondmon: pausing");
    dopause = 1;
}

void Continue(void)
{
    LogMsg(0,0,stdout,"Mondmon: continuing");
    dopause = 0;
}

void ShutDown(void)
{
    LogMsg(0,0,stdout,"Mondmon: exiting");
    shutdown = 1;
}

