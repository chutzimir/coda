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
   Toy Venus server

   Toy Venus allows one file to be open per connection, and provides
   the operations Open, Read, Write, Seek, and Close.

   It does not cache the file.

   Walter Smith
   26 October 1987
 */

#include <stdio.h>
#include "coda_assert.h"
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <lwp.h>
#include <rpc2.h>
#include "tv.h"

extern int errno;		/* where does this really come from? */

/* Per-connection information */

typedef struct {
    FILE *curfp;		/* file currently open (NULL if none)  */
} ConnInfo;


int TVLWP();			/* LWP body */


main()
{
    char buf[128];
    PROCESS mypid;

    printf("Debug Level? [0] ");
    gets(buf);
    RPC2_DebugLevel = atoi(buf);

    InitRPC();
    Fail_Initialize("tvserver", 0);
    Fcon_Init();
    LWP_CreateProcess((PFIC) TVLWP, 4096, LWP_NORMAL_PRIORITY, "TVLWP", NULL, &mypid);
    LWP_WaitProcess((char *)main);
}

TVLWP(p)
char *p;
{
    RPC2_RequestFilter reqfilter;
    RPC2_PacketBuffer *reqbuffer;
    RPC2_Handle cid;
    int err;
    char *pp;

    /* Accept requests on new or existing connections */
    reqfilter.FromWhom = ONESUBSYS;
    reqfilter.OldOrNew = OLDORNEW;
    reqfilter.ConnOrSubsys.SubsysId = TVSUBSYSID;

    while (1) {
	cid = 0;
	if ((err = RPC2_GetRequest(&reqfilter, &cid, &reqbuffer, NULL,
				   NULL, NULL, NULL)) < RPC2_WLIMIT)
	    HandleRPCError(err, cid);
	if ((err = tv_ExecuteRequest(cid, reqbuffer)) < RPC2_WLIMIT)
	    HandleRPCError(err, cid);
	pp = NULL;
	if (RPC2_GetPrivatePointer(cid, &pp) != RPC2_SUCCESS || pp == NULL)
	    RPC2_Unbind(cid);
    }
}

/* Bodies of TV routines */

long TV_NewConn(cid, seType, secLevel, encType, cIdent)
RPC2_Handle cid;
RPC2_Integer seType, secLevel, encType;
RPC2_CountedBS *cIdent;
{
    ConnInfo *ci;
    
    printf("TV_NewConn()\n"); fflush(stdout);

    ci = (ConnInfo *) malloc(sizeof(ConnInfo));
    RPC2_SetPrivatePointer(cid, ci);
    ci->curfp = NULL;
}

long TV_Open(cid, fileName, mode)
unsigned char *fileName;
unsigned char *mode;
{
    ConnInfo *ci;

    CODA_ASSERT(RPC2_GetPrivatePointer(cid, &ci) == RPC2_SUCCESS);
    ci->curfp = fopen(fileName, mode);
    if (ci->curfp == NULL) return errno;
    else return TV_SUCCESS;
}

long TV_Close(cid)
{
    ConnInfo *ci;

    CODA_ASSERT(RPC2_GetPrivatePointer(cid, &ci) == RPC2_SUCCESS);
    if (ci->curfp == NULL) return TV_FAILURE;
    if (fclose(ci->curfp) == EOF) return TV_FAILURE;
    return TV_SUCCESS;
}

long TV_Read(cid, buffer)
RPC2_CountedBS *buffer;
{
    ConnInfo *ci;

    CODA_ASSERT(RPC2_GetPrivatePointer(cid, &ci) == RPC2_SUCCESS);
    if (ci->curfp == NULL) return TV_FAILURE;
    if (fread(buffer->SeqBody, 1, buffer->SeqLen, ci->curfp) == 0)
	return TV_FAILURE;
    return TV_SUCCESS;
}

long TV_Write(cid, buffer)
RPC2_CountedBS *buffer;
{
    ConnInfo *ci;

    CODA_ASSERT(RPC2_GetPrivatePointer(cid, &ci) == RPC2_SUCCESS);
    if (ci->curfp == NULL) return TV_FAILURE;
    if (fwrite(buffer->SeqBody, buffer->SeqLen, 1, ci->curfp) == 0)
    { perror("fwrite failed");
	return errno; }
    return TV_SUCCESS;
}

long TV_Seek(cid, where)
unsigned long where;
{
    ConnInfo *ci;

    CODA_ASSERT(RPC2_GetPrivatePointer(cid, &ci) == RPC2_SUCCESS);
    if (ci->curfp == NULL) return TV_FAILURE;
    if (fseek(ci->curfp, where, 0) == -1) return errno;
    return TV_SUCCESS;
}


/* RPC Stuff */

InitRPC()
{
    PROCESS mylpid;
    int rc;
    RPC2_PortalIdent portalid, *portallist[1];
    RPC2_SubsysIdent subsysid;
    struct timeval tout;

    CODA_ASSERT(LWP_Init(LWP_VERSION, LWP_NORMAL_PRIORITY, &mylpid) == LWP_SUCCESS);

    portalid.Tag = RPC2_PORTALBYINETNUMBER;
    portalid.Value.InetPortNumber = htons(TVPORTAL);
    portallist[0] = &portalid;
    rc = RPC2_Init(RPC2_VERSION, 0, portallist, 1, -1, NULL);
    if (rc != RPC2_SUCCESS) {
	printf("RPC2_Init() --> %s\n", RPC2_ErrorMsg(rc));
	if (rc < RPC2_ELIMIT) exit(-1);
    }
    subsysid.Tag = RPC2_SUBSYSBYID;
    subsysid.Value.SubsysId = TVSUBSYSID;
    CODA_ASSERT(RPC2_Export(&subsysid) == RPC2_SUCCESS);
}


HandleRPCError(rCode, connId)
int rCode;
RPC2_Handle connId;
{
    fprintf(stderr, "tvserver: %s\n", RPC2_ErrorMsg(rCode));
    if (rCode < RPC2_FLIMIT && connId != 0) RPC2_Unbind(connId);
}

iopen(int dummy1, int dummy2, int dummy3) {/* fake ITC system call */} 

