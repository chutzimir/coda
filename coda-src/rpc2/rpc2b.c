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
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/file.h>
#include "lwp.h"
#include "timer.h"
#include "preempt.h"
#include "rpc2.h"
#include "rpc2.private.h"
#include "se.h"
#include "trace.h"
#include "cbuf.h"



#define NETWORKS 8
static unsigned int get_netaddr(long sock);
extern int errno;


long RPC2_Init(
	       char *VId,		/* magic version string */
	       RPC2_Options *Options,
	       RPC2_PortIdent *Port,	/* array of portal ids */
	       long RetryCount,	   /* max number of retries before breaking conn*/
	       struct timeval *KAInterval	/* for keeping long RPC requests alive  */
	       )
{
    char *c;
    long rc, i, ctpid;

    rpc2_logfile = stderr;
    rpc2_tracefile = stderr;


    rpc2_Enter();
    say(0, RPC2_DebugLevel, "RPC2_Init()\n");
    say(999, RPC2_DebugLevel, "Runtime system version: \"%s\"\n", RPC2_VERSION);

    if (strcmp(VId, RPC2_VERSION) != 0) 
	    rpc2_Quit (RPC2_WRONGVERSION);



    (void) FT_GetTimeOfDay(&rpc2_InitTime, (struct timezone *)0);

    rpc2_InitConn();
    rpc2_InitMgrp();
    rpc2_InitHost();

    if (Port) 
	    rpc2_LocalPort = *Port; 
    else 
	    rpc2_LocalPort.Tag = (PortTag)0;  
    
    rc = rpc2_CreateIPSocket(&rpc2_RequestSocket, &rpc2_LocalHost,
	    	&rpc2_LocalPort);

    if ( Port ) 
	    *Port = rpc2_LocalPort; 

    if (rc < RPC2_ELIMIT) 
	    rpc2_Quit(rc);

    /* Initialize retry parameters */
    if (rpc2_InitRetry(RetryCount, KAInterval) != 0)     
	    rpc2_Quit(RPC2_FAIL);

    /* Initialize random number generation for sequence numbers */
    rpc2_InitRandom();

    IOMGR_Initialize();
    TM_Init(&rpc2_TimerQueue);
    /* Call side effect initialization routines */
    for (i = 0; i < SE_DefCount; i++)
	if (SE_DefSpecs[i].SE_Init != NULL)
	    if ((*SE_DefSpecs[i].SE_Init)() < RPC2_ELIMIT) 
		rpc2_Quit(RPC2_SEFAIL2);

    c = "SocketListener";
    LWP_CreateProcess((PFIC)rpc2_SocketListener, 16384, 
		      LWP_NORMAL_PRIORITY, NULL,
		      c, &rpc2_SocketListenerPID);

#ifdef RPC2DEBUG
    c = "ClockTick";
    LWP_CreateProcess((PFIC)rpc2_ClockTick, 16384, LWP_NORMAL_PRIORITY, NULL, c, (PROCESS *)&ctpid);
#endif RPC2DEBUG 

    rpc2_Quit(rc);
}



long RPC2_Export(IN Subsys)
    RPC2_SubsysIdent *Subsys;
    {
    long i, myid;
    struct SubsysEntry *sp;

    rpc2_Enter();
    say(0, RPC2_DebugLevel, "RPC2_Export()\n");

    switch(Subsys->Tag)
	{
	case RPC2_SUBSYSBYID:
	    myid = Subsys->Value.SubsysId;
	    break;
	
	case RPC2_SUBSYSBYNAME:
		say(0, RPC2_DebugLevel, "RPC2_Export: obsolete SUBSYSBYNAME used!\n");
		CODA_ASSERT(0);
	    break;

	default: rpc2_Quit(RPC2_FAIL);
	}
    
    /* Verify this subsystem not already exported */
    for (i = 0, sp = rpc2_SSList; i < rpc2_SSCount; i++, sp = sp->Next)
	if (sp->Id == myid) rpc2_Quit(RPC2_DUPLICATESERVER);

    /* Mark this subsystem as exported */
    sp = rpc2_AllocSubsys();
    sp->Id = myid;
    rpc2_Quit(RPC2_SUCCESS);    
    }


long RPC2_DeExport(IN Subsys)
    RPC2_SubsysIdent *Subsys;
    {
    long i, myid;
    struct SubsysEntry *sp;

    rpc2_Enter();
    say(0, RPC2_DebugLevel, "RPC2_DeExport()\n");

    if (Subsys == NULL)
	{/* Terminate all subsystems */
	rpc2_SSList = NULL;	/* possible core leak */
	rpc2_SSCount = 0;
	rpc2_Quit(RPC2_SUCCESS);	
	}
	
    /* Else terminate a specific subsystem */
    switch(Subsys->Tag)
	{
	case RPC2_SUBSYSBYID:
	    myid = Subsys->Value.SubsysId;
	    break;
	
	case RPC2_SUBSYSBYNAME:
		say(0, RPC2_DebugLevel, "RPC2_Export: obsolete SUBSYSBYNAME used!\n");
		CODA_ASSERT(0);

	    break;

	default: rpc2_Quit(RPC2_BADSERVER);
	}
    
    /* Verify this subsystem is indeed exported */
    for (i = 0, sp = rpc2_SSList; i < rpc2_SSCount; i++, sp = sp->Next)
	if (sp->Id == myid) break;
    if (i >= rpc2_SSCount) rpc2_Quit(RPC2_BADSERVER);

    rpc2_FreeSubsys(&sp);
    rpc2_Quit(RPC2_SUCCESS);    
    }


