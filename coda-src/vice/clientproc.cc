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


/************************************************************************/
/*									*/
/*  clientproc.c - Maintain the FileServer user structure		*/
/*									*/
/*  Function	- A set of routines to build the user structure for	*/
/*		  the File Server.					*/
/*									*/
/************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <util.h>
#include <callback.h>
#include <prs.h>
#include <al.h>
#include <vice.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <srv.h>
#include <vice.private.h>


/* *****  Private variables  ***** */

PRIVATE HostTable hostTable[MAXHOSTTABLEENTRIES];
PRIVATE int maxHost = 0;


/* *****  Private routines  ***** */

PRIVATE HostTable *client_GetVenusId(RPC2_Handle);
PRIVATE void client_RemoveClients(HostTable *);
PRIVATE char *client_SLDecode(RPC2_Integer);
PRIVATE void client_SetUserName(ClientEntry *);


int CLIENT_Build(RPC2_Handle RPCid, char *User, RPC2_Integer sl, 
		 ClientEntry **client) 
{
    int errorCode = 0;
    *client = 0;

    /* Translate from textual representation of uid to user name (if necessary). */
    char username[PRS_MAXNAMELEN + 1];
    if (STRNEQ(User, "UID=", 4)) {
	if (AL_IdToName(atoi(User + 4), username))
	    strcpy(username,"System:AnyUser");
    } else {
	strcpy(username, User);
    }

    /* Get the private pointer; it will be used to hold a reference to the new client entry. */
    errorCode = (int) RPC2_GetPrivatePointer(RPCid, (char **)client);
    if(errorCode != RPC2_SUCCESS)
	    return(errorCode);
    if (*client) {
	/* I added this; couldn't see any reason not to free it. -JJK */
	free((char *)*client);
	*client = 0;
    }

    /* Get a free client table entry and initialize it. */
    *client = (ClientEntry *)malloc(sizeof(ClientEntry));
    assert(*client != 0);
    (*client)->RPCid = RPCid;
    (*client)->NextClient = 0;
    (*client)->DoUnbind = 0;
    (*client)->LastOp = 0;
    (*client)->SecurityLevel = sl;
    strcpy((*client)->UserName, username);
    (*client)->VenusId = client_GetVenusId(RPCid);

    /* Stash a reference to the new entry in the connection's private
       pointer. */
    errorCode = (int) RPC2_SetPrivatePointer(RPCid, (char *)*client);
    if(errorCode != RPC2_SUCCESS) {
	    free((char *)*client);
	    return(errorCode);
    }

    /* Link this client entry into the chain for the host. */
    ObtainWriteLock(&((*client)->VenusId->lock));
    (*client)->NextClient = (ClientEntry *)((*client)->VenusId->FirstClient);
    (*client)->VenusId->FirstClient = *client;
    ReleaseWriteLock(&((*client)->VenusId->lock));

    /* Further munge the name that gets recorded in the client entry. */
    client_SetUserName(*client);

    CurrentConnections++;

    return(0);
}


void CLIENT_Delete(ClientEntry *clientPtr) 
{
    if (clientPtr == 0) {
	LogMsg(0, SrvDebugLevel, stdout,  "Client pointer is zero in CLIENT_Delete");
	return;
    }

    LogMsg(1, SrvDebugLevel, stdout,  "Deleting client entry for user %s at %s.%d",
	     clientPtr->UserName, clientPtr->VenusId->HostName, clientPtr->VenusId->port);

    if(clientPtr->DoUnbind) {
	LogMsg(0, SrvDebugLevel, stdout,  "DoUnbind is TRUE in CLIENT_Delete");
	return;
    }

    if (clientPtr->VenusId && clientPtr->VenusId->FirstClient) {
	if (clientPtr->VenusId->FirstClient == clientPtr) {
		clientPtr->VenusId->FirstClient = clientPtr->NextClient;
	} else {
		for (ClientEntry *searchPtr = clientPtr->VenusId->FirstClient;
		     searchPtr; searchPtr = searchPtr->NextClient)
			if (searchPtr->NextClient == clientPtr)
				searchPtr->NextClient = clientPtr->NextClient;
	}
    }

    CurrentConnections--;

    /* Free the ClientEntry. */
    RPC2_SetPrivatePointer(clientPtr->RPCid, (char *)0);
    if(clientPtr->LastOp) {
	    clientPtr->DoUnbind = 1;
    } else {
	    LogMsg(0, SrvDebugLevel, stdout,  
		   "Unbinding RPC2 connection %d", clientPtr->RPCid);

	    RPC2_Unbind(clientPtr->RPCid);
	    clientPtr->RPCid = 0;
	    AL_FreeCPS(&(clientPtr->CPS));
	    clientPtr->VenusId = 0;
	    free((char *)clientPtr);
    }
}


