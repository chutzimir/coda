#ifndef _BLURB_
#define _BLURB_
/*

            Coda: an Experimental Distributed File System
                             Release 4.0

          Copyright (c) 1987-1996 Carnegie Mellon University
                         All Rights Reserved

Permission  to  use, copy, modify and distribute this software and its
documentation is hereby granted,  provided  that  both  the  copyright
notice  and  this  permission  notice  appear  in  all  copies  of the
software, derivative works or  modified  versions,  and  any  portions
thereof, and that both notices appear in supporting documentation, and
that credit is given to Carnegie Mellon University  in  all  documents
and publicity pertaining to direct or indirect use of this code or its
derivatives.

CODA IS AN EXPERIMENTAL SOFTWARE SYSTEM AND IS  KNOWN  TO  HAVE  BUGS,
SOME  OF  WHICH MAY HAVE SERIOUS CONSEQUENCES.  CARNEGIE MELLON ALLOWS
FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION.   CARNEGIE  MELLON
DISCLAIMS  ANY  LIABILITY  OF  ANY  KIND  FOR  ANY  DAMAGES WHATSOEVER
RESULTING DIRECTLY OR INDIRECTLY FROM THE USE OF THIS SOFTWARE  OR  OF
ANY DERIVATIVE WORK.

Carnegie  Mellon  encourages  users  of  this  software  to return any
improvements or extensions that  they  make,  and  to  grant  Carnegie
Mellon the rights to redistribute these changes without encumbrance.
*/

static char *rcsid = "$Header$";
#endif /*_BLURB_*/


/*

                         IBM COPYRIGHT NOTICE

                          Copyright (C) 1986
             International Business Machines Corporation
                         All Rights Reserved

This  file  contains  some  code identical to or derived from the 1986
version of the Andrew File System ("AFS"), which is owned by  the  IBM
Corporation.    This  code is provded "AS IS" and IBM does not warrant
that it is free of infringement of  any  intellectual  rights  of  any
third  party.    IBM  disclaims  liability of any kind for any damages
whatsoever resulting directly or indirectly from use of this  software
or  of  any  derivative work.  Carnegie Mellon University has obtained
permission to distribute this code, which is based on Version 2 of AFS
and  does  not  contain the features and enhancements that are part of
Version 3 of AFS.  Version 3 of  AFS  is  commercially  available  and
supported by Transarc Corporation, Pittsburgh, PA.

*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include "coda_assert.h"
#include "lwp.h"

char semaphore;

void OtherProcess()
    {
    for(;;)
	{
	LWP_SignalProcess(&semaphore);
	}
    }

int main(int argc, char **argv)
{
    struct timeval t1, t2;
    struct timeval sleeptime;
    PROCESS pid, otherpid;
    register int i,  count, x;
    int j;
    char *waitarray[2];
    static char c[] = "OtherProcess";
    
    count = atoi(argv[1]);


    cont_sw_threshold.tv_sec = 0;
    cont_sw_threshold.tv_usec = 10000;
    last_context_switch.tv_sec = 0;
    last_context_switch.tv_usec = 0;

#ifdef	__linux__
    CODA_ASSERT(LWP_Init(LWP_VERSION, 0, (PROCESS *)&pid) == LWP_SUCCESS);
#else
    CODA_ASSERT(LWP_Init(LWP_VERSION, 0, &pid) == LWP_SUCCESS);
#endif
    CODA_ASSERT(LWP_CreateProcess((PFI)OtherProcess,4096,0, 0, c, (PROCESS *)&otherpid) == LWP_SUCCESS);
    CODA_ASSERT(IOMGR_Initialize() == LWP_SUCCESS);
    waitarray[0] = &semaphore;
    waitarray[1] = 0;
    gettimeofday(&t1, NULL);
    for (i = 0; i < count; i++)
	{
	for (j = 0; j < 100000; j++);
	sleeptime.tv_sec = i;
	sleeptime.tv_usec = 0;
	IOMGR_Select(0, NULL, NULL, NULL, &sleeptime);
	LWP_MwaitProcess(1, waitarray);
	}
    gettimeofday(&t2, NULL);

    x = (t2.tv_sec -t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec);
    printf("%d milliseconds for %d MWaits (%f usec per Mwait and Signal)\n", x/1000, count, (float)(x/count));

    return 0;

}
