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


#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <math.h>
#ifdef	__NetBSD__
/* this is so 
	stest.c:177: warning: passing arg 1 of `LWP_CreateProcess'
		from incompatible pointer type
	stest.c:177: warning: passing arg 4 of `LWP_CreateProcess'
		makes pointer from integer without a cast
 * does not happen
 */
#undef	C_ARGS
#define	C_ARGS(arglist)	()
#endif
#include "lwp.h"
#include "timer.h"
#include "rpc2.h"
#include "se.h"
#include "sftp.h"
#include "rpc2.private.h"
#include "test.h"


#ifdef	__linux__
#define RPC2_DEBUG
#endif

extern etext();
extern long RPC2_Perror;
extern long RPC2_DebugLevel;
extern long SFTP_DebugLevel;
extern long SFTP_RetryInterval;

PRIVATE char ShortText[200];
PRIVATE char LongText[3000];

long FindKey();	/* To obtain keys from ClientIdent */
long NoteAuthFailure();	/* To note authentication failures */
PRIVATE void PrintHostIdent(), PrintPortalIdent();

#define DEFAULTLWPS	1	/* The default number of LWPs */
#define MAXLWPS		32	/* The maximum number of LWPs */
int numLWPs = 0;		/* Number of LWPs created */
int availableLWPs = 0;		/* Number of LWPs serving requests */
int maxLWPs = MAXLWPS;		/* Max number of LWPs to create */
PROCESS pids[MAXLWPS] = { NULL }; /* Pid of each LWP */
void HandleRequests();		/* Routine to serve requests */

long VerboseFlag;
RPC2_PortalIdent ThisPortal;

long VMMaxFileSize; /* length of VMFileBuf, initially 0 */
long VMCurrFileSize; /* number of useful bytes in VMFileBuf */
char *VMFileBuf;    /* for FILEINVM transfers */

main(argc, argv)
    long argc;
    char *argv[];
{
    SFTP_Initializer sftpi;

    LWP_Init(LWP_VERSION, LWP_MAX_PRIORITY-1, &pids[0]);

#ifdef PROFILE
    InitProfiling();
#endif PROFILE

    FillStrings();

    SFTP_SetDefaults(&sftpi);
    sftpi.WindowSize = 32;
    sftpi.SendAhead = 8;
    sftpi.AckPoint = 8;
    GetParms(argc, argv, &sftpi);
    SFTP_Activate(&sftpi);
    SFTP_EnforceQuota = 1;
    InitRPC();

    if ((maxLWPs < 1) ||
	(maxLWPs > MAXLWPS)) {
	printf("Bad max number of LWPs (%d), must be between 1 and %d\n",
	       maxLWPs, MAXLWPS);
	exit(-1);
    }
    HandleRequests(numLWPs++);
}

/*
 * Routine to server requests.
 */
void HandleRequests(lwp)
    int lwp;
{
    RPC2_PacketBuffer *InBuff, *OutBuff;
#if REQFILTER
    RPC2_RequestFilter reqfilter;
#endif
    RPC2_Handle cid;
    long i;

#if REQFILTER
    reqfilter.FromWhom = ANY;
    reqfilter.OldOrNew = OLDORNEW;
#endif
    availableLWPs++;
    while (TRUE) {
	/*
	 * Get a request
	 */
	if ((i = RPC2_GetRequest(
#if REQFILTER
				 &reqfilter,
#else
				 (RPC2_RequestFilter *)NULL,
#endif
				 &cid, &InBuff, (struct timeval *)NULL, 
				 FindKey, (long)RPC2_XOR,
				 NoteAuthFailure)) != RPC2_SUCCESS) {
	    (void) WhatHappened(i, "GetRequest");
	    exit(-1);
	}

	/*
	 * Decrement number of available LWPs.  If count reaches zero and we
	 * haven't reached the max number, create a new one.
	 */
	if ((--availableLWPs <= 0) &&
	    (numLWPs < maxLWPs)) {
#if	__GNUC__ >= 2
	    i = LWP_CreateProcess((PFIC)HandleRequests, 8192, LWP_NORMAL_PRIORITY,
			      (char *)numLWPs, "server", &pids[numLWPs]);
/* ??? */
#else
	    i = LWP_CreateProcess(HandleRequests, 8192, LWP_NORMAL_PRIORITY,
			      numLWPs, "server", &pids[numLWPs]);
#endif
	    assert(i == LWP_SUCCESS);
	    printf("New LWP %d (%d)\n", numLWPs, pids[numLWPs]);
	    numLWPs++;
	}
	assert(RPC2_AllocBuffer(RPC2_MAXPACKETSIZE-500, &OutBuff) == RPC2_SUCCESS);
	                          /* 500 is a fudge factor */
	(void) ProcessPacket(cid, InBuff, OutBuff);
	availableLWPs++;

	if (InBuff != NULL) (void) RPC2_FreeBuffer(&InBuff);
	if (OutBuff != NULL) (void) RPC2_FreeBuffer(&OutBuff);
    }
}