PRIVATE HostTable *client_GetVenusId(RPC2_Handle RPCid) 
{

	/* Look up the Peer info corresponding to the given RPC
           handle. */
	RPC2_PeerInfo peer;
	int i;

	assert(RPC2_GetPeerInfo(RPCid, &peer) == 0);
	assert(peer.RemoteHost.Tag == RPC2_HOSTBYINETADDR);
	assert(peer.RemotePortal.Tag == RPC2_PORTALBYINETNUMBER);

	/* Look for a corresponding host entry. */
	for (i = 0; i < maxHost; i++)
		if (hostTable[i].host == (unsigned int) peer.RemoteHost.Value.InetAddress &&
		    hostTable[i].port == peer.RemotePortal.Value.InetPortNumber)
			break;
	
	/* Not found.  Make a new host entry. */
	assert(maxHost < MAXHOSTTABLEENTRIES-1);
	if (i == maxHost) {
		hostTable[i].host = (unsigned int) peer.RemoteHost.Value.InetAddress;
		hostTable[i].port = peer.RemotePortal.Value.InetPortNumber;
		hostTable[i].FirstClient = 0;
		hostTable[i].id = 0;
		sprintf(hostTable[i].HostName, "%08x", htonl(hostTable[i].host));

		Lock_Init(&hostTable[i].lock);
		maxHost++;
	}

	/* Lock the host entry. */
	ObtainWriteLock(&hostTable[i].lock);

#if 0  /* XXX This causes a consistent problem that the first RPC made on this connection
	  which is opcode 40 (NewConnectFS) has its packet buffer dropped, because the 
	  connection is not enabled in time */ 

	/* If the host entry is not new, validate it by making a
           gratuitous callback. */
	/* Note that failure will cause the callback connection to be
       discarded. */
	if ((hostTable[i].id != 0) && 
	    (CallBack(hostTable[i].id, &NullFid) != 0))
		CLIENT_CleanUpHost(&hostTable[i]);
#endif
	/* Unlock the host entry. */
	ReleaseWriteLock(&hostTable[i].lock);

	return(&hostTable[i]);
}


/* look up a host entry given the (callback) connection id */
HostTable *CLIENT_FindHostEntry(RPC2_Handle CBCid) 
{
    HostTable *he = NULL;

    /* Look for a corresponding host entry. */
    for (int i = 0; i < maxHost; i++)
	if (hostTable[i].id == CBCid) 
	    he = &hostTable[i];

    return(he);
}


int CLIENT_MakeCallBackConn(ClientEntry *Client) 
{

    RPC2_PeerInfo peer;
    RPC2_SubsysIdent sid;
    RPC2_CountedBS cbs;
    RPC2_BindParms bp;
    HostTable *HostEntry;

    /* Look up the Peer info corresponding to the given RPC handle. */
    assert(RPC2_GetPeerInfo(Client->RPCid, &peer) == 0);
    assert(peer.RemoteHost.Tag == RPC2_HOSTBYINETADDR);
    assert(peer.RemotePortal.Tag == RPC2_PORTALBYINETNUMBER);

    /* Subsystem identifier. */
    sid.Tag = RPC2_SUBSYSBYID;
    sid.Value.SubsysId = SUBSYS_CB;

    /* Dummy argument. */
    cbs.SeqLen = 0;

    /* Bind parameters */
    bp.SecurityLevel = RPC2_OPENKIMONO;
    bp.EncryptionType = RPC2_XOR;
    bp.SideEffectType = SMARTFTP;
    bp.ClientIdent = &cbs;
    bp.SharedSecret = NULL;

    assert(Client);
    HostEntry = Client->VenusId;
    ObtainWriteLock(&HostEntry->lock);

    /* Attempt the bind. */
    long errorCode = RPC2_NewBinding(&peer.RemoteHost, &peer.RemotePortal, &sid, &bp, &HostEntry->id);
    if (errorCode > RPC2_ELIMIT) {
	if (errorCode != 0) {
	    LogMsg(0, SrvDebugLevel, stdout,  "RPC2_Bind to %s portal %d for callback got %s",
		    HostEntry->HostName, HostEntry->port, ViceErrorMsg((int) errorCode));
	}

	/* Make a gratuitous callback. */
	errorCode = CallBack(HostEntry->id, &NullFid);
	if (errorCode != 0) {
	    LogMsg(0, SrvDebugLevel, stdout,  "Callback message to %s portal %d failed %s",
		    HostEntry->HostName, HostEntry->port, ViceErrorMsg((int) errorCode));
	    if (errorCode <= RPC2_ELIMIT) {
		HostEntry->id = 0;
	    }
	}
    } else {
	LogMsg(0, SrvDebugLevel, stdout,  "RPC2_Bind to %s portal %d for callback failed %s",
		HostEntry->HostName, HostEntry->port, ViceErrorMsg((int) errorCode));
	HostEntry->id = 0;
    }

    if (HostEntry->id == 0) 
	    errorCode = EPIPE;  /* don't return an RPC2 error */

    ReleaseWriteLock(&HostEntry->lock);

    return((int) errorCode);
}