static RPC2_PacketBuffer *Gimme(long size, RPC2_PacketBuffer **flist, 
				long *count, long *creacount)
{
	RPC2_PacketBuffer *pb;
	
	if (*flist== NULL)	{
		rpc2_Replenish(flist, count, size, creacount, OBJ_PACKETBUFFER);
		CODA_ASSERT(*flist);
		(*flist)->Prefix.BufferSize = size;
	}
	pb = (RPC2_PacketBuffer *) rpc2_MoveEntry(flist, &rpc2_PBList, NULL, 
						  count, &rpc2_PBCount);
	CODA_ASSERT(pb->Prefix.Qname == &rpc2_PBList);
	return(pb);
}

static RPC2_PacketBuffer *GetPacket(psize)
    long psize;
    {
    if (psize <= SMALLPACKET)
	{
	return(Gimme(SMALLPACKET, &rpc2_PBSmallFreeList, 
		&rpc2_PBSmallFreeCount, &rpc2_PBSmallCreationCount));
	}
    if (psize <= MEDIUMPACKET)
	{
	return(Gimme(MEDIUMPACKET, &rpc2_PBMediumFreeList, 
	    &rpc2_PBMediumFreeCount, &rpc2_PBMediumCreationCount));
	}
    if (psize  <= LARGEPACKET)
	{
	return(Gimme(LARGEPACKET, &rpc2_PBLargeFreeList,
		&rpc2_PBLargeFreeCount, &rpc2_PBLargeCreationCount));
	}
    return(NULL);
    }


/* Allocates a packet buffer whose body is at least MinBodySize bytes,
   and sets BuffPtr to point to it.  Returns RPC2_SUCCESS on success.
   Sets BodyLength field of allocated packet to MinBodySize: you can
   alter this if this is not what you intended */
long rpc2_AllocBuffer(IN long MinBodySize, OUT RPC2_PacketBuffer **BuffPtr, 
		      IN char *File, IN long Line)
{
	long thissize;

	rpc2_Enter();
	thissize = MinBodySize + sizeof(RPC2_PacketBuffer);
	if (thissize > RPC2_MAXPACKETSIZE) 
		return(0);

	*BuffPtr = GetPacket(thissize);
	CODA_ASSERT(*BuffPtr);
	CODA_ASSERT((*BuffPtr)->Prefix.MagicNumber == OBJ_PACKETBUFFER);

	bzero(&(*BuffPtr)->Header, sizeof(struct RPC2_PacketHeader));
	(*BuffPtr)->Header.BodyLength = MinBodySize;

#ifdef RPC2DEBUG
	strncpy((char *)(*BuffPtr)->Prefix.File, File, 12);
	(*BuffPtr)->Prefix.File[2] &= 0xffffff00;
	(*BuffPtr)->Prefix.Line = Line;
#endif RPC2DEBUG

	rpc2_Quit(RPC2_SUCCESS);
}



long RPC2_FreeBuffer(INOUT BuffPtr)
    RPC2_PacketBuffer **BuffPtr;
    {
    RPC2_PacketBuffer **tolist;
    long *tocount;

    rpc2_Enter();
    CODA_ASSERT(BuffPtr != NULL && *BuffPtr != NULL);
    CODA_ASSERT((*BuffPtr)->Prefix.MagicNumber == OBJ_PACKETBUFFER);

    switch((int) (*BuffPtr)->Prefix.BufferSize)
	{
	case SMALLPACKET:
		tolist = &rpc2_PBSmallFreeList;
		tocount = &rpc2_PBSmallFreeCount;
		break;
	
	case MEDIUMPACKET:
		tolist = &rpc2_PBMediumFreeList;
		tocount = &rpc2_PBMediumFreeCount;
		break;

	case LARGEPACKET:
		tolist = &rpc2_PBLargeFreeList;
		tocount = &rpc2_PBLargeFreeCount;
		break;
	
	default:    CODA_ASSERT(FALSE);
	}
    CODA_ASSERT((*BuffPtr)->Prefix.Qname == &rpc2_PBList);
    rpc2_MoveEntry(&rpc2_PBList, tolist, *BuffPtr, &rpc2_PBCount, tocount);
    *BuffPtr = NULL;
    rpc2_Quit(RPC2_SUCCESS);
    }


