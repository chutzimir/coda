/* BLURB lgpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the  terms of the  GNU  Library General Public Licence  Version 2,  as
shown in the file LICENSE. The technical and financial contributors to
Coda are listed in the file CREDITS.

                        Additional copyrights

#*/

/*
                         IBM COPYRIGHT NOTICE

                          Copyright (C) 1986
             International Business Machines Corporation
                         All Rights Reserved

This  file  contains  some  code identical to or derived from the 1986
version of the Andrew File System ("AFS"), which is owned by  the  IBM
Corporation.   This  code is provided "AS IS" and IBM does not warrant
that it is free of infringement of  any  intellectual  rights  of  any
third  party.    IBM  disclaims  liability of any kind for any damages
whatsoever resulting directly or indirectly from use of this  software
or  of  any  derivative work.  Carnegie Mellon University has obtained
permission to  modify,  distribute and sublicense this code,  which is
based on Version 2  of  AFS  and  does  not  contain  the features and
enhancements that are part of  Version 3 of  AFS.  Version 3 of AFS is
commercially   available   and  supported  by   Transarc  Corporation,
Pittsburgh, PA.

*/


/*
	-- SFTP routines related to tracing 
*/


#include <stdio.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <errno.h>
#include "lwp.h"
#include "timer.h"
#include "rpc2.h"
#include "rpc2.private.h"
#include "se.h"
#include "sftp.h"
#include "cbuf.h"


extern int errno;

#define TRACELEN 1000

struct TraceEntry
    {
    enum PktType {SENT, RECVD, STATUS, BOGUS} tcode;
    struct RPC2_PacketHeader ph;
    };

#ifdef RPC2DEBUG
struct CBUF_Header *TraceBuf;
#endif RPC2DEBUG

int sftp_XmitPacket(long whichSocket, RPC2_PacketBuffer *whichPacket,
		    RPC2_HostIdent *whichHost, RPC2_PortIdent *whichPort)
    {
#ifdef RPC2DEBUG
    struct TraceEntry *te;

    te = (struct TraceEntry *)CBUF_NextSlot(TraceBuf);
    te->tcode = SENT;
    te->ph = whichPacket->Header;	/* structure assignment */
#endif RPC2DEBUG

    rpc2_XmitPacket(whichSocket, whichPacket, whichHost, whichPort);

    if (ntohl(whichPacket->Header.Flags) & RPC2_MULTICAST)
	{
	rpc2_MSent.Total--;
	rpc2_MSent.Bytes -= whichPacket->Prefix.LengthOfPacket;
	sftp_MSent.Total++;
	sftp_MSent.Bytes += whichPacket->Prefix.LengthOfPacket;
	}
    else
	{
	rpc2_Sent.Total--;
	rpc2_Sent.Bytes -= whichPacket->Prefix.LengthOfPacket;
	sftp_Sent.Total++;
	sftp_Sent.Bytes += whichPacket->Prefix.LengthOfPacket;
	}

    return(RPC2_SUCCESS);
    }

long sftp_RecvPacket(long whichSocket, RPC2_PacketBuffer *whichPacket)
    {
#ifdef RPC2DEBUG
    struct TraceEntry *te;
#endif RPC2DEBUG

    long rc;
    
    rc = rpc2_RecvPacket(whichSocket, whichPacket);
    if (rc < 0) return(rc);

    if (ntohl(whichPacket->Header.Flags) & RPC2_MULTICAST)
	{
	rpc2_MRecvd.Total--;
	rpc2_MRecvd.Bytes -= whichPacket->Prefix.LengthOfPacket;
	sftp_MRecvd.Total++;
	sftp_MRecvd.Bytes += whichPacket->Prefix.LengthOfPacket;
	}
    else
	{
	rpc2_Recvd.Total--;
	rpc2_Recvd.Bytes -= whichPacket->Prefix.LengthOfPacket;
	sftp_Recvd.Total++;
	sftp_Recvd.Bytes += whichPacket->Prefix.LengthOfPacket;
	}

#ifdef RPC2DEBUG
    te = (struct TraceEntry *)CBUF_NextSlot(TraceBuf);
    te->tcode = RECVD;
    te->ph = whichPacket->Header;	/* structure assignment */
#endif RPC2DEBUG

    return(rc);
    }