long FindKey(IN ClientIdent, OUT IdentKey, OUT SessionKey)
    RPC2_CountedBS *ClientIdent;
    RPC2_EncryptionKey IdentKey;
    RPC2_EncryptionKey SessionKey;
    {
    long x;
    say(0, RPC2_DebugLevel, ("*** In FindKey('%s', 0x%lx, 0x%lx) ***\n",
		(char *)ClientIdent->SeqBody, (long)IdentKey, (long)SessionKey));
    x = -1;
    if (strcmp((char *)ClientIdent->SeqBody, "satya") == 0) x =1;
    if (strcmp((char *)ClientIdent->SeqBody, "bovik") == 0) x = 2;
    if (strcmp((char *)ClientIdent->SeqBody, "guest") == 0) x = 3;    
    switch((int) x)
	{
	case 1:
	    (void) strcpy((char *)IdentKey, "bananas");
	    (void) strcpy((char *)SessionKey, "BANANAS");
	    break;
	    
	case 2:
	    (void) strcpy((char *)IdentKey, "harryqb");
	    (void) strcpy((char *)SessionKey, "HARRYQB");
	    break;
	    
	case 3:
	    (void) strcpy((char *)IdentKey, "unknown");
	    (void) strcpy((char *)SessionKey, "UNKNOWN");
	    break;
	    
	default:
	    return(-1);
	}
    return(0);
    }


iopen()
    {
    assert(1 == 0);
    }


long NoteAuthFailure(cIdent, eType, pHost, pPortal)
    RPC2_CountedBS *cIdent;
    RPC2_Integer eType;
    RPC2_HostIdent *pHost;
    RPC2_PortalIdent *pPortal;
    {
    printf("Authentication using e-type %ld failed for %s from\n\t", eType, 
	   (char *)cIdent->SeqBody);
    PrintHostIdent(pHost, (FILE *)NULL); printf("\t");
    PrintPortalIdent(pPortal, (FILE *)NULL); printf("\n");
    return(RPC2_SUCCESS);
    }


PRIVATE void PrintHostIdent(hPtr, tFile)
    register RPC2_HostIdent *hPtr;
    FILE *tFile;
    {
    if (tFile == NULL) tFile = stdout;	/* it's ok, call-by-value */
    switch (hPtr->Tag)
	{
	case RPC2_HOSTBYINETADDR:
		{
		register long a = ntohl(hPtr->Value.InetAddress);
		fprintf(tFile, "Host.InetAddress = %lu.%ld.%ld.%ld",
		    ((unsigned long)(a & 0xff000000))>>24, (a & 0x00ff0000)>>16, (a & 0x0000ff00)>>8, a & 0x000000ff);
		break;	
		}
	
	case RPC2_HOSTBYNAME:
		fprintf(tFile, "Host.Name = \"%s\"", hPtr->Value.Name);
		break;
	
	default:	fprintf(tFile, "Host = ??????\n");
	}

    (void) fflush(tFile);
    }

PRIVATE void PrintPortalIdent(pPtr, tFile)
    register RPC2_PortalIdent *pPtr;
    FILE *tFile;
    {
    if (tFile == NULL) tFile = stdout;	/* it's ok, call-by-value */
    switch (pPtr->Tag)
	{
	case RPC2_PORTALBYINETNUMBER:
		fprintf(tFile, "Portal.InetPortNumber = %u", (unsigned) ntohs(pPtr->Value.InetPortNumber));
		break;	
	
	case RPC2_PORTALBYNAME:
		fprintf(tFile, "Portal.Name = \"%s\"", pPtr->Value.Name);
		break;
	
	default:	fprintf(tFile, "Portal = ??????");
	}


    (void) fflush(tFile);
    }



WhatHappened(X, Y)
    long X;
    char *Y;
    {
    if(VerboseFlag ||  X) printf("%s: %s (%ld)\n", Y, RPC2_ErrorMsg(X), X);
    return(X);
    }