void CLIENT_CallBackCheck() 
{
    struct timeval tp;
    struct timezone tsp;
    TM_GetTimeOfDay(&tp, &tsp);
    unsigned int checktime = (unsigned int) tp.tv_sec - (5 * 60);

    for (int i = 0; i < maxHost; i++) {
	if ((hostTable[i].id) && (hostTable[i].LastCall < checktime)) {
	    ObtainWriteLock(&hostTable[i].lock);
	    long rc = CallBack(hostTable[i].id, &NullFid);
	    if (rc <= RPC2_ELIMIT) {
		LogMsg(0, SrvDebugLevel, stdout,  "Callback failed %s for ws %s, portal %d",
			ViceErrorMsg((int) rc), hostTable[i].HostName, hostTable[i].port);
		CLIENT_CleanUpHost(&hostTable[i]);
	    }
	    ReleaseWriteLock(&hostTable[i].lock);
	}
    }
}


void CLIENT_CleanUpHost(HostTable *ht) 
{
    LogMsg(1, SrvDebugLevel, stdout,  "Cleaning up a HostTable for %s.%d",
	     ht->HostName, ht->port);

    if (ht->id) {
	client_RemoveClients(ht);		/* remove any connections for this Venus */
	DeleteVenus(ht);		/* remove all callback entries	*/
	LogMsg(1, SrvDebugLevel, stdout,  "Unbinding RPC2 connection %d", ht->id);
	RPC2_Unbind(ht->id);
	ht->id = 0;
	ht->FirstClient = 0;
    }
}


PRIVATE void client_RemoveClients(HostTable *ht) 
{
    for (ClientEntry *cp = ht->FirstClient; cp; cp = ht->FirstClient) {
	CLIENT_Delete(cp);
	if(cp == ht->FirstClient) {
	    LogMsg(0, SrvDebugLevel, stdout,  "RemoveClients got a failure from DeleteClient");
	    break;
	}
    }
}


PRIVATE char *client_SLDecode(RPC2_Integer sl) 
{
    if(sl == RPC2_OPENKIMONO) return("OpenKimono");
    if(sl == RPC2_AUTHONLY) return("AuthOnly");
    if(sl == RPC2_HEADERSONLY) return("HeadersOnly");
    if(sl == RPC2_SECURE) return("Secure");
    return("Unknown");
}


void CLIENT_PrintClients() 
{
    struct timeval tp;
    struct timezone tsp;
    TM_GetTimeOfDay(&tp, &tsp);
    LogMsg(1, SrvDebugLevel, stdout,  "List of active users at %s", ctime(&tp.tv_sec));

    for(int i = 0; i < maxHost; i++) {
	for(ClientEntry *cp = hostTable[i].FirstClient; cp; cp=cp->NextClient) {
	    LogMsg(1, SrvDebugLevel, stdout,  "user = %s at %s.%d cid %d security level %s",
		   cp->UserName, hostTable[i].HostName, hostTable[i].port, 
		   cp->RPCid, client_SLDecode(cp->SecurityLevel));
	}
    }
}

/* Coerce name to System:AnyUser if not properly authenticated. */
static void client_SetUserName(ClientEntry *client) 
{
	char *name;
	if (Authenticate && client->SecurityLevel == RPC2_OPENKIMONO)
		name = "System:AnyUser";
	else
		name = client->UserName;

	/* Translate the name to a proper Id. */
	if (AL_NameToId(name, (int *)&(client->Id)) != 0) {
		if(!STREQ(client->UserName, "NEWCONNECT"))
			LogMsg(0, SrvDebugLevel, stdout,  
			       "User id %s unknown", client->UserName);
		assert(AL_NameToId("System:AnyUser", 
				   (int *)&(client->Id)) == 0);
	}
	assert(AL_GetInternalCPS((int) client->Id, &(client->CPS)) == 0);
	
}


/* This counts the number of workstations and the number of active
   workstations. */
/* A workstation is active if it has received a call since time. */
void CLIENT_GetWorkStats(int *num, int *active, unsigned int time) 
{
	*num = 0;
	*active = 0;
	
	for(int i = 0; i < maxHost; i++)
		if (hostTable[i].id != 0) {
			(*num)++;
			if (hostTable[i].ActiveCall > time) 
				(*active)++;
		}
}