char *RPC2_ErrorMsg(rc)
     long rc;
    /* Returns a pointer to a static string describing error rc.  Note that this routine
	violates the RPC2 tradition of stuffing an OUT parameter. 
    */
    {
    static char msgbuf[100];

    switch((int) rc)
	{
	case RPC2_SUCCESS:		return("RPC2_SUCCESS");

	case RPC2_OLDVERSION:		return("RPC2_OLDVERSION (W)");
	case RPC2_INVALIDOPCODE:	return("RPC2_INVALIDOPCODE (W)");
	case RPC2_BADDATA:		return("RPC2_BADDATA (W)");
	case RPC2_NOGREEDY:		return("RPC2_NOGREEDY (W)");
	case RPC2_ABANDONED:		return("RPC2_ABANDONED (W)");

	case RPC2_CONNBUSY:		return("RPC2_CONNBUSY (E)");
	case RPC2_SEFAIL1:		return("RPC2_SEFAIL1 (E)");
	case RPC2_TOOLONG:		return("RPC2_TOOLONG (E)");
	case RPC2_NOMGROUP:		return("RPC2_NOMGROUP (E)");
	case RPC2_MGRPBUSY:		return("RPC2_MGRPBUSY (E)");
	case RPC2_NOTGROUPMEMBER:	return("RPC2_NOTGROUPMEMBER (E)");
	case RPC2_DUPLICATEMEMBER:	return("RPC2_DUPLICATEMEMBER (E)");
	case RPC2_BADMGROUP:		return("RPC2_BADMGROUP (E)");

	case RPC2_FAIL:			return("RPC2_FAIL (F)");
	case RPC2_NOCONNECTION:		return("RPC2_NOCONNECTION (F)");
	case RPC2_TIMEOUT:		return("RPC2_TIMEOUT (F)");
	case RPC2_NOBINDING:		return("RPC2_NOBINDING (F)");
	case RPC2_DUPLICATESERVER:	return("RPC2_DUPLICATESERVER (F)");
	case RPC2_NOTWORKER:		return("RPC2_NOTWORKER (F)");
	case RPC2_NOTCLIENT:		return("RPC2_NOTCLIENT (F)");
	case RPC2_WRONGVERSION:		return("RPC2_WRONGVERSION (F)");
	case RPC2_NOTAUTHENTICATED:	return("RPC2_NOTAUTHENTICATED (F)");
	case RPC2_CLOSECONNECTION:	return("RPC2_CLOSECONNECTION (F)");
	case RPC2_BADFILTER:		return("RPC2_BADFILTER (F)");
	case RPC2_LWPNOTINIT:		return("RPC2_LWPNOTINIT (F)");
	case RPC2_BADSERVER:		return("RPC2_BADSERVER (F)");
	case RPC2_SEFAIL2:		return("RPC2_SEFAIL2 (F)");
	case RPC2_SEFAIL3:		return("RPC2_SEFAIL3 (F)");
	case RPC2_SEFAIL4:		return("RPC2_SEFAIL4 (F)");
	case RPC2_DEAD:			return("RPC2_DEAD (F)");	
	case RPC2_NAKED:		return("RPC2_NAKED (F)");	


	default:			(void) sprintf(msgbuf, "Unknown RPC2 return code %ld", rc); return(msgbuf);
	}
    
    }



long RPC2_GetPrivatePointer(IN ConnHandle, OUT PrivatePtr)
    RPC2_Handle ConnHandle;
    char **PrivatePtr;
    {
    struct CEntry *ceaddr;

    rpc2_Enter();

    say(999, RPC2_DebugLevel, "RPC2_GetPrivatePointer()\n");

    ceaddr = rpc2_GetConn(ConnHandle);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);
	
    *PrivatePtr = ceaddr->PrivatePtr;
    rpc2_Quit(RPC2_SUCCESS);
    }


long RPC2_SetPrivatePointer(IN ConnHandle, IN PrivatePtr)
    RPC2_Handle ConnHandle;
    char *PrivatePtr;
    {
    struct CEntry *ceaddr;

    rpc2_Enter();
    say(999, RPC2_DebugLevel, "RPC2_SetPrivatePointer()\n");


    ceaddr = rpc2_GetConn(ConnHandle);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);
    ceaddr->PrivatePtr = PrivatePtr;
    rpc2_Quit(RPC2_SUCCESS);
    }



long RPC2_GetSEPointer(IN ConnHandle, OUT SEPtr)
    RPC2_Handle ConnHandle;
    struct SFTP_Entry **SEPtr;
    {
    struct CEntry *ceaddr;

    rpc2_Enter();
    say(999, RPC2_DebugLevel, "RPC2_GetSEPointer()\n");

    ceaddr = rpc2_GetConn(ConnHandle);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);
    *SEPtr = (struct SFTP_Entry *)ceaddr->SideEffectPtr;
    rpc2_Quit(RPC2_SUCCESS);    
    }
    

long RPC2_SetSEPointer(IN ConnHandle, IN SEPtr)
    RPC2_Handle ConnHandle;
    struct SFTP_Entry *SEPtr;
    {
    struct CEntry *ceaddr;

    rpc2_Enter();
    say(999, RPC2_DebugLevel, "RPC2_SetSEPointer()\n");


    ceaddr = rpc2_GetConn(ConnHandle);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);
    ceaddr->SideEffectPtr = (char *)SEPtr;
    rpc2_Quit(RPC2_SUCCESS);    
    }