ProcessPacket(cIn, pIn, pOut)
    RPC2_Handle cIn;
    RPC2_PacketBuffer *pIn, *pOut;
    {
    int *iptr;
    long i, opcode, replylen;
    char *cptr;
    SE_Descriptor sed;
    RPC2_NewConnectionBody *newconnbody;
#ifdef OLDLWP
    int smax, sused;
#endif OLDLWP

    opcode = pIn->Header.Opcode;
    switch((int) opcode)
	{
	case REMOTESTATS:
	    PrintStats();
	    pOut->Header.ReturnCode = RPC2_SUCCESS;
	    pOut->Header.BodyLength = 0;
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
	    break;

	case BEGINREMOTEPROFILING:
	    pOut->Header.ReturnCode = RPC2_SUCCESS;
	    pOut->Header.BodyLength = 0;
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
#ifdef PROFILE
	    ProfilingOn();
#endif PROFILE
	    break;


	case ENDREMOTEPROFILING:
#ifdef PROFILE
	    ProfilingOff();
	    DoneProfiling();
#endif PROFILE

	    pOut->Header.ReturnCode = RPC2_SUCCESS;
	    pOut->Header.BodyLength = 0;
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
#ifdef OLDLWP
	    LWP_StackUsed(rpc2_SocketListenerPID, &smax, &sused);
	    printf("SL stack used: %d of %d\n", sused, smax);
#endif OLDLWP
	    printf("\tCreation:    Spkts = %ld  Mpkts = %ld  Lpkts = %ld  SLEs = %ld  Conns = %ld\n",
		rpc2_PBSmallCreationCount, rpc2_PBMediumCreationCount, 
		rpc2_PBLargeCreationCount, rpc2_SLCreationCount, rpc2_ConnCreationCount);
	    printf("\nFree:    Spkts = %ld  Mpkts = %ld  Lpkts = %ld  SLEs = %ld  Conns = %ld\n",
		rpc2_PBSmallFreeCount, rpc2_PBMediumFreeCount, 
		rpc2_PBLargeFreeCount, rpc2_SLFreeCount, rpc2_ConnFreeCount);


	    break;

	case FETCHFILE:
	case STOREFILE:
	    sed.Tag = SMARTFTP;
	    sed.Value.SmartFTPD.Tag = FILEBYNAME;
	    sed.Value.SmartFTPD.SeekOffset = 0;
	    sed.Value.SmartFTPD.FileInfo.ByName.ProtectionBits = 0644;

	    if (opcode == (long) STOREFILE) sed.Value.SmartFTPD.TransmissionDirection = CLIENTTOSERVER;
	    else sed.Value.SmartFTPD.TransmissionDirection = SERVERTOCLIENT;
	    replylen = ntohl(*((unsigned long *)(pIn->Body)));
	    sed.Value.SmartFTPD.SeekOffset = ntohl(*((unsigned long *)(pIn->Body + sizeof(long))));
	    printf("SeekOffset = %ld\n", sed.Value.SmartFTPD.SeekOffset);
	    sed.Value.SmartFTPD.ByteQuota = ntohl(*((unsigned long *)(pIn->Body + 2*sizeof(long))));
	    printf("ByteQuota = %ld\n", sed.Value.SmartFTPD.ByteQuota);
	    sed.Value.SmartFTPD.hashmark = *(pIn->Body+3*sizeof(long));
	    (void) strcpy((char *)sed.Value.SmartFTPD.FileInfo.ByName.LocalFileName, (char *)pIn->Body+1+3*sizeof(long));

	    if (strcmp(sed.Value.SmartFTPD.FileInfo.ByName.LocalFileName, "-") == 0)
		{
		sed.Value.SmartFTPD.Tag = FILEBYFD;
		sed.Value.SmartFTPD.FileInfo.ByFD.fd = (opcode == FETCHFILE) ? fileno(stdin) : 
								fileno(stdout);
		}
	    else
		{
		if (strcmp(sed.Value.SmartFTPD.FileInfo.ByName.LocalFileName, "/dev/mem") == 0)
		    {/* Has to be set each time: other modes may clobber  fields */
		    sed.Value.SmartFTPD.Tag = FILEINVM;
		    sed.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqBody = (RPC2_ByteSeq)VMFileBuf;
		    sed.Value.SmartFTPD.FileInfo.ByAddr.vmfile.MaxSeqLen = VMMaxFileSize;
		    sed.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqLen = VMCurrFileSize;
		    }
		}

	    if (VerboseFlag) SFTP_PrintSED(&sed, stdout);

	    if ((i = RPC2_InitSideEffect(cIn, &sed)) == RPC2_SUCCESS)
		{
		if ((i = RPC2_CheckSideEffect(cIn, &sed, (long)SE_AWAITLOCALSTATUS)) != RPC2_SUCCESS)
		    {
		    printf("RPC2_CheckSideEffect()--> %s\n", RPC2_ErrorMsg(i));(void) fflush(stdout);
		    /* if (i < RPC2_FLIMIT) break; */	/* switch */
		    }
		}
	    else
		{
		printf("RPC2_InitSideEffect()--> %s\n", RPC2_ErrorMsg(i));(void) fflush(stdout);
		/* if (i < RPC2_FLIMIT)break;	*/ /* switch */
		}

	    if (sed.LocalStatus != SE_SUCCESS)
		{
		printf("sed.LocalStatus = %s\n", SE_ErrorMsg((long)sed.LocalStatus));
		}
	    pOut->Header.ReturnCode = (int)sed.LocalStatus;
	    if ((opcode == (long) STOREFILE) &&
		   (sed.Value.SmartFTPD.Tag == FILEINVM))
	        {
		VMCurrFileSize = sed.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqLen;
		printf("VMCurrFileSize = %ld\n", VMCurrFileSize);
	        }

	    if (VerboseFlag) SFTP_PrintSED(&sed, stdout);

	    pOut->Header.BodyLength = replylen; /* should ensure not too long */
	    bzero((char *)pOut->Body, (int) replylen);
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
	break;
	
	case ONEPING: /* reply size = request size */
	    pOut->Header.ReturnCode = RPC2_SUCCESS;
	    pOut->Header.BodyLength = pIn->Header.BodyLength;
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
	    break;

	case MANYPINGS: 
	    pOut->Header.ReturnCode = RPC2_SUCCESS;
	    pOut->Header.BodyLength = pIn->Header.BodyLength;
	    i = RPC2_SendResponse(cIn, pOut);
	    if (i != RPC2_SUCCESS) (void) WhatHappened(i, "SendResponse");
	    break;

	
	case LENGTHTEST:
	    iptr = (int *)pIn->Body;
	    *iptr = (int) htonl((unsigned long) *iptr);
	    printf("Length: %d\n", *iptr);(void) fflush(stdout);
	    cptr = (char *)iptr; 
	    cptr += sizeof(RPC2_Integer);		
	    pOut->Body[0] = '\0';
	    (void) strncat((char *)pOut->Body, cptr, *iptr);
	    pOut->Header.ReturnCode = RPC2_SUCCESS;
	    pOut->Header.BodyLength = *iptr;
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
	break;


	case SETREMOTEVMFILESIZE:
	    {
	    iptr = (int *)pIn->Body;
	    VMMaxFileSize = (int) ntohl((unsigned long)*iptr);
	    printf("New VM file buffer size = %ld\n ", VMMaxFileSize);
	    if (VMFileBuf) free(VMFileBuf);
	    assert(VMFileBuf = (char *)malloc((unsigned)VMMaxFileSize));
	    pOut->Header.ReturnCode = RPC2_SUCCESS;
	    pOut->Header.BodyLength = 0;
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
	    break;
	    }


	case RPC2_NEWCONNECTION: /* new connection */
	    newconnbody = (RPC2_NewConnectionBody *)pIn->Body;
	    printf("New connection 0x%lx:   SideEffectType = %lu  SecurityLevel = %lu  ClientIdent = \"%s\"\n",
		   cIn, ntohl((unsigned long)newconnbody->SideEffectType),  
		   ntohl((unsigned long)newconnbody->SecurityLevel), 
		   (char *)&newconnbody->ClientIdent.SeqBody);
	    i = RPC2_SUCCESS;
	    (void) RPC2_Enable(cIn);
	    break;
	    

	default: /* unknown opcode */
	    pOut->Header.ReturnCode = RPC2_FAIL;
	    pOut->Header.BodyLength = 1 + strlen("Get your act together");
	    (void) strcpy((char *)pOut->Body, "Get your act together");
	    i = WhatHappened(RPC2_SendResponse(cIn, pOut), "SendResponse");
	break;
	}

#ifdef RPC2DEBUG
	if (i != RPC2_SUCCESS)  sftp_DumpTrace("stest.dump");
#endif RPC2DEBUG
    return(i);
    }