void sftp_TraceStatus(struct SFTP_Entry *sEntry, int filenum, int linenum)
    {
#ifdef RPC2DEBUG
    struct TraceEntry *te;

    te = (struct TraceEntry *)CBUF_NextSlot(TraceBuf);
    te->tcode = STATUS;
    if (IsSource(sEntry))
	{
	te->ph.GotEmAll = htonl(sEntry->SendLastContig);
	te->ph.BitMask0 = (unsigned) htonl(sEntry->SendTheseBits[0]);
	te->ph.BitMask1 = (unsigned) htonl(sEntry->SendTheseBits[1]);
	}
    else
	{
	te->ph.GotEmAll = htonl(sEntry->RecvLastContig);
	te->ph.BitMask0 = (unsigned) htonl(sEntry->RecvTheseBits[0]);
	te->ph.BitMask1 = (unsigned) htonl(sEntry->RecvTheseBits[1]);
	}

    te->ph.Opcode = htonl(-1);
    te->ph.LocalHandle = htonl(sEntry->LocalHandle);
    te->ph.RemoteHandle = htonl(sEntry->PInfo.RemoteHandle);
    te->ph.SeqNumber = htonl(filenum);
    te->ph.Flags = 0;
    te->ph.SEFlags = 0;
    te->ph.BodyLength = htonl(linenum);
#endif RPC2DEBUG
    }

/* 1 ==> sftp1.c, 2 ==> sftp2.c, .... */
void sftp_TraceBogus(long filenum, long linenum)
{
#ifdef RPC2DEBUG
    struct TraceEntry *te;

    te = (struct TraceEntry *)CBUF_NextSlot(TraceBuf);
    te->tcode = BOGUS;
    te->ph.GotEmAll = 0;
    te->ph.BitMask0 = 0;
    te->ph.BitMask1 = 0;

    te->ph.Opcode = htonl(-1);
    te->ph.LocalHandle = 0;
    te->ph.RemoteHandle = 0;
    te->ph.SeqNumber = htonl(filenum);
    te->ph.Flags = 0;
    te->ph.SEFlags = 0;
    te->ph.BodyLength = htonl(linenum);
#endif RPC2DEBUG
    }




static void PrintSFEntry(tEntry, tId, outFile)
    struct TraceEntry *tEntry;
    long tId;
    FILE *outFile;    
    {
#ifdef RPC2DEBUG
    char *s;
    struct RPC2_PacketHeader *ph;

    switch(tEntry->tcode)
	{
	case SENT: s =  "SENT "; break;	
	case RECVD: s = "RECVD"; break;
	case STATUS: s = "STATUS"; break;
	case BOGUS: s = "BOGUS"; break;
	default:  s =   "?????"; break;
	}

    fprintf(outFile, "%8ld: %8s  ", tId, s);
    ph = &tEntry->ph;
    switch((int)ntohl(ph->Opcode))
	{
	case SFTP_START: s = "START"; break;
	case SFTP_ACK: s =   "ACK  "; break;
	case SFTP_DATA: s =  "DATA "; break;
	case SFTP_NAK: s =   "NAK  "; break;
	case SFTP_RESET: s = "RESET"; break;
	case SFTP_BUSY: s =  "BUSY "; break;
	case -1:	s = ""; break;
	default:         s = "?????"; break;
	}
    
    fprintf(outFile, "%6s  %6lu  0x%08lx  0x%08lx  %6lu  0x%08lx|%08lx  0x%08lx  0x%08lx  %4lu\n", s,
		(unsigned long)ntohl(ph->SeqNumber),
		(unsigned long)ntohl(ph->Flags),
		(unsigned long)ntohl(ph->SEFlags),
		(unsigned long)ntohl(ph->GotEmAll),
		(unsigned long)ntohl(ph->BitMask0),
		(unsigned long)ntohl(ph->BitMask1),
		(unsigned long)ntohl(ph->RemoteHandle),
		(unsigned long)ntohl(ph->LocalHandle),
		(unsigned long)ntohl(ph->BodyLength));
#endif RPC2DEBUG
    }

void sftp_DumpTrace(char *fName)
{
#ifdef RPC2DEBUG
    FILE *dumpfile;
    
    if ((dumpfile = fopen(fName, "w")) == NULL)
	{
	perror(fName);
	exit(-1);
	}
    fprintf(dumpfile, "%20s", "");
    fprintf(dumpfile, "%6s  %6s    %8s    %8s  %6s             %8s    %8s    %8s  %4s\n\n",
    	"Op", "SNo", "Flags", "SEFlags", "GotEm", "AlsoSeen", "RHandle", "LHandle", "Blen");
    CBUF_WalkBuff(TraceBuf, PrintSFEntry, TRACELEN, dumpfile);
    fclose(dumpfile);
#endif RPC2DEBUG
}


void sftp_InitTrace(void)
    {
#ifdef RPC2DEBUG
    TraceBuf = (struct CBUF_Header *)CBUF_Init(sizeof(struct  TraceEntry), TRACELEN, "SFTP Trace");
#endif RPC2DEBUG
    }