long RPC2_GetPeerInfo(IN ConnHandle, OUT PeerInfo)
    RPC2_Handle ConnHandle;
    RPC2_PeerInfo *PeerInfo;
    {
    struct CEntry *ceaddr;

    rpc2_Enter();
    say(999, RPC2_DebugLevel, "RPC2_GetPeerInfo()\n");

    ceaddr = rpc2_FindCEAddr(ConnHandle);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);

    PeerInfo->RemoteHost = ceaddr->PeerHost;	/* structure assignment */
    PeerInfo->RemotePort = ceaddr->PeerPort;  /* structure assignment */
    PeerInfo->RemoteSubsys.Tag = RPC2_SUBSYSBYID;
    PeerInfo->RemoteSubsys.Value.SubsysId = ceaddr->SubsysId;
    PeerInfo->RemoteHandle = ceaddr->PeerHandle;
    PeerInfo->SecurityLevel = ceaddr->SecurityLevel;
    PeerInfo->EncryptionType = ceaddr->EncryptionType;
    bcopy(ceaddr->SessionKey, PeerInfo->SessionKey, RPC2_KEYSIZE);
    PeerInfo->Uniquefier = ceaddr->PeerUnique;
    rpc2_Quit(RPC2_SUCCESS);
    }


long RPC2_DumpTrace(IN OutFile, IN HowMany)
    FILE *OutFile;
    long HowMany;
    {/* NOTE: not surrounded by rpc2_Enter() and rpc2_Quit() */

#ifdef RPC2DEBUG
    if (OutFile == NULL) OutFile = stdout;	/* it's ok, call-by-value */
    CBUF_WalkBuff(rpc2_TraceBuffHeader, rpc2_PrintTraceElem, HowMany, OutFile);
    (void) fflush(OutFile);
    return(RPC2_SUCCESS);
#else
    return(RPC2_SUCCESS);
#endif RPC2DEBUG
    }


long RPC2_InitTraceBuffer(IN ecount)
    long ecount;
    {/* NOTE: not surrounded by rpc2_Enter() and rpc2_Quit() */

#ifdef RPC2DEBUG
    if (rpc2_TraceBuffHeader) CBUF_Free(&rpc2_TraceBuffHeader);
    rpc2_TraceBuffHeader = CBUF_Init(sizeof(struct TraceElem), ecount, "RPC2 Trace Buffer");
    CODA_ASSERT (rpc2_TraceBuffHeader != NULL);
    return(RPC2_SUCCESS);
#else
    return(RPC2_SUCCESS);
#endif RPC2DEBUG
    }


long RPC2_DumpState(DumpFile, Verbosity)
    FILE *DumpFile;
    long Verbosity;	/* > 0 ==> full dump */
    {/* NOTE: not surrounded by rpc2_Enter() and rpc2_Quit() */

#ifdef RPC2DEBUG
    long when = rpc2_time();
    char where[100];
    
    if (DumpFile == NULL) 
	    DumpFile = stdout;	/* it's ok, call-by-value */
    gethostname(where, sizeof(where));
    fprintf(DumpFile, "\n\n\t\t\tRPC2 Runtime State on %s at %s\n", where, ctime((time_t *)&when));
    fprintf(DumpFile, "rpc2_ConnCreationCount = %ld  rpc2_ConnCount = %ld  rpc2_ConnFreeCount = %ld\n",
    	rpc2_ConnCreationCount, rpc2_ConnCount, rpc2_ConnFreeCount);
    fprintf(DumpFile, "rpc2_PBCount = %ld  rpc2_PBHoldCount = %ld  rpc2_PBFreezeCount = %ld\n", rpc2_PBCount, rpc2_PBHoldCount, rpc2_PBFreezeCount);
    fprintf(DumpFile, "rpc2_PBSmallFreeCount = %ld  rpc2_PBSmallCreationCount = %ld\n", rpc2_PBSmallFreeCount, rpc2_PBSmallCreationCount);
    fprintf(DumpFile, "rpc2_PBMediumFreeCount = %ld  rpc2_PBMediumCreationCount = %ld\n", rpc2_PBMediumFreeCount, rpc2_PBMediumCreationCount);
    fprintf(DumpFile, "rpc2_PBLargeFreeCount = %ld  rpc2_PBLargeCreationCount = %ld\n", rpc2_PBLargeFreeCount, rpc2_PBLargeCreationCount);

    fprintf(DumpFile, "rpc2_SLCreationCount = %ld rpc2_SLFreeCount = %ld  rpc2_ReqCount = %ld  rpc2_SLCount = %ld\n",
    	rpc2_SLCreationCount, rpc2_SLFreeCount, rpc2_SLReqCount, rpc2_SLCount);
    fprintf(DumpFile, "rpc2_SSCreationCount = %ld  rpc2_SSCount = %ld  rpc2_SSFreeCount = %ld\n",
    	rpc2_SSCreationCount, rpc2_SSCount, rpc2_SSFreeCount);
    return(RPC2_SUCCESS);
#else
    return(RPC2_SUCCESS);
#endif RPC2DEBUG
    }


long RPC2_LamportTime()
    /*  Returns the Lamport time for this system.
    	This is at least one greater than the value returned on the preceding call.
	Accepted incoming packets with Lamport times greater than the local Lamport clock cause
	    the local clock to be set to one greater than the incoming packet's time.
	Each non-retry outgoing packet gets a Lamport timestamp via this call.
	NOTE: the Lamport time bears no resemblance to the actual time of day.  We could fix this.
    */
    {
    rpc2_Enter();
    rpc2_LamportClock += 1;
    rpc2_Quit(rpc2_LamportClock);
    }