GetParms(argc, argv, sftpI)
    long argc;
    char *argv[];
    SFTP_Initializer *sftpI;
    {
    register int i;
    for (i = 1; i < argc; i++)
	{
	if (strcmp(argv[i], "-x") == 0 && i < argc -1)
	    {RPC2_DebugLevel = atoi(argv[++i]); continue;}
	if (strcmp(argv[i], "-sx") == 0 && i < argc -1)
	    {SFTP_DebugLevel = atoi(argv[++i]); continue;}
	if (strcmp(argv[i], "-l") == 0 && i < argc -1)
	    {maxLWPs = atoi(argv[++i]); continue;}
	if (strcmp(argv[i], "-v") == 0)
	    {VerboseFlag = 1; continue;}
	if (strcmp(argv[i], "-p") == 0 && i < argc - 1)
	    {
	    ThisPortal.Value.InetPortNumber = atoi(argv[++i]); 
	    ThisPortal.Value.InetPortNumber = htons(ThisPortal.Value.InetPortNumber);
	    continue;
	    }
    

	printf("Usage: stest [-x debuglevel] [-sx sftpdebuglevel]  [-l maxlwps] [-v verboseflag] [-p portal]\n");
	exit(-1);
	}    
	        
    }


FillStrings()
    {
    register int i, j;
    for (i = 'a'; i < 'z'+1; i++)
	{
	j = 6*(i-'a');
	ShortText[j] = ShortText[j+1] = ShortText[j+2] = i;
	ShortText[j+3] = ShortText[j+4] = ShortText[j+5] = i;
	}
    LongText[0]=0;
    for (i=0; i < 10; i++)
	(void) strcat(LongText, ShortText);
    }