long RPC2_SetBindLimit(IN bindLimit)
    int bindLimit;
    {
    rpc2_Enter();
    rpc2_BindLimit = bindLimit;
    rpc2_Quit(RPC2_SUCCESS);
    }

long RPC2_Enable(RPC2_Handle whichConn)
{
	struct CEntry *ceaddr;

	say(0, RPC2_DebugLevel, "RPC2_Enable()\n");

	rpc2_Enter();
	ceaddr = rpc2_GetConn(whichConn);
	if (ceaddr == NULL) 
		rpc2_Quit(RPC2_NOCONNECTION);
	if (!TestState(ceaddr, SERVER, S_AWAITENABLE)) 
		rpc2_Quit(RPC2_FAIL);
	SetState(ceaddr, S_AWAITREQUEST);
	rpc2_Quit(RPC2_SUCCESS);
}

long RPC2_SetColor(Conn, Color)
    RPC2_Handle Conn;
    RPC2_Integer Color;
    {
    struct CEntry *ceaddr;

    say(0, RPC2_DebugLevel, "RPC2_SetColor()\n");

    rpc2_Enter();
    ceaddr = rpc2_GetConn(Conn);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);
    ceaddr->Color = Color;
    rpc2_Quit(RPC2_SUCCESS);
    }

long RPC2_GetColor(Conn, Color)
    RPC2_Handle Conn;
    RPC2_Integer *Color;
    {
    struct CEntry *ceaddr;

    say(0, RPC2_DebugLevel, "RPC2_GetColor()\n");

    rpc2_Enter();
    ceaddr = rpc2_GetConn(Conn);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);
    *Color = ceaddr->Color;
    rpc2_Quit(RPC2_SUCCESS);
    }

long RPC2_GetPeerLiveness(IN ConnHandle, OUT Time, OUT SETime)
    RPC2_Handle ConnHandle;
    struct timeval *Time;
    struct timeval *SETime;
    {
    struct CEntry *ceaddr;

    rpc2_Enter();
    say(999, RPC2_DebugLevel, "RPC2_GetPeerLiveness()\n");

    Time->tv_sec = Time->tv_usec = 0;
    SETime->tv_sec = SETime->tv_usec = 0;

    ceaddr = rpc2_FindCEAddr(ConnHandle);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);

    /* get live time for RPC2 connection */
    if (ceaddr->HostInfo)
	*Time = ceaddr->HostInfo->LastWord;	/* structure assignment */

    /* get live time for side effect, if any */
    if (ceaddr->SEProcs != NULL && ceaddr->SEProcs->SE_GetHostInfo != NULL)  {
	struct HEntry *he;
	long rc;
    
	if ((rc = (*ceaddr->SEProcs->SE_GetHostInfo)(ConnHandle, &he)) != RPC2_SUCCESS)
	    rpc2_Quit(rc);

	if (he) 
	    *SETime = he->LastWord;	/* structure assignment */
    }

    rpc2_Quit(RPC2_SUCCESS);
    }


/* 
 * returns the RPC and side effect network logs for the
 * peer of the connection Conn.  Note that the logs 
 * contain information from all connections to that 
 * peer, not just the connection Conn.  
 *
 * CAVEAT: the side effect logs may not be returned if
 * the side effect data structures have not been linked
 * into the host/portal data structures used for tracking
 * network activity.  The side effect linkage occurs when
 * side effect parameters are exchanged.  In SFTP, this
 * exchange may _not_ occur until the first RPC that passes
 * a side effect descriptor!
 */

long RPC2_GetNetInfo(IN Conn, INOUT RPCLog, INOUT SELog)
    RPC2_Handle Conn;
    RPC2_NetLog *RPCLog;
    RPC2_NetLog *SELog;
    {
    struct CEntry *ceaddr;

    say(0, RPC2_DebugLevel, "RPC2_GetNetInfo()\n");

    rpc2_Enter();

    if (RPCLog == NULL && SELog == NULL) 
	rpc2_Quit(RPC2_FAIL);

    if (SELog) SELog->ValidEntries = 0;
    if (RPCLog) RPCLog->ValidEntries = 0;

    ceaddr = rpc2_GetConn(Conn);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);

    /* first get the latency obs from the RPC layer */
    if (RPCLog && ceaddr->HostInfo)
	rpc2_GetHostLog(ceaddr->HostInfo, RPCLog);
    
    /* then get bandwidth obs from side effect layer */
    /* need a side effect call for getting the host info ptr */
    if (SELog &&
	ceaddr->SEProcs != NULL && ceaddr->SEProcs->SE_GetHostInfo != NULL)  {
	struct HEntry *he;
	long rc;
    
	if ((rc = (*ceaddr->SEProcs->SE_GetHostInfo)(Conn, &he)) != RPC2_SUCCESS)
	    rpc2_Quit(rc);

	if (he) rpc2_GetHostLog(he, SELog);
    }
    
    rpc2_Quit(RPC2_SUCCESS);
    }  


/* 
 * allows log entries to be added to the RPC or side effect
 * log for the peer of connection Conn.  This is useful 
 * for depositing externally derived information about
 * conditions to a particular host.  The number of log
 * entries to be deposited is in NumEntries.  The number 
 * of log entries actually deposted is returned in 
 * ValidEntries.
 */
long RPC2_PutNetInfo(IN Conn, INOUT RPCLog, INOUT SELog)
    RPC2_Handle Conn;
    RPC2_NetLog *RPCLog;
    RPC2_NetLog *SELog;
    {
    struct CEntry *ceaddr;
    int i;

    say(0, RPC2_DebugLevel, "RPC2_PutNetInfo()\n");

    rpc2_Enter();

    if (RPCLog == NULL && SELog == NULL) 
	rpc2_Quit(RPC2_FAIL);

    if (SELog) SELog->ValidEntries = 0;
    if (RPCLog) RPCLog->ValidEntries = 0;

    ceaddr = rpc2_GetConn(Conn);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);

    /* first the rpc layer */
    if (RPCLog && ceaddr->HostInfo)
	for (i = 0; i < RPCLog->NumEntries; i++) {
	    if (!rpc2_AppendHostLog(ceaddr->HostInfo, &RPCLog->Entries[i]))
		return(RPC2_FAIL);
	    RPCLog->ValidEntries++;
	}
    
    /* side effect layer */
    if (SELog &&
	ceaddr->SEProcs != NULL && ceaddr->SEProcs->SE_GetHostInfo != NULL)  {
	struct HEntry *he;
	long rc;
    
	if ((rc = (*ceaddr->SEProcs->SE_GetHostInfo)(Conn, &he)) != RPC2_SUCCESS)
	    rpc2_Quit(rc);

	if (he) 
	    for (i = 0; i < SELog->NumEntries; i++) {
		if (!rpc2_AppendHostLog(he, &SELog->Entries[i]))
		    return(RPC2_FAIL);
		SELog->ValidEntries++;
	    }
    }
    
    rpc2_Quit(RPC2_SUCCESS);
    }  

/* 
 * clears the RPC and side effect network logs for the
 * peer of the connection Conn.  
 */
long RPC2_ClearNetInfo(IN Conn)
    RPC2_Handle Conn;
    {
    struct CEntry *ceaddr;

    rpc2_Enter();
    say(999, RPC2_DebugLevel, "RPC2_ClearNetInfo()\n");

    ceaddr = rpc2_FindCEAddr(Conn);
    if (ceaddr == NULL) rpc2_Quit(RPC2_NOCONNECTION);

    /* get live time for RPC2 connection */
    if (ceaddr->HostInfo)
	rpc2_ClearHostLog(ceaddr->HostInfo);

    if (ceaddr->SEProcs != NULL && ceaddr->SEProcs->SE_GetHostInfo != NULL)  {
	struct HEntry *he;
	long rc;

	if ((rc = (*ceaddr->SEProcs->SE_GetHostInfo)(Conn, &he)) != RPC2_SUCCESS)
	    rpc2_Quit(rc);

	if (he) rpc2_ClearHostLog(he);
    }
    
    rpc2_Quit(RPC2_SUCCESS);
    }  

    