InitRPC()
    {
    RPC2_PortalIdent *portallist[1], **pp;
    RPC2_SubsysIdent subsysid;

    ThisPortal.Tag = RPC2_PORTALBYINETNUMBER;
    portallist[0] = &ThisPortal;
    pp = (ThisPortal.Value.InetPortNumber == 0) ? NULL : portallist;

    if (WhatHappened(RPC2_Init(RPC2_VERSION, (RPC2_Options *)NULL, pp, (long)1,
		      (long) 6, (struct timeval *)NULL), "Init") != RPC2_SUCCESS)
	exit(-1);
    PrintHostIdent(&rpc2_LocalHost, (FILE *)NULL);
    printf("    ");
    PrintPortalIdent(&rpc2_LocalPortal, (FILE *)NULL);
    printf("\n\n");
    subsysid.Tag = RPC2_SUBSYSBYNAME;
    (void) strcpy(subsysid.Value.Name, "Vice2-FileServer");
    (void) RPC2_Export(&subsysid);
    }


PrintStats()
    {
    printf("RPC2:\n");
    printf("Packets Sent = %lu\tPacket Retries = %lu (of %lu)\tPackets Received = %lu\n",
	   rpc2_Sent.Total, rpc2_Sent.Retries, 
	   rpc2_Sent.Retries + rpc2_Sent.Cancelled, rpc2_Recvd.Total);
    printf("Bytes sent = %lu\tBytes received = %lu\n", rpc2_Sent.Bytes, rpc2_Recvd.Bytes);
    printf("Received Packet Distribution:\n");
    printf("\tRequests = %lu\tGoodRequests = %lu\n",
	   rpc2_Recvd.Requests, rpc2_Recvd.GoodRequests);
    printf("\tReplies = %lu\tGoodReplies = %lu\n",
	   rpc2_Recvd.Replies, rpc2_Recvd.GoodReplies);
    printf("\tBusies = %lu\tGoodBusies = %lu\n",
	    rpc2_Recvd.Busies, rpc2_Recvd.GoodBusies);
    printf("SFTP:\n");
    printf("Packets Sent = %lu\t\tStarts Sent = %lu\t\tDatas Sent = %lu\n",
	   sftp_Sent.Total, sftp_Sent.Starts, sftp_Sent.Datas);
    printf("Data Retries Sent = %lu\t\tAcks Sent = %lu\t\tNaks Sent = %lu\n",
	   sftp_Sent.DataRetries, sftp_Sent.Acks, sftp_Sent.Naks);
    printf("Busies Sent = %lu\t\t\tBytes Sent = %lu\n",
	   sftp_Sent.Busies, sftp_Sent.Bytes);
    printf("Packets Received = %lu\t\tStarts Received = %lu\tDatas Received = %lu\n",
	   sftp_Recvd.Total, sftp_Recvd.Starts, sftp_Recvd.Datas);
    printf("Data Retries Received = %lu\tAcks Received = %lu\tNaks Received = %lu\n",
	   sftp_Recvd.DataRetries, sftp_Recvd.Acks, sftp_Recvd.Naks);
    printf("Busies Received = %lu\t\tBytes Received = %lu\n",
	   sftp_Recvd.Busies, sftp_Recvd.Bytes);
    (void) fflush(stdout);
    }