long rpc2_CreateIPSocket(long *svar, RPC2_HostIdent *hvar, RPC2_PortIdent *pvar)
{
	struct sockaddr_in bindaddr;
	struct servent *sentry;
	int blen = 0x8000 ;

	/* Allocate socket */
	*svar = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (*svar < 0) 
		return(RPC2_FAIL);

#if 0
	pe = getprotobyname("ip");
	if ( pe == NULL ) 
		exit(1);

 	/* let udp layer (17) give debugging info */
	rc = setsockopt(*svar, pe->p_proto, SO_DEBUG, NULL, 0);
	if ( rc ) {
		perror("setsockopt: ");
		exit(1);
	}

	rc = setsockopt(*svar, SOL_SOCKET, SO_SNDBUF, &blen, sizeof(blen));
	if ( rc ) {
		perror("setsockopt: ");
		exit(1);
	}

	rc = setsockopt(*svar, SOL_SOCKET, SO_RCVBUF, &blen, sizeof(blen));
	if ( rc ) {
		perror("setsockopt: ");
		exit(1);
	}

	rc = setsockopt(*svar, pe->p_proto, SO_DEBUG, NULL, 0);
	if ( rc ) {
		perror("setsockopt: ");
		exit(1);
	}
#endif
	/* set host address for bind() */
	bzero(&bindaddr, sizeof(bindaddr));
	bindaddr.sin_family = AF_INET;
	bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
					
	/* set port address for bind() */
	switch (pvar->Tag) {
	case RPC2_PORTBYNAME:
		sentry = getservbyname(pvar->Value.Name, "udp");
		if (sentry == NULL) 
			return(RPC2_BADSERVER);
		/* sentry has port in network order */
		bindaddr.sin_port  = sentry->s_port;
		break;
	
		/*  XXXX NO INETNUMBER , just ports */
	case RPC2_PORTBYINETNUMBER:
		bindaddr.sin_port = pvar->Value.InetPortNumber;
		break;

	case RPC2_DUMMYPORT:
	default:
		bindaddr.sin_port = 0;	/* kernel will assign */
	}

	/* Now bind the socket */
	if (bind (*svar, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
		if (errno == EADDRINUSE) 
			return(RPC2_DUPLICATESERVER);
		else 
			return(RPC2_BADSERVER);
	}

	/* Retrieve fully resolved socket address */
	CODA_ASSERT(pvar->Tag != RPC2_DUMMYPORT);

	hvar->Tag = RPC2_HOSTBYINETADDR;
	hvar->Value.InetAddress.s_addr = get_netaddr(*svar);
	if (hvar->Value.InetAddress.s_addr == 0) 
	    return (RPC2_FAIL);
	blen = sizeof(bindaddr);
	if (getsockname(*svar, (struct sockaddr *)&bindaddr, &blen) < 0) 
	    return(RPC2_FAIL);
	pvar->Tag = RPC2_PORTBYINETNUMBER;
	pvar->Value.InetPortNumber = bindaddr.sin_port;

#ifdef RPC2DEBUG
	if (RPC2_DebugLevel > 9) {
		rpc2_PrintHostIdent(hvar, rpc2_tracefile);
		printf("    ");
		rpc2_PrintPortIdent(pvar, rpc2_tracefile);
		printf("\n");
	}
#endif RPC2DEBUG
    
	return RPC2_SUCCESS;
}

/* get internet address from kernel, zero on error */
static unsigned int get_netaddr(long sock)
{
#ifdef DJGPP
      return __djgpp_get_my_host();   /* MJC--hack! */
#if 0
      /*#elif __CYGWIN32__ */
      char hostname[128];
    struct hostent *h;

    if (gethostname(hostname, sizeof(hostname)) != 0) {
	fprintf (stderr, "rpc2_GetLocalHost: cannot gethostname()");
	CODA_ASSERT (0);
    }

    h = gethostbyname(hostname);
    if (!h) {
	fprintf (stderr, "get_netaddr: cannot gethostbyname(%s)",
		 hostname);
	CODA_ASSERT (0);
    }
    printf("get_netaddr returning: %lx\n", *(long *)(h->h_addr_list[0]));
    return (*(unsigned int *)(h->h_addr_list[0]));
#endif
#else 
    struct ifconf ifc;
    struct ifreq conf[NETWORKS];
    struct sockaddr_in *sa_struct;
    int i;
    int interfaces;	/* number of interfaces returned by ioctl */

    ifc.ifc_len = sizeof(conf);
    ifc.ifc_buf = (caddr_t) conf;
    if (ioctl(sock, SIOCGIFCONF, (char *)&ifc) < 0) 
	    return(0);

    interfaces = ifc.ifc_len / sizeof(struct ifreq);
    for (i = 0; i < interfaces; i++)
	{
	sa_struct = (struct sockaddr_in *) &conf[i].ifr_addr;
	if (sa_struct->sin_addr.s_addr != 0 &&
	    sa_struct->sin_addr.s_addr != 0x100007f)
		return (sa_struct->sin_addr.s_addr);
	}
    return (0);
#endif
}


/* Fill in localhost with the IP address that is used when sending
   packets to remotehost.
*/
long rpc2_GetLocalHost(localhost, remotehost)
    RPC2_HostIdent *localhost, *remotehost;
{
    struct sockaddr_in sin;
    int s, i = sizeof(struct sockaddr_in);
#ifdef DJGPP
    CODA_ASSERT(remotehost->Tag == RPC2_HOSTBYINETADDR);
    /* horrible hack -- figure out how to do this with MSTCP */
    localhost->Tag = RPC2_HOSTBYINETADDR;
    localhost->Value.InetAddress.s_addr = __djgpp_get_my_host();
    return 0;
#elif __CYGWIN32__
    char hostname[128];
    struct hostent *h;

    if (gethostname(hostname, sizeof(hostname)) != 0) {
	fprintf (stderr, "rpc2_GetLocalHost: cannot gethostname()");
	CODA_ASSERT (0);
    }

    h = gethostbyname(hostname);
    if (!h) {
	fprintf (stderr, "rpc2_GetLocalHost: cannot gethostbyname(%s)",
		 hostname);
	CODA_ASSERT (0);
    }

    localhost->Tag = RPC2_HOSTBYINETADDR;
    localhost->Value.InetAddress.s_addr = h->h_addr.s_addr;
    fprintf (stderr, "rpc2_GetLocalHost: name %s len %d ip %s\n",
	     hostname, h->h_length, inet_ntoa(localhost->Value.InetAddress));
    return 0;
#else
    CODA_ASSERT(remotehost->Tag == RPC2_HOSTBYINETADDR);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(1);	/* dummy port number */
    sin.sin_addr.s_addr = remotehost->Value.InetAddress.s_addr;
    if((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	return -1;
    if(connect(s, (struct sockaddr *)&sin,i) == -1) {
	close(s);
	return -1;
    }
    if(getsockname(s, (struct sockaddr *)&sin,&i) == -1) {
	close(s);
	return -1;
    }
    close(s);
    localhost->Tag = RPC2_HOSTBYINETADDR;
    localhost->Value.InetAddress.s_addr = sin.sin_addr.s_addr;
    return 0;
#endif
}

unsigned long rpc2_TVTOTS(const struct timeval *tv)
    /* makes a longword time stamp in 1 msec units since rpc2_InitTime  */
{
    struct timeval delta;
    unsigned long diff;

    delta = *tv;

    SUBTIME(&delta, &rpc2_InitTime);
    TVTOTS(&delta, diff);

    if (diff == 0) diff = 1;  /* just in case we called right away */
    return(diff);
}

void rpc2_TSTOTV(const long ts, struct timeval *tv)
    /* converts a longword time stamp in 1 msec units since rpc2_InitTime back
     * to real-time */
{
    TSTOTV(tv, ts);
    ADDTIME(tv, &rpc2_InitTime);
    return;
}

unsigned long rpc2_MakeTimeStamp()
    /* makes a longword time stamp in 1 msec units since rpc2_InitTime  */
{
    struct timeval now;

    /* use the approximate version b/c gettimeofday is called often */
    /* but for now we take the safe route */
    FT_GetTimeOfDay(&now, (struct timezone *)0);

    return rpc2_TVTOTS(&now);
}


void rpc2_ResetObs(obsp, ceaddr) 
    long *obsp;
    struct CEntry *ceaddr;
    {
    long delta = ceaddr->reqsize * 8 * 100 / rpc2_Bandwidth;
    say(4, RPC2_DebugLevel, "rpc2_ResetObs: conn 0x%lx, obs %ld, delta %ld, new %ld\n", 
			     ceaddr->UniqueCID, *obsp, delta, *obsp-delta);
    if (*obsp > delta)  *obsp -= delta;
    }


/* Retransmission timer stuff */
void rpc2_UpdateRTT(RPC2_PacketBuffer *pb, struct CEntry *ceaddr)
{
    struct timeval observed, sent;
    long obs, diff; 
    unsigned long upperlimit;
    struct timeval *beta0;
    RPC2_NetLogEntry entry;

    if (!pb->Header.TimeStamp) return;

    rpc2_TSTOTV(pb->Header.TimeStamp, &sent);
    observed = pb->Prefix.RecvStamp;
    SUBTIME(&observed, &sent);

    rpc2_UpdateEstimates(ceaddr->HostInfo, &observed, ceaddr->reqsize);

    /* 
     * Requests can be sent and received in the same tick.  
     * (though this is unlikely in the 1ms/tick case)
     * Adding in service time on the server complicates things -- 
     * the clock may tick on the server (service time > 0) but not
     * on the client. Coerce this case to 1.
     */
    TVTOTS(&observed, obs);
    if (obs <= 0) obs = 1;

    /* log the round-trip time observation in the host log */
    entry.Tag = RPC2_MEASURED_NLE;
    entry.Value.Measured.Bytes = ceaddr->reqsize; //-2*sizeof(struct RPC2_PacketHeader);
    entry.Value.Measured.ElapsedTime = obs;
    entry.Value.Measured.Conn = ceaddr->UniqueCID;
    (void) rpc2_AppendHostLog(ceaddr->HostInfo, &entry);

    /* smooth observation if we have a bandwidth estimate */
    if (rpc2_Bandwidth) rpc2_ResetObs(&obs, ceaddr);

    if (ceaddr->RTT == 0)
        {
	/* initialize estimates */
	ceaddr->RTT = obs << RPC2_RTT_SHIFT;
	ceaddr->RTTVar = obs << (RPC2_RTTVAR_SHIFT-1);
        }
    else 
        {
	diff = obs - 1 - (ceaddr->RTT >> RPC2_RTT_SHIFT);
	if ((ceaddr->RTT += diff) <= 0)
	    ceaddr->RTT = 1;

	if (diff < 0) diff = -diff;
	diff -= ceaddr->RTTVar >> RPC2_RTTVAR_SHIFT;
	if ((ceaddr->RTTVar += diff) <= 0)
	    ceaddr->RTTVar = 1;
        }

    /* 
     * reset the lower limit on retry interval, in microseconds for
     * rpc2_SetRetry. It should be at least LOWERLIMIT, but no more than
     * Retry_Beta[0]. Try RTT + (RPC2_RTTVAR_SCALE * RTTVar) first.
     */
    ceaddr->LowerLimit = ((ceaddr->RTT >> RPC2_RTT_SHIFT) + ceaddr->RTTVar) * 1000;
    beta0 = &ceaddr->Retry_Beta[0];
    upperlimit = beta0->tv_usec + beta0->tv_sec * 1000000;

    if (ceaddr->LowerLimit < LOWERLIMIT) ceaddr->LowerLimit = LOWERLIMIT;
    else if (ceaddr->LowerLimit > upperlimit) ceaddr->LowerLimit = upperlimit;

    say(4, RPC2_DebugLevel, "rpc2_UpdateRTT: conn 0x%lx, obs %ld, RTT %ld, RTTVar %ld LL %lu usec\n", 
			     ceaddr->UniqueCID, obs, ceaddr->RTT, 
			     ceaddr->RTTVar, ceaddr->LowerLimit);

    /* now adjust retransmission intervals with new Lowerlimit */
    (void) rpc2_SetRetry(ceaddr);
}

