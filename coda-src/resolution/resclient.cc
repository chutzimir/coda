/* BLURB gpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

#*/





/* resclient.c
 * 	Code implementing the client part of resolution.
 */

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include "coda_assert.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <struct.h>
#ifndef __CYGWIN32__
#include <dirent.h>
#endif

#include <lwp.h>
#include <rpc2.h>
#include <inodeops.h>
#include <util.h>
#include <codadir.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <cvnode.h>
#include <olist.h>
#include <errors.h>
#include <srv.h>
#include <vlist.h>
#include <operations.h>
#include <res.h>
#include <treeremove.h>
#include <vrdb.h>

#include "rescomm.h"
#include "resutil.h"
#include "pdlist.h"
#include "reslog.h"
#include "remotelog.h"
#include "timing.h"



#define ISCREATEOP(a)		((a) == ViceCreate_OP || \
				 (a) == ResolveViceCreate_OP || \
				 (a) == ViceMakeDir_OP || \
				 (a) == ResolveViceMakeDir_OP ||\
				 (a) == ViceSymLink_OP || \
				 (a) == ResolveViceSymLink_OP)

#define ISDELETEOP(a)		((a) == ViceRemove_OP || \
				 (a) == ResolveViceRemove_OP || \
				 (a) == ViceRemoveDir_OP || \
				 (a) == ResolveViceRemoveDir_OP)

class RUParm {	
  public:
    dlist *vlist;		/* list of vnodes of all objects */
    olist *hvlog;		/* remote log by host and vnode */
    unsigned long srvrid;  	/* serverid where rm happened */
    unsigned long vid;		/* volume id */
    int	rcode;			/* return code: 0 -> no conflicts */
    
    RUParm(dlist *vl, olist *rmtlog, unsigned long id, unsigned long v) {
	vlist = vl;
	hvlog = rmtlog;
	srvrid = id;
	vid = v;
	rcode = 0;
    }
};

/* Yield parameters */
/* N.B.  Yield "periods" MUST all be power of two so that AND'ing can be used! */
const int Yield_GetResObjPeriod = 8;
const int Yield_GetResObjMask = (Yield_GetResObjPeriod - 1);
const int Yield_CollectFidPeriod = 256;
const int Yield_CollectFidMask =(Yield_CollectFidPeriod - 1);
const int Yield_CheckSemPerformResPeriod = 8;
const int Yield_CheckSemPerformRes_Mask =(Yield_CheckSemPerformResPeriod - 1);
const int Yield_GetP2ObjFids_Period = 256;
const int Yield_GetP2ObjFids_Mask = (Yield_GetP2ObjFids_Period - 1);
const int Yield_GetP2Obj_Period = 8;
const int Yield_GetP2Obj_Mask = (Yield_GetP2Obj_Period - 1);
const int Yield_CreateP2Obj_Period = 8;
const int Yield_CreateP2Obj_Mask = (Yield_CreateP2Obj_Period - 1);
extern void PollAndYield();

/* definitions of OPS returned by CheckResSemantics */
#define PERFORMOP	0
#define NULLOP		1
#define	MARKPARENTINC	2
#define	MARKOBJINC	3
#define CREATEINCOBJ	4

/* extern routine declarations */
extern void AllocIncBSEntry(RPC2_BoundedBS *, char *, ViceFid *, ViceFid *, long);
extern int GetSubTree(ViceFid *, Volume *, dlist *);
extern int GetParentFid(Volume *, ViceFid *, ViceFid *);

int DirRUConf(RUParm *, char *, long, long);
int EDirRUConf(PDirEntry, void *);

/* private routine declarations */
static rlent *FindRmtPartialOps(int , rlent *, int , rlent **, int *);
static rlent *CreateCompList(int *, rlent **, int , int *, int , rlent **);
static int ComputeCompOps(olist *, ViceFid *, rlent **, int *);
static void PreProcessCompOps(rlent *, int);
static int GetResObjs(rlent *, int , ViceFid *, Volume **, dlist *);
static int CheckSemPerformRes(rlent *, int, Volume *, ViceFid *, dlist *, olist *, dlist *, int *);
static int CheckValidityResOp(rlent *, int, int, int,dlist *, ViceFid *, olist *);
static int PerformResOp(rlent *, dlist *, olist *, vle *, Volume *, VolumeId, int *);
static void ProcessResResult(int *, int, rlent *, ViceFid *, dlist *, int, RPC2_BoundedBS *);
static RUConflict(rlent *, dlist *, olist *, ViceFid *);
static char *ExtractNameFromRLE(rlent *);
static void ExtractChildFidFromRLE(rlent *, ViceFid *);
static int ExtractVNTypeFromRLE(rlent *);
static int CompareStoreId(ViceStoreId *, ViceStoreId *);
static int CmpLogEntries(rlent **, rlent **);
static int CmpFidOp(rlent **, rlent **);
static int CmpNames(rlent *, rlent *);
static int ResolveRename(rlent *, Volume *, VolumeId, ViceFid *, dlist *, olist *, dlist *, int *);
static int ResolveCrossDirRename(rlent *, ViceFid *, Volume *, dlist *, dlist *);
static int CleanRenameTarget(rlent *, dlist *, Volume *, VolumeId, olist *, int *);
static int CheckResolveRenameSemantics(rlent *, Volume *, ViceFid *, dlist *, vle **, vle **, vle **, vle **,olist *, dlist *, dlist *, int *);

long RS_FetchLog(RPC2_Handle RPCid, ViceFid *Fid, RPC2_Integer *size,
		 SE_Descriptor *sed)
{
    Vnode *vptr = 0;
    Volume *volptr = 0;
    long errorCode = 0;
    SE_Descriptor sid;
    int volindex = -1;
    char *logbuf = 0;
    
    SLog(9,  "Entering RS_FetchLog(%x.%x.%x)",
	   Fid->Volume, Fid->Vnode, Fid->Unique);
    PROBE(tpinfo, CFETCHLOGBEGIN)
	/* Validate the fid */
	{
	    if (!XlateVid(&Fid->Volume)) {
		SLog(0,  "RS_FetchLog: Couldn't Xlate VSG %x",
		       Fid->Volume);
		errorCode = EINVAL;
		goto Exit;
	    }
	}
    
    /* get the object */
    {
	if (errorCode = GetFsObj(Fid, &volptr, &vptr, READ_LOCK,
				 NO_LOCK, 0, 0, 0)) {
	    SLog(0,  "RS_FetchLog: Error %d in getobj", errorCode);
	    errorCode = EINVAL;
	    goto Exit;
	}
    }
    
    /* Check Semantics - Locking - Log status*/
    {
	/* Check if volume has been locked by caller */
	/* Check that the log is not wrapped around */
	/* not implemented yet */
    }
    
    /* get log */
    {
	SLog(9,  "RS_FetchLog: Getting log ");
	
	/* make sure log is not empty - after a crash it is empty */
	{
	    pdlist *loglist;
	    VNResLog *vnlog;
	    if ((loglist = GetResLogList(vptr->disk.vol_index, vptr->vnodeNumber,
					 vptr->disk.uniquifier, &vnlog)) == NULL) {
		loglist = AllocateVMResLogList(vptr->disk.vol_index, 
					       vptr->vnodeNumber, 
					       vptr->disk.uniquifier);
		CODA_ASSERT(loglist);
	    }
	    
	    if (loglist->count() == 0) 
		CreateAfterCrashLogRecord(vptr, loglist);
	}
	
	olist localloglist;
	pdlist *pl = NULL;
	GetRmSubTreeLocalRMTLE(vptr->disk.vol_index,
			       vptr->vnodeNumber, 
			       vptr->disk.uniquifier,
			       &localloglist, pl);
	logbuf = FlattenLocalRMTLElist(&localloglist, (int *)size);
	PurgeLocalRMTLElist(&localloglist);
	if (!logbuf) {
	    SLog(0,  "RS_FetchLog: Log is empty");
	    errorCode = EINVAL;
	    goto Exit;
	}
    }	
    
    PollAndYield();
    /* ship the log */
    {
	SLog(9,  "RS_FetchLog: Shipping log ");
	memset(&sid, 0, sizeof(SE_Descriptor));
	sid.Tag = SMARTFTP;
	sid.Value.SmartFTPD.TransmissionDirection = SERVERTOCLIENT;
	sid.Value.SmartFTPD.SeekOffset = 0;
	sid.Value.SmartFTPD.hashmark = (SrvDebugLevel > 2 ? '#' : '\0');
	sid.Value.SmartFTPD.Tag = FILEINVM;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqLen = *size;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.MaxSeqLen = *size;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqBody = 
	    (RPC2_ByteSeq)logbuf;
	
	if((errorCode = RPC2_InitSideEffect(RPCid, &sid)) 
	   <= RPC2_ELIMIT) {
	    SLog(0,  "RS_FetchLog: InitSE failed (%d), (%x.%x.%x)",
		   errorCode, Fid->Volume, Fid->Vnode, Fid->Unique);
	    goto Exit;
	}
	
	if ((errorCode = RPC2_CheckSideEffect(RPCid, &sid, SE_AWAITLOCALSTATUS)) 
	    <= RPC2_ELIMIT) {
	    SLog(0,  "RS_FetchLog: CheckSE failed (%d), (%x.%x.%x)",
		   errorCode, Fid->Volume, Fid->Vnode, Fid->Unique);
	    if (errorCode == RPC2_SEFAIL1) errorCode = EIO;
	    goto Exit;
	}
    }
    
  Exit:
    {
	Error	fileCode = 0;
	if (vptr) {
	    VPutVnode(&fileCode, vptr);
	    CODA_ASSERT(fileCode == 0);
	}	
	if (volptr)
	    PutVolObj(&volptr, NO_LOCK);
	if (logbuf)
	    delete[] logbuf;
	SLog(9,  "RS_FetchLog: returns %d", errorCode);
	PROBE(tpinfo, CFETCHLOGEND);
	return(errorCode);
    }
}

/* Phase 1 of Directory Resolution (Client End)
 *
 *	Consists of:
 *		1. Fetch the linear log from the coordinator
 *		2. Build the index for the remote sites' log
 *		3. Compute Compensating Operations for the local site
 *			to make the directory replicas identical
 *		4. Perform the required operations; One of
 *		   a) PerformOP as is;
 *		   b) Create Inconsistent child (eg. link/remove conflicts)
 *		   c) Mark existing child inconsistent (eg. rm/update conflicts)
 *		   d) Mark Directory being resolved in conflict (name/name conflicts)
 *		5. Mark the directory with unique storeid;
 *		6. Commit/Abort the operation depending on error conditions
 */
long RS_DirResPhase1(RPC2_Handle RPCid, ViceFid *Fid, RPC2_Integer size, 
		     ViceStatus *status, RPC2_BoundedBS *piggyinc,
		     SE_Descriptor *sed) {
    PROBE(tpinfo, CPHASE1BEGIN);
    long errorCode = 0;
    char *logbuf = 0;
    olist *hvlog = 0;	/* log by *H*ost and *V*node */
    rlent *CompList = 0;
    int	nCompOps = 0;
    Volume *volptr = 0;
    dlist *vlist = 0;
    dlist *inclist = 0;
    int nblocks = 0;
    /* get log from coordinator */
    {
	logbuf = (char *)malloc(size);
	CODA_ASSERT(logbuf);
	SE_Descriptor	sid;
	memset(&sid, 0, sizeof(SE_Descriptor));
	sid.Tag = SMARTFTP;
	sid.Value.SmartFTPD.TransmissionDirection = CLIENTTOSERVER;
	sid.Value.SmartFTPD.SeekOffset = 0;
	sid.Value.SmartFTPD.hashmark = (SrvDebugLevel > 2 ? '#' : '\0');
	sid.Value.SmartFTPD.Tag = FILEINVM;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqLen = size;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.MaxSeqLen = size;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqBody = 
	(RPC2_ByteSeq)logbuf;
	
	if((errorCode = RPC2_InitSideEffect(RPCid, &sid)) 
     <= RPC2_ELIMIT) {
	    SLog(0,  "RS_FetchLog: InitSE failed (%d), (%x.%x.%x)",
	    errorCode, Fid->Volume, Fid->Vnode, Fid->Unique);
	    goto Exit;
	}

if ((errorCode = RPC2_CheckSideEffect(RPCid, &sid, SE_AWAITLOCALSTATUS)) 
     <= RPC2_ELIMIT) {
    SLog(0,  "FetchBulkTransfer: CheckSE failed (%d), (%x.%x.%x)",
    errorCode, Fid->Volume, Fid->Vnode, Fid->Unique);
    if (errorCode == RPC2_SEFAIL1) errorCode = EIO;
    goto Exit;
}
}

/* build logs/byhost/byvnode structure */
{	
    hvlog = new olist();
    BuildRemoteResLogIndexByHost((rlent *)logbuf, 
				 (int)(size/sizeof(rlent)),
				 hvlog);
    
}

PollAndYield();
PROBE(tpinfo, COMPOPSBEGIN);
/* compute partial ops/compensation operations list */
{
    if (errorCode = ComputeCompOps(hvlog, Fid, &CompList, &nCompOps)) {
	SLog(0,  "RS_DirResPhase1 - Error %d in ComputeCompOps",
	       errorCode);
	goto Exit;
    }
}
PROBE(tpinfo, COMPOPSEND);

PollAndYield();

/* resolve */
{
    PreProcessCompOps(CompList, nCompOps);
    PollAndYield();
    
    vlist = new dlist((CFN) VLECmp);
    if (errorCode = GetResObjs(CompList, nCompOps, Fid, 
			       &volptr, vlist)) {
	SLog(0,  "RS_DirResPhase1 Error %d in Getting objs",
	       errorCode);
	goto Exit;
    }
    
    /* Check Semantics and perform compensating operations */
    /*
      result = (int *)malloc(nCompOps * sizeof(int));
      */
    inclist = new dlist((CFN)CompareIlinkEntry);
    PROBE(tpinfo, PERFOPSBEGIN);
    if (errorCode = CheckSemPerformRes(CompList, nCompOps, volptr, 
				       Fid, vlist, hvlog, inclist, 
				       &nblocks)) {
	SLog(0,  "RS_DirResPhase1 Error %d during CheckSemPerformRes",
	       errorCode);
	goto Exit;
    }
    PROBE(tpinfo, PERFOPSEND);
    
    /* set status of directory */
    {
	ViceStoreId stid;
	AllocStoreId(&stid);
	ViceVersionVector DiffVV;
	DiffVV = status->VV;
	vle *ov = FindVLE(*vlist, Fid);
	CODA_ASSERT(ov && ov->vptr);
	
	/* check if new vv is legal */
	int res = VV_Cmp_IgnoreInc(&Vnode_vv(ov->vptr), &DiffVV);
	if (res != VV_EQ && res != VV_SUB) {
	    SLog(0,  "RS_DirResPhase 1: %x.%x VV's are in conflict",
		   ov->vptr->vnodeNumber, ov->vptr->disk.uniquifier);
	    CODA_ASSERT(0);
	}
	else {
	    SubVVs(&DiffVV, &Vnode_vv(ov->vptr));
	    AddVVs(&Vnode_vv(ov->vptr), &DiffVV);
	    AddVVs(&V_versionvector(volptr), &DiffVV);
	}
	
	/* do cop1 update with new local storeid - 
	   ensure that different directory replicas are unequal */
	NewCOP1Update(volptr, ov->vptr, &stid);
	SetCOP2Pending(Vnode_vv(ov->vptr));
	
	SetStatus(ov->vptr, status, 0, 0);
	
	/* append log record */
	int ind = InitVMLogRecord(V_volumeindex(volptr),
				  Fid, &DiffVV.StoreId, 
				  ResolveNULL_OP, 0/* dummy argument */);
				  sle *SLE = new sle(ind);
				  ov->sl.append(SLE);
			      }
    }
    
  Exit:
    /*
      ProcessResResult(result, nCompOps, CompList, 
      Fid, vlist, errorCode, piggyinc);
      */
    PROBE(tpinfo, P1PUTOBJBEGIN);
    PutObjects((int)errorCode, volptr, NO_LOCK, vlist, nblocks, 1);
    PROBE(tpinfo, P1PUTOBJEND);
    
    if (!errorCode && inclist) 
	DlistToBS(inclist, piggyinc);
    
    
    /* clean up */
    {
	if (hvlog) {
	    PurgeRemoteResLogIndexByHost(hvlog);
	    delete hvlog;
	}
	if (logbuf)
	    free(logbuf);
	if (CompList)
	    free(CompList);
	/*
	  if (result)
	  free(result);
	  */
	if (inclist) {
	    ilink *il;
	    while (il = (ilink *)inclist->get())
		delete il;
	    delete inclist;
	    inclist = NULL;
	}
	/* vlist & volptr cleaned up in PutObjects */
    }
    
    SLog(9,  "RS_DirResPhase1 - returning %d", errorCode);
    PROBE(tpinfo, CPHASE1END);
    return(errorCode);
}


long RS_DirResPhase2(RPC2_Handle RPCid, ViceFid *Fid, ViceStoreId *logid, 
		     ViceStatus *status, RPC2_BoundedBS *piggyinc) {
    PROBE(tpinfo, CPHASE2BEGIN);
    Volume *volptr = 0;
    Vnode *vptr = 0;
    long errorCode = 0;
    int blocks = 0;
    VolumeId VSGVolnum = Fid->Volume;
    
    {
	if (!XlateVid(&Fid->Volume)) {
	    SLog(0,  
		   "DirResPhase2: Coudnt Xlate VSG %x", Fid->Volume);
	    PROBE(tpinfo, CPHASE2END);
	    return(EINVAL);
	}
    }
    
    // parse the list 
    dlist *inclist = new dlist((CFN)CompareIlinkEntry);
    BSToDlist(piggyinc, inclist);
    
    dlist *vlist = 0;
    // get the objects 
    {
	vlist = new dlist((CFN) VLECmp);
	
	if (errorCode = GetPhase2Objects(Fid, vlist, inclist, &volptr)) {
	    SLog(0,  "DirResPhase2: Error getting objects");
	    goto Exit;
	}
    }
    
    // create nonexistent objects 
    {
	if (errorCode = CreateResPhase2Objects(Fid, vlist, inclist, volptr, 
					       VSGVolnum, &blocks)) {
	    SLog(0,  "DirResPhase2: Error %d in create objects",
		   errorCode);
	    goto Exit;
	}
    }
    
    // Process all inclist entries 
    {
	dlist_iterator next(*inclist);
	ilink *il;
	while ((il = (ilink *)next())) {
	    ViceFid cfid;
	    cfid.Volume = Fid->Volume;
	    cfid.Vnode = il->vnode;
	    cfid.Unique = il->unique;
	    
	    ViceFid ipfid;
	    ipfid.Volume = Fid->Volume;
	    ipfid.Vnode = il->pvnode;
	    ipfid.Unique = il->punique;
	    
	    vle *v = FindVLE(*vlist, &cfid);
	    if (v) {
		if (!strcmp(il->name, ".")) 
		    MarkObjInc(&cfid, v->vptr);
		else if(v->vptr->disk.vparent == il->pvnode &&
			v->vptr->disk.uparent == il->punique)
		    MarkObjInc(&cfid, v->vptr);
		else {	
		    /* parents are different - mark both parents inc */
		    vle *ipv = FindVLE(*vlist, &ipfid);
		    if (ipv && ipv->vptr)
			MarkObjInc(&ipfid, ipv->vptr);
		    
		    ViceFid vpfid;
		    vpfid.Volume = Fid->Volume;
		    vpfid.Vnode = v->vptr->disk.vparent;
		    vpfid.Unique = v->vptr->disk.uparent;
		    vle *vpv = FindVLE(*vlist, &vpfid);
		    if (vpv && vpv->vptr)
			MarkObjInc(&vpfid, vpv->vptr);
		}
	    }
	    else { // child couldnt be created - mark parent inc 
		vle *ipv = FindVLE(*vlist, &ipfid);
		if (ipv && ipv->vptr)
		    MarkObjInc(&ipfid, ipv->vptr);
	    }
	}
    }
    
    // spool a resolve nullop record and set out status parameter 
    {
	vle *ov = FindVLE(*vlist, Fid);
	CODA_ASSERT(ov);
	if (AllowResolution && V_VMResOn(volptr)) {
	    int ind = InitVMLogRecord(V_volumeindex(volptr),
				      Fid, logid, ResolveNULL_OP, 0);
	    
	    sle *SLE = new sle(ind);
	    ov->sl.append(SLE);
	}
	if (AllowResolution && V_RVMResOn(volptr)) 
	    CODA_ASSERT(SpoolVMLogRecord(vlist, ov->vptr, 
				    volptr, logid, 
				    ResolveNULL_OP, 0) == 0);
	
	SetStatus(ov->vptr, status, 0, 0);
    }
    
  Exit:
    /* put all objects */
    PutObjects((int)errorCode, volptr, NO_LOCK, vlist, blocks, 1);
    
    // clean up 
    if (inclist) CleanIncList(inclist);
    
    SLog(9,  "DirResPhase2 returns %d", errorCode);
    PROBE(tpinfo, CPHASE2END);
    return(errorCode);
}

extern void UpdateVVs(ViceVersionVector *, ViceVersionVector *, ViceVersionVector *);

long RS_DirResPhase3(RPC2_Handle RPCid, ViceFid *Fid, ViceVersionVector *VV,
		     SE_Descriptor *sed) {
    PROBE(tpinfo, CPHASE3BEGIN);
    Volume *volptr = 0;
    dlist *vlist = new dlist((CFN) VLECmp);
    int ix = -1;
    long errorCode = 0;
    vle *ov = 0;
    vrent *vre = 0;
    
    {
	if (!XlateVid(&Fid->Volume)) {
	    SLog(0,  
		   "DirResPhase3: Coudnt Xlate VSG %x", Fid->Volume);
	    PROBE(tpinfo, CPHASE3END);
	    return(EINVAL);
	}
    }
    /* get objects */
    {
	ov = AddVLE(*vlist, Fid);
	if (errorCode = GetFsObj(Fid, &volptr, &ov->vptr, WRITE_LOCK, NO_LOCK, 0, 0, ov->d_inodemod)) {
	    SLog(0,  
		   "DirResPhase3: Error %d getting object %x.%x",
		   errorCode, Fid->Vnode, Fid->Unique);
	    goto Exit;
	}
    }
    /* get index of host */
    {
	vre = VRDB.find(V_groupId(volptr));
	CODA_ASSERT(vre);
	ix = vre->index(ThisHostAddr);
	CODA_ASSERT(ix >= 0);
    }
    
    /* if phase1 was successful update vv */
    SLog(9,  "DirResPhase3: Going to update vv");
    
    if ((&(VV->Versions.Site0))[ix] && COP2Pending(ov->vptr->disk.versionvector)) {
        ov->vptr->disk.versionvector.StoreId = VV->StoreId;
	(&(VV->Versions.Site0))[ix] = 0;
	UpdateVVs(&V_versionvector(volptr), &Vnode_vv(ov->vptr), VV);
	ClearCOP2Pending(ov->vptr->disk.versionvector);	
	(&(VV->Versions.Site0))[ix] = 1;
	
	// add a log entry that will be common for all hosts participating in this res
	SLog(0, "Going to check if logentry must be spooled\n");
	if (V_RVMResOn(volptr)) {
	    SLog(0, 
		   "Going to spool log entry for phase3\n");
	    CODA_ASSERT(SpoolVMLogRecord(vlist, ov->vptr, volptr, &(VV->StoreId), ResolveNULL_OP, 0) == 0);
	}
    }
    /* truncate log if success everywhere in phase 1 */
    {
	SLog(9, "DirResPhase3: Going to check if truncate log possible");
	unsigned long Hosts[VSG_MEMBERS];
	int i = 0;
	vv_t checkvv;

	vre->GetHosts(Hosts);
	vre->HostListToVV(Hosts, &checkvv);
	for (i = 0; i < VSG_MEMBERS; i++) 
	    if (((&(checkvv.Versions.Site0))[i]) ^ 
		((&(VV->Versions.Site0))[i]))
		break;
	if (i == VSG_MEMBERS) {
	    /* update set has 1 for all hosts */
	    SLog(9,  
		   "OldDirResPhase3: Success everywhere - truncating log");
	    if (AllowResolution && V_VMResOn(volptr)) 
		TruncResLog(V_volumeindex(volptr), Fid->Vnode, Fid->Unique);
	    if (AllowResolution && V_RVMResOn(volptr)) 
		ov->d_needslogtrunc = 1;
	}
    }
    
    /* return contents of directory in a buffer for coordinator to compare */
    {
	PDirHandle dh;
	int size = 0;
	dh = VN_SetDirHandle(ov->vptr);
	SLog(9,  "RS_DirResPhase3: Shipping dir contents ");
	SE_Descriptor sid;
	memset(&sid, 0, sizeof(SE_Descriptor));
	sid.Tag = SMARTFTP;
	sid.Value.SmartFTPD.TransmissionDirection = SERVERTOCLIENT;
	sid.Value.SmartFTPD.SeekOffset = 0;
	sid.Value.SmartFTPD.hashmark = (SrvDebugLevel > 2 ? '#' : '\0');
	sid.Value.SmartFTPD.Tag = FILEINVM;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqLen = size;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.MaxSeqLen = size;
	sid.Value.SmartFTPD.FileInfo.ByAddr.vmfile.SeqBody = 
		(RPC2_ByteSeq)DH_Data(dh);
	
	if((errorCode = RPC2_InitSideEffect(RPCid, &sid)) 
	   <= RPC2_ELIMIT) {
		SLog(0,  
		       "RS_DirResPhase3:  InitSE failed (%d)", errorCode);
		VN_PutDirHandle(ov->vptr);
		goto Exit;
	}
	
	if ((errorCode = RPC2_CheckSideEffect(RPCid, &sid, SE_AWAITLOCALSTATUS)) 
	    <= RPC2_ELIMIT) {
		SLog(0,  
		       "RS_DirResPhase3: CheckSE failed (%d)", errorCode);
		if (errorCode == RPC2_SEFAIL1) errorCode = EIO;
		VN_PutDirHandle(ov->vptr);
		goto Exit;
	}
	VN_PutDirHandle(ov->vptr);
    }
  Exit:
    PutObjects((int)errorCode, volptr, NO_LOCK, vlist, 0, 1);
    SLog(9,  "DirResPhase3 returns %d", errorCode);
    PROBE(tpinfo, CPHASE3END);
    return(errorCode);
}

/*
 * Given: 
 * 	An array of log entries for a remote site 
 * 	An array of log entries for this site
 * 	Determine the "highest" common point between the logs &
 *	Return the address of the remote operations not executed locally
 */
static rlent *FindRmtPartialOps(int nrmtentries, rlent *RmtLog,
				 int nlocalentries, rlent **SortedLocalLog,
				 int *npartialentries) {
    
    rlent **RmtLogPtrs = 0;
    
    SLog(9,  "Entering FindRmtPartialOps()");
    /* sort rmt log entries */
    {
	int i = 0;
	RmtLogPtrs = (rlent **)malloc(nrmtentries * 
				      sizeof(rlent *));
	for (i = 0; i < nrmtentries; i++)
	    RmtLogPtrs[i] = &(RmtLog[i]);
	qsort(RmtLogPtrs, nrmtentries, sizeof(rlent *),
	      (int (*)(const void *, const void *))CmpLogEntries);
	SLog(39,  "Sorted Remote operations are:");
	if (SrvDebugLevel >= 39) {
	    for (i = 0; i < nrmtentries; i++) 
		RmtLogPtrs[i]->print();
	}
    }
    
    SLog(9,  
	   "FindRmtPartialOps: Going to find common point with remote log");
    rlent *LatestCommonEntry = 0;
    /* find the Common point with the remote log */
    {
	int lindex, rindex;
	for (lindex = 0, rindex = 0;
	     lindex < nlocalentries && rindex < nrmtentries;
	     lindex++) {
	    ViceStoreId lstid = SortedLocalLog[lindex]->storeid;
	    ViceStoreId	rstid = RmtLogPtrs[rindex]->storeid;
	    int cmpresult = CompareStoreId(&lstid, &rstid);
	    SLog(39,  "cmpresult = %d; lindex = %d,rindex = %d", 
		   cmpresult, lindex, rindex);
	    while (cmpresult > 0 && rindex < (nrmtentries - 1)) {
		rindex ++;
		rstid = RmtLogPtrs[rindex]->storeid;
		cmpresult = CompareStoreId(&lstid, &rstid);
	    }
	    SLog(39,  
		   "After WHILE: cmpresult = %d, LatestCommonEntry(%x.%x)",
		   cmpresult, LatestCommonEntry ? LatestCommonEntry->storeid.Host:0,
		   LatestCommonEntry ? LatestCommonEntry->storeid.Uniquifier:0);
	    if ((cmpresult == 0) &&
		(RmtLogPtrs[rindex] > LatestCommonEntry)) {
		if ((ISNONRESOLVEOP(SortedLocalLog[lindex]->opcode)) &&
		    (ISNONRESOLVEOP(RmtLogPtrs[rindex]->opcode)))
		    LatestCommonEntry = RmtLogPtrs[rindex];
		else if ((LatestCommonEntry == 0) && 
			 ((SortedLocalLog[lindex]->opcode == ResolveAfterCrash_OP) ||
			  (RmtLogPtrs[rindex]->opcode == ResolveAfterCrash_OP)))
		    //if no common point found and one site is just recovering from 
		    //a crash then relax the condition that it must be 
		    // a non resolve operation 
		    /* REMOVE THIS AFTER LOGS ARE IN RVM */
		    LatestCommonEntry = RmtLogPtrs[rindex];
	    }
	    SLog(39,  "After IF LCE = %x.%x",
		   LatestCommonEntry ? LatestCommonEntry->storeid.Host:0,
		   LatestCommonEntry ? LatestCommonEntry->storeid.Uniquifier:0);
	}
	
    }
    
    /* clean up */
    if (RmtLogPtrs) free(RmtLogPtrs);
    
    /* compute position of partial-rmt log */
    {
	*npartialentries = 0;
	if (LatestCommonEntry) {
	    *npartialentries = nrmtentries - 
		(LatestCommonEntry - RmtLog) - 1;
	    return(++LatestCommonEntry);
	}
	else
	    return(0);
    }
}
#define KEEPFLAG 12345678

static rlent *CreateCompList(int *sizes, rlent **partialops, 
			      int nhosts, int *complistsize,
			      int nlentries, rlent **sortedLlog) {
    rlent **opsptrs = 0;
    int totalops;
    rlent *complist = 0;
    
    *complistsize = 0;
    
    SLog(9,  "Entering CreateCompList()");
    SLog(9,  "CreateCompList: %d local entries", nlentries);
    /* get all the partial ops together */
    {	
	totalops = 0;
	for (int i = 0; i < nhosts; i++) 
	    if (sizes[i] > 0)
		totalops += sizes[i];
	if (totalops <= 0) return(0);
	opsptrs = (rlent **)malloc(sizeof(rlent *) * totalops);
	SLog(39,  "CreateCompList: %d totalops ", totalops);
	int index = 0;
      { /* drop scope for int i below; to avoid identifier clash */
	for (int i = 0; i < nhosts; i++) 
	    for (int j = 0; j < sizes[i]; j++) {
		opsptrs[index] = partialops[i] + j;
		index++;
	    }
      } /* drop scope for int i above; to avoid identifier clash */
    }	
    /* sort the ops */
    SLog(39,  "CreateCompList: Sorting all ops lists");
    qsort(opsptrs, totalops, sizeof(rlent *), 
	(int (*)(const void *, const void *)) CmpLogEntries);
    {
	SLog(39,  
	       "CreateCompList: Sorted List of all partial remote ops:");
	if (SrvDebugLevel >= 39) 
	    for (int i = 0; i < totalops; i++) 
		opsptrs[i]->print();
    }
    
    SLog(9,  "CreateCompList: Deleting redundant entries");
    
    VnodeId	tmpdvnode = 0;
    /* delete duplicate entries */
    {	
	ViceStoreId prevstoreid;
	CODA_ASSERT(totalops > 0);
	prevstoreid = opsptrs[0]->storeid;
	tmpdvnode = opsptrs[0]->dvnode;
	opsptrs[0]->dvnode = KEEPFLAG;
	*complistsize = 1;
	for (int i = 1; i < totalops; i++) {
	    if (SID_EQ(opsptrs[i]->storeid, prevstoreid)) {
		SLog(39,  "CreateCompList: Deleting Entry %d (%x.%x) same as previous",
			i, prevstoreid.Host, prevstoreid.Uniquifier);
		opsptrs[i] = 0;
	    }
	    else {
		prevstoreid = opsptrs[i]->storeid;
		CODA_ASSERT(tmpdvnode == opsptrs[i]->dvnode);
		
		SLog(39,  "CreateCompList: Keeping entry %d (%x.%x)",
			i, prevstoreid.Host, prevstoreid.Uniquifier);
		opsptrs[i]->dvnode = KEEPFLAG;
		*complistsize += 1;
	    }
	}
    }
    
    {
	SLog(39,  "Sorted List of non duplicate entries");
	if (SrvDebugLevel >= 39) 
	    for (int i = 0; i < totalops; i++) {
		if (opsptrs[i])
		    opsptrs[i]->print();
	    }
    }
    
    SLog(9,  "CreateCompList: Deleting local ops");
    /* delete entries also in local log */
    {	int i, j;
	int res = -1;
	for (i = 0, j = 0; i < totalops && j < nlentries; i++) {
	    if (opsptrs[i]) {
		SLog(39,  
		       "CreateCompList: Looking at totallog ptr %d", i);
		while ((j < nlentries) &&
		       (res = CompareStoreId(&(opsptrs[i]->storeid),
					     &(sortedLlog[j]->storeid)))
		       > 0)
		    j++;
		SLog(39,  
		       "After Scanning over local entries j = %d", j);
		if (res == 0) {
		    SLog(39,  "Found a match between local entry %d and total entry %d",
			    j, i);
		    CODA_ASSERT(opsptrs[i]->dvnode == KEEPFLAG);
		    opsptrs[i]->dvnode = tmpdvnode;
		    SLog(39,  "Deleting Entry:");
		    if (SrvDebugLevel >= 39) 
			opsptrs[i]->print();
		    opsptrs[i] = 0;
		    *complistsize -= 1;
		}
		else if (j >= nlentries) {
		    SLog(39,  "Processed all local entries ");
		    break;
		}
	    }
	}
    }
    
    SLog(9,  "CreateCompList: mallocing compensation list");
    /* copy unique log records into a buffer */
    if (*complistsize > 0) {
	complist = (rlent *)malloc(sizeof(rlent) * *complistsize);
	CODA_ASSERT(complist);
	int index = 0;
	for (int i = 0; i < nhosts; i++) 
	    for (int j = 0; j < sizes[i]; j++) {
		rlent *tmprle = partialops[i] + j;
		if (tmprle->dvnode == KEEPFLAG) {
		    SLog(39,  
			   "Copying record with id %x.%x into comp list",
			    tmprle->storeid.Host, tmprle->storeid.Uniquifier);
		    tmprle->dvnode = tmpdvnode;
		    bcopy((const char *)tmprle, (char *) &complist[index], (int) sizeof(rlent));
		    index++;
		}
	    }
	CODA_ASSERT(index == *complistsize);
    }
    else 
	complist = NULL;
    
    /* clean up */
    if (opsptrs) free(opsptrs);
    
    SLog(9,  
	   "CreateCompList: List has %d entries", *complistsize);
    return(complist);
}
#undef KEEPFLAG

static int ComputeCompOps(olist *AllHostsList, ViceFid *Fid, 
			   rlent **CompOps, int *nCompOps) {
    
    int nlentries = 0;
    rlent **sortedLlog = 0;
    long errorCode = 0;
    
    SLog(9,  "Entering ComputeCompOps(): (%x.%x.%x)",
	    Fid->Volume, Fid->Vnode, Fid->Unique);
    *CompOps = 0;
    *nCompOps = 0;
    
    rmtle *llrmtle = 0;
    /* Extract the local log */
    {
	he *localHost = FindHE(AllHostsList, ThisHostAddr);
	if (!localHost) {
	    SLog(0,  "ComputeCompOps: Couldnt find list for host %x",
		    ThisHostAddr);
	    errorCode = EINVAL;
	    goto Exit;
	}
	llrmtle = FindRMTLE(&localHost->vlist, Fid->Vnode, 
			    Fid->Unique);
	if (!llrmtle) {
	    SLog(0,  "ComputeCompOps: Couldnt find local log for(%x.%x)",
		    Fid->Vnode, Fid->Unique);
	    errorCode = EINVAL;
	    goto Exit;
	}
    }
    
    SLog(9,  "ComputeCompOps: Sorting local Log ");
    /* sort local log */
    {
	nlentries = llrmtle->u.remote.nentries;
	sortedLlog = (rlent **)malloc(nlentries * sizeof(rlent *));
	CODA_ASSERT(sortedLlog);
	for (int i = 0; i < nlentries; i++) 
	    sortedLlog[i] = &(llrmtle->u.remote.log[i]);
	qsort(sortedLlog, nlentries, sizeof(rlent *), 
	      	(int (*)(const void *, const void *)) CmpLogEntries);
	
	SLog(39,  "Sorted Local Log has %d entries :", nlentries);
	if (SrvDebugLevel > 39) {
	    for (int i = 0; i < nlentries; i++) 
		(*sortedLlog)[i].print();
	}
    }
    
    SLog(9,  "ComputeCompOps: Computing partial ops for rmt sites");
    int npartialops[VSG_MEMBERS];
    rlent *partialops[VSG_MEMBERS];
    /* compute partial ops */
    {	
	int index = 0;
	for (int i = 0; i < VSG_MEMBERS; i++) {
	    npartialops[i] = 0;
	    partialops[i] = 0;
	}
	CODA_ASSERT(AllHostsList->count() <= VSG_MEMBERS);
	
	/* get each remote host's partial ops */
	{	
	    he *HE;
	    olist_iterator next(*AllHostsList);
	    while (HE = (he *)next()) {
		if (HE->hid != ThisHostAddr) {
		    rmtle *RLE = FindRMTLE(&HE->vlist, Fid->Vnode, Fid->Unique);
		    CODA_ASSERT(RLE);
		    
		    partialops[index] = FindRmtPartialOps(RLE->u.remote.nentries,
							  RLE->u.remote.log,
							  nlentries,
							  sortedLlog,
							  &npartialops[index]);
		    if (!partialops[index]) {
			SLog(0,  "Couldnt get common point with site %x",
				HE->hid);
			/* print local and remote logs */
			SLog(0,  "Local log contains:");
			for (int i = 0; i < nlentries; i++)
			    (*sortedLlog)[i].print();
			errorCode = EINVAL;
			goto Exit;
		    }
		    
		    SLog(39,  "Partial ops for site %d are:", index);
		    if (SrvDebugLevel >= 39) {
			for (int j = 0; j < npartialops[index]; j++) 
			    (partialops[index])[j].print();
		    }
		    
		}
		SLog(9,  "ComputeCompOps: Found partial ops for site %d",
			index);
		index++;
	    }
	    CODA_ASSERT(index == AllHostsList->count());
	}
    }
    
    SLog(9,  "ComputeCompOps: merging partial ops ");
    /* Combine partial ops and remove duplicates */
    *CompOps = CreateCompList(npartialops, partialops, 
			      AllHostsList->count(), nCompOps,
			      nlentries, sortedLlog);
    
    /* print compensation operations */
    SLog(39,  "%d Compensating operations for resolve are :", *nCompOps);
    {
	extern int SrvDebugLevel;
	if (SrvDebugLevel >= 39) {
	    for (int i = 0; i < *nCompOps; i++) {
		rlent *rl = &((*CompOps)[i]);
		rl->print();
	    }
	}
    }
    
  Exit:
    /* clean up */
    if (sortedLlog) free(sortedLlog);
    SLog(9,  "ComputeCompOps: returns %d", errorCode);
    return(errorCode);
}

/* PreProcessCompOps:
 * 	Take out entries from log that counteract each other
 *	i.e. Create foo followed by Rm foo. 
 *	w/o intervening link creations
 */
static void PreProcessCompOps(rlent *rlog, int nrents) {
    
    rlent **rlptrs = 0;
    
    /* sort the entries */
    {
	rlptrs = (rlent **)malloc(nrents * sizeof(rlent *));
	for (int i = 0; i < nrents; i++)
	    rlptrs[i] = &rlog[i];
	qsort(rlptrs, nrents, sizeof(rlent *), 
		(int (*)(const void *, const void *)) CmpFidOp);
    }
    
    /* null out the entries that cancel each other */
    {
	for (int i = 1; i < nrents; i++) {
	    if ((ISDELETEOP(rlptrs[i]->opcode)) &&
		(ISCREATEOP(rlptrs[i-1]->opcode))) {
		ViceFid c1Fid, c2Fid;
		ExtractChildFidFromRLE(rlptrs[i], &c1Fid);
		ExtractChildFidFromRLE(rlptrs[i-1], &c2Fid);
		if (FID_Cmp(&c1Fid, &c2Fid) == 0 && 
		    CmpNames(rlptrs[i], rlptrs[i-1]) == 0) {
		    rlptrs[i]->opcode = ResolveNULL_OP;
		    rlptrs[i-1]->opcode = ResolveNULL_OP;
		}
	    }
	}
    }
    free(rlptrs);
}

static int GetResObjs(rlent *rlog, int nrentries, ViceFid *Fid, 
		       Volume **volptr, dlist *vlist) 
{
    long errorCode = 0;
    Vnode *vptr = 0;
    
    SLog(9, "Entering GetResObjs %S", FID_(Fid));
    
    /* translate fid */
    {
	if (!XlateVid(&Fid->Volume)) {
	    SLog(0, "GetResObjs: Couldn't Xlate VSG %x", Fid->Volume);
	    return(EINVAL);
	}
    }
    
    SLog(9, "GetResObjs: Getting parent dir %s", FID_(Fid));
    /* get  parent directory first  */
    {
	AddVLE(*vlist, Fid);
	if (errorCode = GetFsObj(Fid, volptr, &vptr, READ_LOCK, 
				 NO_LOCK, 0, 0, 0)) 
		goto Exit;
    }
    
    
    SLog(9,  "GetResObjs: Gathering Fids for children");
    /* gather all the fids for children into vlist */
    {
	int count = 0;
	for (int i = 0; i < nrentries; i++) {
	    rlent *rle = &rlog[i];
	    
	    count++;
	    if ((count & Yield_CollectFidMask) == 0)
		PollAndYield();
	    CODA_ASSERT(rle->dvnode == vptr->vnodeNumber);
	    CODA_ASSERT(rle->dunique == vptr->disk.uniquifier);
	    switch(rle->opcode) {
	      case ResolveNULL_OP:
	      case ViceNewStore_OP:
	      case ResolveViceNewStore_OP:
		break;
	      case ResolveViceRemove_OP:
	      case ViceRemove_OP:
		{
		    ViceFid rmFid;
		    ViceFid vpFid;
		    rmFid.Volume = Fid->Volume;
		    rmFid.Vnode = rle->u.u_remove.cvnode;
		    rmFid.Unique = rle->u.u_remove.cunique;
		    vpFid.Volume = Fid->Volume;
		    if (ObjectExists(V_volumeindex(*volptr), 
				     vSmall,
				     vnodeIdToBitNumber(rmFid.Vnode),
				     rmFid.Unique, &vpFid)) {
			AddVLE(*vlist, &rmFid);
			if (vpFid.Vnode != Fid->Vnode ||
			    vpFid.Unique != Fid->Unique) 
			    AddVLE(*vlist, &vpFid);
		    }
		}
		break;
	      case ResolveViceCreate_OP:
	      case ViceCreate_OP:
		{
		    ViceFid crFid;
		    ViceFid vpFid;
		    crFid.Volume = Fid->Volume;
		    crFid.Vnode = rle->u.u_create.cvnode;
		    crFid.Unique = rle->u.u_create.cunique;
		    vpFid.Volume = Fid->Volume;
		    if (ObjectExists(V_volumeindex(*volptr), 
				     vSmall,
				     vnodeIdToBitNumber(crFid.Vnode),
				     crFid.Unique, &vpFid)) {
			AddVLE(*vlist, &crFid);
			if ((vpFid.Vnode != Fid->Vnode) ||
			    (vpFid.Unique != Fid->Unique))
			    AddVLE(*vlist, &vpFid);
		    }
		}
		break;
	      case ResolveViceRename_OP:
	      case ViceRename_OP:
		{
		    ViceFid rnsrcFid;	/* rename source object */
		    ViceFid rntgtFid;	/* rename target object's fid */
		    ViceFid NewDFid;	/* target parent dir */
		    ViceFid OldDFid;	/* source dir */
		    ViceFid ndvpFid;	/* target parent dir vnode's parent */
		    ViceFid odvpFid;	/* source parent dir vnode's parent */
		    ViceFid svpFid;		/* source object vnode's parent */
		    ViceFid tvpFid;		/* target object vnode's parent */
		    
		    OldDFid.Volume = NewDFid.Volume = Fid->Volume;	
		    rnsrcFid.Volume = rntgtFid.Volume = Fid->Volume;
		    ndvpFid.Volume = odvpFid.Volume = Fid->Volume;
		    svpFid.Volume = tvpFid.Volume = Fid->Volume;
		    
		    if (rle->u.u_rename.srctgt == SOURCE) {
			OldDFid.Vnode = rle->dvnode;
			OldDFid.Unique = rle->dunique;
			NewDFid.Vnode = rle->u.u_rename.OtherDirV;
			NewDFid.Unique = rle->u.u_rename.OtherDirU;
		    }
		    else {
			NewDFid.Vnode = rle->dvnode;
			NewDFid.Unique = rle->dunique;
			OldDFid.Vnode = rle->u.u_rename.OtherDirV;
			OldDFid.Unique = rle->u.u_rename.OtherDirU;
		    }
		    rnsrcFid.Vnode = rle->u.u_rename.rename_src.cvnode;
		    rnsrcFid.Unique = rle->u.u_rename.rename_src.cunique;
		    if (rle->u.u_rename.rename_tgt.tgtexisted) {
			rntgtFid.Vnode = rle->u.u_rename.rename_tgt.TgtVnode;
			rntgtFid.Unique = rle->u.u_rename.rename_tgt.TgtUnique;
		    }
		    if (ObjectExists(V_volumeindex(*volptr),
				     vLarge,
				     vnodeIdToBitNumber(NewDFid.Vnode),
				     NewDFid.Unique, &ndvpFid)) {
			AddVLE(*vlist, &NewDFid);
			AddVLE(*vlist, &ndvpFid);
		    }
		    if (!FID_EQ(&NewDFid, &OldDFid) &&
			ObjectExists(V_volumeindex(*volptr),
				     vLarge, 
				     vnodeIdToBitNumber(OldDFid.Vnode),
				     OldDFid.Unique, &odvpFid)) {
			AddVLE(*vlist, &OldDFid);
			AddVLE(*vlist, &odvpFid);
		    }
		    if (ObjectExists(V_volumeindex(*volptr),
				     ISDIR(rnsrcFid) ? vLarge : vSmall,
				     vnodeIdToBitNumber(rnsrcFid.Vnode),
				     rnsrcFid.Unique, &svpFid)) {
			AddVLE(*vlist, &rnsrcFid);
			if (svpFid.Vnode != OldDFid.Vnode ||
			    svpFid.Unique != OldDFid.Unique)
			    AddVLE(*vlist, &svpFid);
		    }	
		    if (rle->u.u_rename.rename_tgt.tgtexisted &&
			ObjectExists(V_volumeindex(*volptr),
				     ISDIR(rntgtFid) ? vLarge : vSmall,
				     vnodeIdToBitNumber(rntgtFid.Vnode),
				     rntgtFid.Unique, &tvpFid)) {
			if (!ISDIR(rntgtFid))
			    AddVLE(*vlist, &rntgtFid);
			else {
			    if (errorCode = GetSubTree(&rntgtFid, *volptr, vlist)){
				SLog(0,  "GetResObjs: error %d getting subtree",
					errorCode);
				goto Exit;
			    }
			}
			if (tvpFid.Vnode != NewDFid.Vnode ||
			    tvpFid.Unique != NewDFid.Unique)
			    AddVLE(*vlist, &tvpFid);
		    }
		}
		break;
	      case ResolveViceSymLink_OP:
	      case ViceSymLink_OP:
		{
		    ViceFid slFid;
		    ViceFid vpFid;
		    slFid.Volume = Fid->Volume;
		    slFid.Vnode = rle->u.u_symlink.cvnode;
		    slFid.Unique = rle->u.u_symlink.cunique;
		    vpFid.Volume = Fid->Volume;		    
		    if (ObjectExists(V_volumeindex(*volptr), 
				     vSmall,
				     vnodeIdToBitNumber(slFid.Vnode),
				     slFid.Unique, &vpFid)) {
			AddVLE(*vlist, &slFid);
			if ((vpFid.Vnode != Fid->Vnode) ||
			    (vpFid.Unique != Fid->Unique))
			    AddVLE(*vlist, &vpFid);
		    }
		}
		break;
	      case ResolveViceLink_OP:
	      case ViceLink_OP:
		{
		    ViceFid hlFid;
		    ViceFid vpFid;
		    hlFid.Volume = Fid->Volume;
		    hlFid.Vnode = rle->u.u_hardlink.cvnode;
		    hlFid.Unique = rle->u.u_hardlink.cunique;
		    vpFid.Volume = Fid->Volume;		    
		    if (ObjectExists(V_volumeindex(*volptr), 
				     vSmall,
				     vnodeIdToBitNumber(hlFid.Vnode),
				     hlFid.Unique, &vpFid)) {
			AddVLE(*vlist, &hlFid);
			if ((vpFid.Vnode != Fid->Vnode) ||
			    (vpFid.Unique != Fid->Unique))
			    AddVLE(*vlist, &vpFid);
		    }
		}
		break;
	      case ResolveViceMakeDir_OP:
	      case ViceMakeDir_OP:
		{
		    ViceFid mkdFid;
		    ViceFid vpFid;
		    mkdFid.Volume = Fid->Volume;
		    mkdFid.Vnode = rle->u.u_makedir.cvnode;
		    mkdFid.Unique = rle->u.u_makedir.cunique;
		    vpFid.Volume = Fid->Volume;		    
		    if (ObjectExists(V_volumeindex(*volptr), 
				     vLarge,
				     vnodeIdToBitNumber(mkdFid.Vnode),
				     mkdFid.Unique, &vpFid)) {
			AddVLE(*vlist, &mkdFid);
			if ((vpFid.Vnode != Fid->Vnode) ||
			    (vpFid.Unique != Fid->Unique))
			    AddVLE(*vlist, &vpFid);
		    }
		}
		break;
	      case ResolveViceRemoveDir_OP:
	      case ViceRemoveDir_OP:
		{
		    ViceFid rmdFid;
		    ViceFid vpFid;
		    rmdFid.Volume = Fid->Volume;
		    rmdFid.Vnode = rle->u.u_removedir.cvnode;
		    rmdFid.Unique = rle->u.u_removedir.cunique;
		    vpFid.Volume = Fid->Volume;
		    if (ObjectExists(V_volumeindex(*volptr), 
				     vLarge,
				     vnodeIdToBitNumber(rmdFid.Vnode),
				     rmdFid.Unique, &vpFid)) {
			if (errorCode = GetSubTree(&rmdFid, *volptr, vlist))
			    goto Exit;
			if (vpFid.Vnode != Fid->Vnode ||
			    vpFid.Unique != Fid->Unique)
			    AddVLE(*vlist, &vpFid);
		    }
		}
		break;
	      default:
		CODA_ASSERT(0);
		break;
	    }
	}
    }
    
    SLog(9,  "GetResObjs: Putting back parent vnode ");
    /* put back parent directory's vnode */
    {
	if (vptr) {
	    Error error = 0;
	    VPutVnode(&error, vptr);
	    CODA_ASSERT(error == 0);
	    vptr = 0;
	}
    }
    /* get all objects in fid order */
    {	
	int count = 0;
	dlist_iterator next(*vlist);
	vle *v;
	while (v = (vle *)next()) {
	    SLog(9,  "GetResObjects: acquiring %s", FID_(&v->fid));
	    if ( ISDIR(v->fid) )
		    v->d_inodemod = 1;
	    if (errorCode = GetFsObj(&v->fid, volptr, &v->vptr, 
				     WRITE_LOCK, 
				     NO_LOCK, 1, 0, v->d_inodemod))
		goto Exit;
	    count++;
	    if ((count && Yield_GetResObjMask) == 0)
		PollAndYield();
	}
    }
    
  Exit:
    if (vptr) {
	SLog(9,  "GetResObjs: ERROR condition - Putting back parent");
	Error error = 0;
	VPutVnode(&error, vptr);
	CODA_ASSERT(error == 0);
	vptr = 0;
    }
    
    SLog(9,  "GetResObjs: returning(%d)", errorCode);
    return(errorCode);
}

/* CheckSemPerformRes:
 *	Given a list of compensating operations (rlog)
 *	Check if it is legal to perform all these operations
 *		Algorithm:
 *		Check if Name exists in parent directory
 *		Check if object exists -
 *			If so, is it in the same directory
 *		Look up table to see if operation not allowed
 *	Then perform the operation 
 *
 * 	Return values:
 *		functions return value indicates abort/commit
 *		result[] - array of codes for outcome of each operation
 *			PERFORMOP, NULLOP, MARKPARENTINC, MARKOBJINC
 *			CREATEINCOBJ
 *			
 */
static int CheckSemPerformRes(rlent *rlog, int nrents, 
			       Volume *volptr, ViceFid *dFid, 
			       dlist *vlist, olist *hvlog,
			       dlist *inclist, int *nblocks) {
    long errorCode = 0;
    *nblocks = 0;
    int *result = 0;

    SLog(9,  "Entering CheckSemPerformRes()");
    
    if (nrents) {
	result = (int *)malloc(sizeof(int) * nrents);
	CODA_ASSERT((int)result != 0);
    }
    /* Initialize the results */
    {
	for (int i = 0; i < nrents; i++) 
	    result[i] = 0;
    }
    {
	vle *pv = 0;
	ViceFid cFid, nFid;
	char *name = NULL;
	int NameExists = FALSE;
	int ObjExists = FALSE;
	int ParentPtrOk = TRUE;
	
	pv = FindVLE(*vlist, dFid);
	CODA_ASSERT(pv);
	
	VolumeId VSGVolnum = V_id(volptr);
	if (!ReverseXlateVid(&VSGVolnum)) {
	    SLog(0,  "CheckSemPerformRes: Couldnt RevXlateVid %x",
		    VSGVolnum);
	    errorCode = EINVAL;
	    goto Exit;
	}
	
	SLog(39,  "CheckSemPerformRes: Looking at %d comp ops",
		nrents);
	for (int i = 0; i < nrents; i++) {
	    if (SrvDebugLevel >= 39) {
		SLog(39,  "Printing %d Compensating Operation:", i);
		rlog[i].print();
	    }
	    /* yield every nth record */
	    if ((i & Yield_CheckSemPerformRes_Mask) == 0)
		PollAndYield();

	    if (rlog[i].opcode == ResolveNULL_OP)
		continue;

	    /* handle rename separately */
	    if (rlog[i].opcode == ViceRename_OP || 
		rlog[i].opcode == ResolveViceRename_OP) {
		int tblocks = 0;
		errorCode = ResolveRename(&rlog[i], volptr, VSGVolnum, dFid, 
					  vlist, hvlog, inclist, &tblocks);
		if (errorCode) {
		    SLog(0,  "CheckSemPeformRes: Error %d from Resolve Rename",
			    errorCode);
		    goto Exit;
		}
		else {
		    *nblocks += tblocks;
		    continue;
		}
	    }
	    /* check if name exists */
	    {	
		NameExists = FALSE;
		name = ExtractNameFromRLE(&rlog[i]);
		if (name) {
		    PDirHandle dh;
		    dh = VN_SetDirHandle(pv->vptr);
		    if (DH_Lookup(dh, name, &nFid, CLU_CASE_SENSITIVE) == 0)
			NameExists = TRUE;
		    else 
			NameExists = FALSE;
		    VN_PutDirHandle(pv->vptr);
		}
		nFid.Volume = dFid->Volume;
		SLog(39,  "CheckSemPerformRes: NameExists = %d", NameExists);
	    }
	    
	    /* check if object exists */
	    {
		/* XXXXX BE CAREFUL WITH CHILD FID AND RENAMES */
		ExtractChildFidFromRLE(&rlog[i], &cFid);
		cFid.Volume = dFid->Volume;
		CODA_ASSERT(cFid.Vnode);
		
		/* if vnode exists - it will exist in vlist */
		ObjExists = FALSE;
		vle *cv = FindVLE(*vlist, &cFid);
		if (cv && cv->vptr) 
		    if (!cv->vptr->delete_me)
			ObjExists = TRUE;
		
		ParentPtrOk = TRUE;
		if (ObjExists) 
		    if ((cv->vptr->disk.vparent != dFid->Vnode) ||
			(cv->vptr->disk.uparent != dFid->Unique))
			ParentPtrOk = FALSE;
		SLog(39,  "CheckSemPerformRes: OE = %d, PPOK = %d",
			ObjExists, ParentPtrOk);
	    }
	    
	    /* Check Semantics and Perform */
	    {
		/* until log records are optimized do name length check - HACK YUCK */
		if (name) {
		    if (strlen(name) >= DIROPNAMESIZE - 1) {
			result[i] = MARKPARENTINC;
			SLog(0,  "Marking parend Inc.- name %s too long", 
				name);
		    }
		}
		if (result[i] == 0)
		    result[i] = CheckValidityResOp(&rlog[i], NameExists,
						   ObjExists, ParentPtrOk, 
						   vlist, dFid, hvlog);
		int tblocks = 0;
		int vntype;
		vle *pv, *cv; 		
		switch (result[i]) {
		  case PERFORMOP:
		    SLog(9,  "CheckSemPerformRes: Going to Perform Op");
		    errorCode = PerformResOp(&rlog[i], vlist, hvlog, pv, 
					     volptr, VSGVolnum, &tblocks);
		    break;
		  case NULLOP:
		    SLog(9,  "CheckSemPerformRes: NULL Operation - ignore");
		    continue;
		  case MARKPARENTINC:
		    SLog(9,  "CheckSemPerformRes: Marking Parent Inc");
		    pv = FindVLE(*vlist, dFid);
		    CODA_ASSERT(pv);
		    CODA_ASSERT(pv->vptr);
		    MarkObjInc(dFid, pv->vptr);
		    AddILE(*inclist, ".", dFid->Vnode, dFid->Unique, 
			   dFid->Vnode, dFid->Unique, (long)vDirectory);
		    break;
		  case MARKOBJINC:
		    SLog(9,  "CheckSemPerformRes: Marking Object Inc");
		    cv = FindVLE(*vlist, &cFid);
		    if (!cv || !cv->vptr) {
			SLog(0,  "MARKOBJINC: couldnt get obj(%x.%x)vnode pointer",
				cFid.Vnode, cFid.Unique);
			errorCode = EINVAL;
		    }
		    else {
			MarkObjInc(&cFid, cv->vptr);
			char *name = ExtractNameFromRLE(&rlog[i]);
			AddILE(*inclist, name, cFid.Vnode, cFid.Unique, 
			       dFid->Vnode, dFid->Unique, 
			       (long)(cv->vptr->disk.type));
		    }
		    break;
		  case CREATEINCOBJ:
		    /* xxx BE CAREFUL WITH CHILD FID AND RENAMES */
		    SLog(9,  "CheckSemPerformRes: Creating Inc Object");
		    ExtractChildFidFromRLE(&rlog[i], &cFid);
		    cFid.Volume = V_id(volptr);
		    name = ExtractNameFromRLE(&rlog[i]);
		    vntype = ExtractVNTypeFromRLE(&rlog[i]);
		    tblocks = 0;
		    errorCode = CreateObjToMarkInc(volptr, dFid, &cFid, name, 
						   vntype, vlist, &tblocks);
		    if (errorCode == 0) {
			vle *cv = FindVLE(*vlist, &cFid);
			CODA_ASSERT(cv);
			CODA_ASSERT(cv->vptr);
			MarkObjInc(&cFid, cv->vptr);
			AddILE(*inclist, name, cFid.Vnode, cFid.Unique, 
			       cv->vptr->disk.vparent, 
			       cv->vptr->disk.uparent,
			       (long)(cv->vptr->disk.type));
		    }
		    break;
		  default:
		    SLog(0,  "Illegal result from CheckValidityResOP");
		    CODA_ASSERT(0);
		}
		*nblocks += tblocks;
		if (errorCode)
		    goto Exit;
	    }
	}
    }
  Exit:
    SLog(9,  "CheckSemPerformRes: returning %d", errorCode);
    if (result) free(result);
    return(errorCode);
}

/*
 * CheckValidityResOp:
 *	This implements the state machine for checking semantics
 *	The state is determined by :
 *		1. Object Exists? (OE)
 *		2. If Exists, then in the same directory? (ParentPtrOk)
 *		3. Name Exists in parent? (NE)
 *	The Result is one of:
 *	        All Ok - Perform the Operation (PERFORMOP)
 *		Mark Parent Inconsistent (MARKPARENTINC)
 *		Mark Object Inconsistent (MARKOBJINC)
 *		Create Inconsistent Object (CREATEINCOBJ)
 *		Null Op - dont do the operation (NULLOP)
 */
static int CheckValidityResOp(rlent *rle, int NE, 
			       int OE, int ParentPtrOk,
			       dlist *vlist, ViceFid *dFid, 
			       olist *hvlog) {
    
    switch(rle->opcode) {
      case ViceNewStore_OP:
      case ResolveViceNewStore_OP:
	SLog(0,  "CheckValidityResOp: Got a Store operation in comp op");
	return(MARKPARENTINC);
      case ResolveViceRemove_OP:
      case ViceRemove_OP:
	if (!NE) {
	    if (OE) {
		if (ParentPtrOk) {
		    return(MARKPARENTINC);
		}
		else { 
		    return(MARKPARENTINC);
		}
	    }
	    else 	/* object doesnt exist */
		return(NULLOP);
	}	
	else {	/* name exists */
	    if (OE) {
		if (ParentPtrOk) {
		    /* object exists in same parent */
		    if (RUConflict(rle, vlist, hvlog, dFid))
			return(MARKOBJINC);	
		    else return(PERFORMOP);
		}
		else {
		    /* object exists in another directory */
		    return(MARKPARENTINC);
		}
	    }
	    else {
		/* object doesnt exist */
		return(MARKPARENTINC);
	    }
	}
	break;
      case ResolveViceCreate_OP:
      case ViceCreate_OP:
	if (!NE) {
	    if (OE) {
		CODA_ASSERT(0);	/* site participated in create cant get create again */
	    }
	    else  /* !OE */
		return(PERFORMOP);
	}
	else {	/* name exists */
	    if (OE) {
		CODA_ASSERT(0);
	    }
	    else  /* !OE */
		return(MARKPARENTINC);	/* N/N Conflict */
	}
	break;
      case ResolveViceRename_OP:
      case ViceRename_OP:
	CODA_ASSERT(0);
	break;
      case ResolveViceSymLink_OP:
      case ViceSymLink_OP:
	if (!NE) {
	    if (OE) {
		CODA_ASSERT(0); 	
	    }
	    else
		return(PERFORMOP);
	}
	else {	/* name exists */
	    if (OE) {
		CODA_ASSERT(0);
	    }
	    else /* !OE */
		return(MARKPARENTINC);
	}
	break;
      case ResolveViceLink_OP:
      case ViceLink_OP:
	if (!NE) {
	    if (OE) {
		if (ParentPtrOk) 
		    return(PERFORMOP);
		else /* rename happened on object */
		    return(MARKPARENTINC);
	    }
	    else 
		/* object doesnt exist - may have been removed */
		return(CREATEINCOBJ);
	}
	else {	/* name exists */
	    if (OE) {
		if (ParentPtrOk) {
		    return(NULLOP);
		}
		else /* slightly fishy here XXX */
		    return(MARKPARENTINC);
	    }
	    else  /* not sure yet XXXX */		
		return(MARKPARENTINC);
	}
	break;
      case ResolveViceMakeDir_OP:
      case ViceMakeDir_OP:
	if (!NE) {
	    if (OE) {
		CODA_ASSERT(0);	
	    }
	    else 
		/* object doesnt exist */
		return(PERFORMOP);
	}
	else {	/* name exists */
	    if (OE) {
		CODA_ASSERT(0);
	    }
	    else /* !OE */
		return(MARKPARENTINC);
	}
	break;
      case ResolveViceRemoveDir_OP:
      case ViceRemoveDir_OP:
	if (!NE) {
	    if (OE) {
		if (ParentPtrOk) 
		    return(MARKOBJINC);
		else
		    return(MARKPARENTINC);
	    }
	    else 
		return(NULLOP);
	}
	else {	/* name exists */
	    if (OE) {
		if (ParentPtrOk) {
		    if (RUConflict(rle, vlist, hvlog, dFid))
			return(MARKOBJINC);
		    else
			return(PERFORMOP);
		}
		else 
		    /* object exists in another directory */
		    return(MARKPARENTINC);
	    }
	    else 
		/* object doesnt exist */
		return(NULLOP);
	}
	break;
      default:
	CODA_ASSERT(0);
	return(0); /* keep g++ happy */
    }
}
/* Resolve renames: serious errors return non zero errorcode */
static int ResolveRename(rlent *rl, Volume *volptr, VolumeId VSGVolnum, 
			  ViceFid *dFid, dlist *vlist, olist *hvlog, 
			  dlist *inclist, int *blocks) {
    dlist *newinclist = 0;
    long errorCode = 0;
    vle *sv = 0;
    vle *tv = 0;
    vle *sdv = 0;
    vle *tdv = 0;
    
    SLog(9,  "Entering ResolveRename for (%x.%x.%x)", dFid->Volume,
	    dFid->Vnode, dFid->Unique);
    if ((rl->u.u_rename.OtherDirV != rl->dvnode) ||
	(rl->u.u_rename.OtherDirU != rl->dunique)) {
	ResolveCrossDirRename(rl, dFid, volptr, vlist, inclist);
	return(0);
    }
    
    newinclist = new dlist((CFN)CompareIlinkEntry);
    errorCode = CheckResolveRenameSemantics(rl, volptr, dFid, vlist, &sv, &tv, 
					    &sdv, &tdv, hvlog, inclist, 
					    newinclist, blocks);
    if (!errorCode) {
	int tblocks = 0;
	CODA_ASSERT(CleanRenameTarget(rl, vlist, volptr, VSGVolnum, 
				 hvlog, &tblocks) == 0);
	*blocks += tblocks;
	tblocks = 0;
	PerformRename(NULL, VSGVolnum, volptr, sdv->vptr, tdv->vptr, 
		      sv->vptr, (tv ? tv->vptr : 0), 
		      rl->u.u_rename.rename_src.oldname,
		      rl->u.u_rename.rename_tgt.newname, 
		      sdv->vptr->disk.unixModifyTime, 0, &rl->storeid, 
		      &sdv->d_cinode, &tdv->d_cinode, &sv->d_cinode, NULL);
	if (tv && tv->vptr->delete_me) {
	    tblocks = (int) -nBlocks(tv->vptr->disk.length);
	    CODA_ASSERT(AdjustDiskUsage(volptr, tblocks) == 0);
	    *blocks += tblocks;
	    if (tv->vptr->disk.type != vDirectory) {
		tv->f_sinode = tv->vptr->disk.inodeNumber;
		tv->vptr->disk.inodeNumber = 0;
	    }
	}
	/* XXX - MIGHT HAVE TO UPDATE THE VERSION VECTOR FOR THE CHILD ! */
	SpoolRenameLogRecord(ResolveViceRename_OP, sv, tv, sdv, tdv, volptr, 
			     rl->u.u_rename.rename_src.oldname, 
			     rl->u.u_rename.rename_tgt.newname,
			     &rl->storeid);
    }
    if (errorCode && errorCode == EINCONS) {
	SLog(0,  "Incorrect Res Rename: src = %s (%x.%x), tgt = %s (%x.%x)s",
		rl->u.u_rename.rename_src.oldname, 
		rl->u.u_rename.rename_src.cvnode,
		rl->u.u_rename.rename_src.cunique, 
		rl->u.u_rename.rename_tgt.newname, 
		rl->u.u_rename.rename_tgt.TgtVnode, 
		rl->u.u_rename.rename_tgt.TgtUnique);
	
	ilink *il;
	while (il = (ilink *)newinclist->get()) {
	    ViceFid fid;
	    fid.Volume = V_id(volptr);
	    fid.Vnode = il->vnode;
	    fid.Unique = il->unique;
	    
	    vle *v;
	    v = FindVLE(*vlist, &fid);
	    CODA_ASSERT(v); 
	    CODA_ASSERT(v->vptr);
	    MarkObjInc(&fid, v->vptr);
	    if (inclist->IsMember(il))
		delete il;
	    else
		inclist->insert(il);
	}
	CODA_ASSERT(newinclist->count() == 0);
	delete newinclist;
	newinclist = 0;
	errorCode = 0;
    }
    
    SLog(9,  "ResolveRename returning %d", errorCode);
    return(errorCode);
}

static int CheckResolveRenameSemantics(rlent *rl, Volume *volptr, 
					ViceFid *dFid, dlist *vlist, 
					vle **srcv, vle **tgtv, vle **srcdv, 
					vle **tgtdv,olist *hvlog, dlist *inclist, 
					dlist *newinclist, int *blocks) {
    SLog(9,  "Entering CheckResolveRenameSemantics");
    if (SrvDebugLevel > 9) 
	rl->print();
    *srcv = *tgtv = *srcdv = *tgtdv = 0;
    ViceFid OldDid, NewDid;
    ViceFid SrcFid, TgtFid;
    vle *opv = 0;
    vle *npv = 0;
    vle *sv = 0;
    vle *tv = 0;
    int SrcNameExists = FALSE;
    int SrcNameFidBindingOK = FALSE;
    int SrcObjExists = FALSE;
    int SrcParentPtrOK = FALSE;
    char name[MAXNAMLEN];
    long errorCode = 0;
    
    OldDid.Volume = NewDid.Volume = V_id(volptr);
    SrcFid.Volume = TgtFid.Volume = V_id(volptr);
    /* check that both src and target directories exist */
    {
	if (rl->u.u_rename.srctgt == SOURCE) {
	    OldDid.Vnode = rl->dvnode;
	    OldDid.Unique = rl->dunique;
	    NewDid.Vnode = rl->u.u_rename.OtherDirV;
	    NewDid.Unique = rl->u.u_rename.OtherDirU;
	}
	else {
	    NewDid.Vnode = rl->dvnode;
	    NewDid.Unique = rl->dunique;
	    OldDid.Vnode = rl->u.u_rename.OtherDirV;
	    OldDid.Unique = rl->u.u_rename.OtherDirU;
	}
	opv = FindVLE(*vlist, &OldDid);
	if (!opv || !opv->vptr) {
	    SLog(0,  "ChkResRenSem: Old Dir(%x.%x.%x) doesnt exist",
		    OldDid.Volume, OldDid.Vnode, OldDid.Unique);
	    return(EINVAL);
	}
	if (!FID_EQ(&OldDid, &NewDid)) {
	    npv = FindVLE(*vlist, &NewDid);
	    if (!npv || !npv->vptr) {
		SLog(0,  "ChkResRenSem: New Dir(%x.%x.%x) doesnt exist",
			NewDid.Volume, NewDid.Vnode, NewDid.Unique);
		return(EINVAL);
	    }
	}
	else 
	    npv = opv;
    }
    /* source object checks */
    {
	ViceFid tmpFid;
	PDirHandle odh;
	ViceFid spFid;	/* source parent Fid */
	
	SrcFid.Vnode = rl->u.u_rename.rename_src.cvnode;
	SrcFid.Unique = rl->u.u_rename.rename_src.cunique;
	
	odh = VN_SetDirHandle(opv->vptr);
	if (DH_Lookup(odh, rl->u.u_rename.rename_src.oldname, 
		   &tmpFid, CLU_CASE_SENSITIVE) == 0) {
	    SrcNameExists = TRUE;
	    tmpFid.Volume = SrcFid.Volume;
	    SrcNameFidBindingOK = FID_EQ(&tmpFid, &SrcFid);
	}
	VN_PutDirHandle(opv->vptr);
	sv = FindVLE(*vlist, &SrcFid);
	if (sv && sv->vptr && !sv->vptr->delete_me)
	    SrcObjExists = TRUE;
	
	if (SrcObjExists) {
	    spFid.Volume = V_id(volptr);
	    spFid.Vnode = sv->vptr->disk.vparent;
	    spFid.Unique = sv->vptr->disk.uparent;
	    SrcParentPtrOK = FID_EQ(&spFid, &OldDid);
	    if (!SrcParentPtrOK) {
		/* set child's parent to be marked inc */
		SLog(0,  "ChkResRenSem: Parent(%x.%x) on src vnode(%x.%x) not same as parent %x.%x",
			spFid.Vnode, spFid.Unique, SrcFid.Vnode, 
			SrcFid.Unique, OldDid.Vnode, OldDid.Unique);
		vle *spv = FindVLE(*vlist, &spFid);
		CODA_ASSERT(spv);
		CODA_ASSERT(spv->vptr);
		if (spFid.Vnode == 1 && spFid.Unique == 1) 
		    AddILE(*newinclist, ".", 1, 1, 1, 1, vDirectory);
		else {
		    CODA_ASSERT(GetNameInParent(spv->vptr, vlist, volptr, name));
		    AddILE(*newinclist, name, spFid.Vnode, spFid.Unique, 
			   spv->vptr->disk.vparent, spv->vptr->disk.uparent, vDirectory);
		}
	    }
	}
	if (!SrcNameExists || !SrcNameFidBindingOK || 
	    !SrcObjExists || !SrcParentPtrOK) {
	    SLog(0,  "ChkResRenSem Src: NE = %d FBindOK = %d OE = %d PPOK = %d",
		    SrcNameExists, SrcNameFidBindingOK, SrcObjExists, 
		    SrcParentPtrOK);
	    SLog(0,  "ChkResRenSem: Marking both Old(%x.%x) and New dirs(%x.%x) inc",
		    OldDid.Vnode, OldDid.Unique, NewDid.Vnode, NewDid.Unique);
	    errorCode = EINCONS;
	    goto Exit;
	}
    }
    
    /* target object checks */
    {
	if (!rl->u.u_rename.rename_tgt.tgtexisted) {
	    PDirHandle ndh;
	    ViceFid tmpfid;
	    tmpfid.Volume = V_id(volptr);
	    ndh = VN_SetDirHandle (npv->vptr);
	    if (DH_Lookup(ndh, rl->u.u_rename.rename_tgt.newname, 
		       &tmpfid, CLU_CASE_SENSITIVE) == 0) {
		SLog(0,  "ChkResRenSem: Target name %s already exists wrongly",
			rl->u.u_rename.rename_tgt.newname);
		VN_PutDirHandle(npv->vptr);
		errorCode = EINCONS;
		goto Exit;
	    }
	    VN_PutDirHandle(npv->vptr);
	}
	else {
	    /* target is supposed to exist */
	    TgtFid.Vnode = rl->u.u_rename.rename_tgt.TgtVnode;
	    TgtFid.Unique = rl->u.u_rename.rename_tgt.TgtUnique;
	    PDirHandle ndh;
	    ViceFid tmpFid;
	    ViceFid tpFid;	/* target vnode's parent fid */
	    int TgtNameExists, TgtNameFidBindingOK;
	    int TgtObjExists, TgtParentPtrOK;
	    
	    TgtNameExists = FALSE;
	    TgtNameFidBindingOK = FALSE;
	    TgtObjExists = FALSE;
	    TgtParentPtrOK = TRUE;
	    tmpFid.Volume = V_id(volptr);
	    ndh = VN_SetDirHandle(npv->vptr);
	    if (DH_Lookup(ndh, rl->u.u_rename.rename_tgt.newname, 
		       &tmpFid, CLU_CASE_SENSITIVE) == 0) {
		TgtNameExists = TRUE;
		TgtNameFidBindingOK = FID_EQ(&tmpFid, &TgtFid);
	    }
	    VN_PutDirHandle(npv->vptr);
	    tv = FindVLE(*vlist, &TgtFid);
	    if (tv && tv->vptr && !tv->vptr->delete_me)
		TgtObjExists = TRUE;
	    if (TgtObjExists) {
		tpFid.Volume = V_id(volptr);
		tpFid.Vnode = tv->vptr->disk.vparent;
		tpFid.Unique = tv->vptr->disk.uparent;
		TgtParentPtrOK = FID_EQ(&tpFid, &NewDid);
		if (!TgtParentPtrOK) {
		    SLog(0,  "ChkResRenSem: Parent(%x.%x) on tgtvnode(%x.%x)",	
			    tpFid.Vnode, tpFid.Unique, TgtFid.Vnode, TgtFid.Unique);
		    SLog(0,  "    doesn't match Target Dir %x.%x",
			    NewDid.Vnode, NewDid.Unique);
		    vle *tpv = FindVLE(*vlist, &tpFid);
		    if (tpv && tpv->vptr) {
			if (tpFid.Vnode == 1 && tpFid.Unique == 1) 
			    AddILE(*newinclist, ".", 1, 1, 1, 1, vDirectory);
			else {
			    CODA_ASSERT(GetNameInParent(tpv->vptr, vlist, volptr, name));
			    AddILE(*newinclist, name, tpFid.Vnode, tpFid.Unique, 
				   tpv->vptr->disk.vparent, tpv->vptr->disk.uparent, vDirectory);
			}
		    }
		}
	    }
	    if (!TgtNameExists || !TgtNameFidBindingOK || 
		!TgtObjExists || !TgtParentPtrOK) {
		SLog(0,  "ChkResRenSem: Tgt NE %d FBindOK %d OE %d PPOK %d",
			TgtNameExists, TgtNameFidBindingOK, 
			TgtObjExists, TgtParentPtrOK);
		SLog(0,  "ChkResRenSem: Marking both Old(%x.%x) and New dirs(%x.%x) inc",
			OldDid.Vnode, OldDid.Unique, NewDid.Vnode, NewDid.Unique);
		errorCode = EINCONS;
		goto Exit;
	    }
	    /* check for remove update conflicts on target */
	    {
		if (!ISDIR(TgtFid)) {
		    /* file remove/update conflict */
		    CODA_ASSERT(tv->vptr);
		    int res = VV_Cmp(&Vnode_vv(tv->vptr), 
				     &(rl->u.u_rename.rename_tgt.TgtGhost.TgtGhostVV));
		    if (res != VV_EQ && res != VV_SUB) {
			SLog(0,  "ChkResRenSem: RUConflict on target %x.%x",
				TgtFid.Vnode, TgtFid.Unique);
			errorCode = EINCONS;
			goto Exit;
		    }
		}
		else  {
		    RUParm rup(vlist, hvlog, rl->serverid, V_id(volptr));
		    DirRUConf(&rup, rl->u.u_rename.rename_tgt.newname, 
			      TgtFid.Vnode, TgtFid.Unique);
		    SLog(9,  "ChkResRenSem: DirRUConf returns %d",
			    rup.rcode);
		    if (rup.rcode) {
			AddILE(*newinclist, rl->u.u_rename.rename_tgt.newname,
			       TgtFid.Vnode, TgtFid.Unique, NewDid.Vnode, 
			       NewDid.Unique, vDirectory);
			errorCode = EINCONS;
			goto Exit;
		    }
		}
	    }
	}
    }
    
    /* check normal rename semantics */
    if (errorCode = CheckRenameSemantics(NULL, &opv->vptr, &npv->vptr, 
					 &sv->vptr, 
					 rl->u.u_rename.rename_src.oldname,
					 tv ? &tv->vptr : 0, 
					 rl->u.u_rename.rename_tgt.newname, 
					 &volptr, 0, NULL, 
					 NULL, NULL, NULL, NULL, 
					 NULL, NULL, NULL, NULL, 
					 NULL, NULL, 0, 1)) {
	SLog(0,  "ChkResRenSem: Error %d from vicerenamechksemantics ",
		errorCode);
	errorCode = EINCONS;
    }
    
  Exit:
    if (errorCode) {
	CODA_ASSERT(opv);
	CODA_ASSERT(opv->vptr);
	CODA_ASSERT(npv);
	CODA_ASSERT(npv->vptr);
	if (opv->vptr->disk.uniquifier == 1) 
	    AddILE(*newinclist, ".", 1, 1, 1, 1, vDirectory);
	else {
	    CODA_ASSERT(GetNameInParent(opv->vptr, vlist, volptr, name));
	    AddILE(*newinclist, name, OldDid.Vnode, OldDid.Unique, 
		   opv->vptr->disk.vparent, opv->vptr->disk.uparent, vDirectory);
	}
	if (opv != npv) {
	    if (npv->vptr->disk.uniquifier == 1) 
		AddILE(*newinclist, ".", 1, 1, 1, 1, vDirectory);
	    else {
		CODA_ASSERT(GetNameInParent(npv->vptr, vlist, volptr, name));
		AddILE(*newinclist, name, NewDid.Vnode, NewDid.Unique, 
		       npv->vptr->disk.vparent, npv->vptr->disk.uparent, vDirectory);
	    }
	}
    }
    else {
	*srcv = sv;
	*tgtv = tv;
	*srcdv = opv;
	*tgtdv = npv;
    }
    SLog(9,  "ChkResRenSem: returns %d", errorCode);
    return(errorCode);
}
/* CleanRenameTarget
 * If it is a non-empty directory then remove the children
 */
static int CleanRenameTarget(rlent *rl, dlist *vlist, Volume *volptr,
			      VolumeId VSGVolnum, olist *hvlog, int *blocks) {
    SLog(9,  "Entering CleanRenameTarget");
    if (!rl->u.u_rename.rename_tgt.tgtexisted)
	return(0);
    ViceFid tFid;
    tFid.Volume = V_id(volptr);
    tFid.Vnode = rl->u.u_rename.rename_tgt.TgtVnode;
    tFid.Unique = rl->u.u_rename.rename_tgt.TgtUnique;
    
    if (!ISDIR(tFid)) return(0);
    vle *tv = FindVLE(*vlist, &tFid);
    if (!tv || !tv->vptr) {
	SLog(0,  "CleanRenameTarget: Couldn't find target(%x.%x) obj in vlist",
		tFid.Vnode, tFid.Unique);
	return(0);
    }
    
    PDirHandle	tdh;
    tdh = VN_SetDirHandle(tv->vptr);
    if (DH_IsEmpty(tdh)) {
	SLog(0,  "CleanRenameTarget: Target %x.%x is already empty",
		tFid.Vnode, tFid.Unique);
	VN_PutDirHandle(tv->vptr);
	return(0);
    }
    
    TreeRmBlk pkdparm;
    pkdparm.init(0, VSGVolnum, volptr, 0, &rl->storeid, vlist, 
		 1, hvlog, rl->serverid, blocks);
    DH_EnumerateDir(tdh, PerformTreeRemoval, (void *)&pkdparm);
    VN_PutDirHandle(tv->vptr);
    return(0);
}

void SpoolRenameLogRecord(int opcode, vle *sv, vle *tv, vle *sdv, 
			  vle *tdv, Volume *volptr, char *OldName, 
			  char *NewName, ViceStoreId *StoreId) {
    int src_ind = -1;
    int tgt_ind = -1;
    ViceFid TgtFid, SrcFid, OldDid, NewDid;
    TgtFid.Volume = OldDid.Volume = NewDid.Volume = SrcFid.Volume = V_id(volptr);
    if (tv) {
	TgtFid.Vnode = tv->vptr->vnodeNumber;
	TgtFid.Unique = tv->vptr->disk.uniquifier;
    }
    else {
	TgtFid.Vnode = 0;
	TgtFid.Unique = 0;
    }
    
    OldDid.Vnode = sdv->vptr->vnodeNumber;
    OldDid.Unique = sdv->vptr->disk.uniquifier;
    NewDid.Vnode = tdv->vptr->vnodeNumber;
    NewDid.Unique = tdv->vptr->disk.uniquifier;
    SrcFid.Vnode = sv->vptr->vnodeNumber;
    SrcFid.Unique = sv->vptr->disk.uniquifier;
    int SameParent = FID_EQ(&OldDid, &NewDid);
    if (tv) {
	/* probably want to attach target directory's ghost log only
	   to the target parent directory's log record for rename -
	   prevent aliasing of log - causes problem while deallocation */
	if (ISDIR(TgtFid)) {
	    VNResLog *vnlog;
	    pdlist *pl = GetResLogList(tv->vptr->disk.vol_index, 
				       TgtFid.Vnode, TgtFid.Unique, 
				       &vnlog);
	    CODA_ASSERT(pl != NULL);
	    if (SameParent)
		src_ind = InitVMLogRecord(V_volumeindex(volptr), &OldDid, 
					 StoreId, opcode, SOURCE, 
					 OldName, SrcFid.Vnode, SrcFid.Unique, 
					 &sv->vptr->disk.versionvector, 
					 NewDid.Vnode, NewDid.Unique,
					 NewName, 1, TgtFid.Vnode, TgtFid.Unique,
					 pl->head, pl->count());
	    else {
		src_ind = InitVMLogRecord(V_volumeindex(volptr), &OldDid, 
					  StoreId, opcode, SOURCE, 
					  OldName, SrcFid.Vnode, SrcFid.Unique, 
					  &sv->vptr->disk.versionvector, 
					  NewDid.Vnode, NewDid.Unique,
					  NewName, 1, TgtFid.Vnode, TgtFid.Unique,
					  -1, 0);
		
		tgt_ind = InitVMLogRecord(V_volumeindex(volptr), &NewDid,
					  StoreId, opcode, TARGET,
					  OldName, SrcFid.Vnode, 
					  SrcFid.Unique, 
					  &sv->vptr->disk.versionvector, 
					  OldDid.Vnode, OldDid.Unique,
					  NewName, 1, TgtFid.Vnode, 
					  TgtFid.Unique,
					  pl->head, pl->count());
	    }
	}
	else {
	    src_ind = InitVMLogRecord(V_volumeindex(volptr), &OldDid, 
				      StoreId, opcode, SOURCE, 
				      OldName, SrcFid.Vnode, SrcFid.Unique, 
				      &sv->vptr->disk.versionvector, 
				      NewDid.Vnode, NewDid.Unique,
				      NewName, 1, TgtFid.Vnode, TgtFid.Unique,
				      &tv->vptr->disk.versionvector);
	    if (!SameParent)
		tgt_ind = InitVMLogRecord(V_volumeindex(volptr), &NewDid,
					  StoreId, opcode, TARGET,
					  OldName, SrcFid.Vnode, 
					  SrcFid.Unique, 
					  &sv->vptr->disk.versionvector, 
					  OldDid.Vnode, OldDid.Unique,
					  NewName, 1, TgtFid.Vnode, 
					  TgtFid.Unique,
					  &tv->vptr->disk.versionvector);
	}
	
    }
    else {
	src_ind = InitVMLogRecord(V_volumeindex(volptr), &OldDid, 
				  StoreId, opcode, SOURCE, 
				  OldName, SrcFid.Vnode, SrcFid.Unique, 
				  &sv->vptr->disk.versionvector, 
				  NewDid.Vnode, NewDid.Unique,
				  NewName, 0);
	if (!SameParent)
	    tgt_ind = InitVMLogRecord(V_volumeindex(volptr), &NewDid, 
				      StoreId, opcode, TARGET, 
				      OldName, SrcFid.Vnode, SrcFid.Unique, 
				      &sv->vptr->disk.versionvector, 
				      OldDid.Vnode, OldDid.Unique,
				      NewName, 0);
    }
    CODA_ASSERT(src_ind != -1);
    sle *SLE = new sle(src_ind);
    sdv->sl.append(SLE);
    if (!SameParent) {
	CODA_ASSERT(tgt_ind != -1);
	sle *tgtSLE = new sle(tgt_ind);
	tdv->sl.append(tgtSLE);
    }
}

void MarkObjInc(ViceFid *fid, Vnode *vptr) {
    VolumeId VSGVolnum = fid->Volume;
    if (ReverseXlateVid(&VSGVolnum))  {
	CodaBreakCallBack(0, fid, VSGVolnum);
    }
    else {
	BreakCallBack(0, fid);
    }
    
    SetIncon(vptr->disk.versionvector);
}

static int PerformResOp(rlent *r, dlist *vlist, olist *hvlog,
			 vle *pv, Volume *volptr, 
			 VolumeId VSGVolnum, 
			 int *blocks) {
    
    SLog(9,  "Entering PerformResOp: %s ", 
	    PRINTOPCODE(r->opcode));
    long errorCode = 0;
    *blocks = 0;
    char *name = ExtractNameFromRLE(r);
    CODA_ASSERT(name);
    ViceFid cFid;
    /* XXXX BE CAREFUL WITH CHILD FIDS AND RENAMES */
    ExtractChildFidFromRLE(r, &cFid);
    cFid.Volume = V_id(volptr);
    
    /* perform the operation */
    switch (r->opcode) {
      case ViceNewStore_OP:
      case ResolveViceNewStore_OP:
	CODA_ASSERT(0);
	break;
      case ResolveViceRemove_OP:
      case ViceRemove_OP:
	{
	    SLog(9,  "PerformResOP: Removing child %s(%x.%x)",
		    name, cFid.Vnode, cFid.Unique);
	    vle *cv = FindVLE(*vlist, &cFid);
	    CODA_ASSERT(cv);
	    CODA_ASSERT(cv->vptr);
	    
	    /* perform remove */
	    {
		PerformRemove(NULL, VSGVolnum, volptr, 
        		      pv->vptr, cv->vptr, name, 
			      pv->vptr->disk.unixModifyTime,
			      0, &r->storeid , &pv->d_cinode, 
			      blocks);
		if (cv->vptr->delete_me) {
		    int tblocks = (int) -nBlocks(cv->vptr->disk.length);
		    CODA_ASSERT(AdjustDiskUsage(volptr, tblocks) == 0);
		    *blocks += tblocks;
		    cv->f_sinode = cv->vptr->disk.inodeNumber;
		    cv->vptr->disk.inodeNumber = 0;
		}
	    }
	    /* spool log record */
	    {
		SLog(9,  "PerformResOp: Spooling log record Remove(%s)",
			name);
		int ind;
		ind = InitVMLogRecord(V_volumeindex(volptr), 
				      &pv->fid, &r->storeid,
				      ResolveViceRemove_OP,
				      name, cv->fid.Vnode,
				      cv->fid.Unique, 
				      &cv->vptr->disk.versionvector);
		sle *SLE = new sle(ind);
		pv->sl.append(SLE);
	    }
	}
	break;
      case ResolveViceCreate_OP:
      case ViceCreate_OP:
	{
	    SLog(9,  "PerformResOP: creating child %s(%x.%x)",
		    name, cFid.Vnode, cFid.Unique);
	    /* create the vnode */
	    vle *cv = AddVLE(*vlist, &cFid);
	    CODA_ASSERT(cv->vptr == 0);
	    if (errorCode = AllocVnode(&cv->vptr, volptr, (ViceDataType)vFile, &cFid,
				       &pv->fid, pv->vptr->disk.owner,
				       1, blocks)) {
		SLog(0,  "PerformResOP: Error %d in AllocVnode",
			errorCode);
		return(errorCode);
	    }
	    int tblocks = 0;
	    PerformCreate(NULL, VSGVolnum, volptr, pv->vptr,
			  cv->vptr, name, 
			  pv->vptr->disk.unixModifyTime,
			  pv->vptr->disk.modeBits,
			  0, &r->storeid, &pv->d_cinode, &tblocks);
	    *blocks += tblocks;
	    cv->vptr->disk.dataVersion = 1;
	    cv->f_finode = icreate((int) V_device(volptr), 0, (int) V_id(volptr),
				   (int) cv->vptr->vnodeNumber,
				   (int) cv->vptr->disk.uniquifier,
				   (int) cv->vptr->disk.dataVersion);
	    CODA_ASSERT(cv->f_finode > 0);
	    cv->vptr->disk.inodeNumber = cv->f_finode;
	    
	    /* append log record */
	    {
		SLog(9,  "PerformResOp: Spooling log record Create(%s)",
			name);
		int ind;
		ind = InitVMLogRecord(V_volumeindex(volptr), &pv->fid, &r->storeid,
				      ResolveViceCreate_OP,
				      name, cFid.Vnode, cFid.Unique);
		sle *SLE = new sle(ind);
		pv->sl.append(SLE);
	    }
	}
	break;
      case ResolveViceRename_OP:
      case ViceRename_OP:
	CODA_ASSERT(0);
	break;
      case ResolveViceSymLink_OP:
      case ViceSymLink_OP:
	{
	    SLog(9,  "PerformResOP: Creating SymLink %s(%x.%x)",
		    name, cFid.Vnode, cFid.Unique);
	    /* create the vnode */
	    vle *cv = AddVLE(*vlist, &cFid);
	    CODA_ASSERT(cv->vptr == 0);
	    if (errorCode = AllocVnode(&cv->vptr, volptr, (ViceDataType)vSymlink, &cFid,
				       &pv->fid, pv->vptr->disk.owner,
				       1, blocks)) {
		SLog(0,  "PerformResOP: Error %d in AllocVnode(symlink)",
			errorCode);
		return(errorCode);
	    }
	    int tblocks = 0;
	    PerformSymlink(NULL, VSGVolnum, volptr, pv->vptr,
			   cv->vptr, name, 0, 0,
			   pv->vptr->disk.unixModifyTime,
			   pv->vptr->disk.modeBits,
			   0, &r->storeid, &pv->d_cinode, &tblocks);
	    *blocks += tblocks;

	    /* create the inode */
	    cv->vptr->disk.dataVersion = 1;
	    cv->f_finode = icreate((int) V_device(volptr), 0, (int) V_id(volptr),
				   (int) cv->vptr->vnodeNumber,
				   (int) cv->vptr->disk.uniquifier,
				   (int) cv->vptr->disk.dataVersion);
	    CODA_ASSERT(cv->f_finode > 0);
	    cv->vptr->disk.inodeNumber = cv->f_finode;
	    
	    /* append log record */
	    {
		SLog(9,  "PerformResOp: Spooling log record SymLink(%s)",
			name);
		int ind;
		ind = InitVMLogRecord(V_volumeindex(volptr), &pv->fid, &r->storeid,
				      ResolveViceSymLink_OP,
				      name, cFid.Vnode, cFid.Unique);
		sle *SLE = new sle(ind);
		pv->sl.append(SLE);
	    }
	}
	break;
      case ResolveViceLink_OP:
      case ViceLink_OP:
	{
	    SLog(9,  "PerformResOP: Creating Link %s(%x.%x)",
		    name, cFid.Vnode, cFid.Unique);
	    vle *cv = FindVLE(*vlist, &cFid);
	    if (!cv || !cv->vptr) {
		SLog(0,  "PerformResOp: CreateL %x.%x doesnt exist",
			cFid.Vnode, cFid.Unique);
		return(EINVAL);
	    }

	    /* add name to parent */
	    PerformLink(0, VSGVolnum, volptr, pv->vptr,
			cv->vptr, name, 
			cv->vptr->disk.unixModifyTime,
			0, &r->storeid, &pv->d_cinode,
			blocks);
	    /* spool log record */
	    {
		SLog(9,  "PerformResOp: Spooling log record Link(%s)",
			name);
		int ind;
		ind = InitVMLogRecord(V_volumeindex(volptr), &pv->fid, &r->storeid,
				      ResolveViceLink_OP,
				      name, cFid.Vnode, cFid.Unique);
		sle *SLE = new sle(ind);
		pv->sl.append(SLE);
	    }
	}
	break;
      case ResolveViceMakeDir_OP:
      case ViceMakeDir_OP:
	{
	    SLog(9,  "PerformResOP: MakeDir %s(%x.%x)",
		    name, cFid.Vnode, cFid.Unique);
	
	    Vnode *cvptr;
	    /* allocate the vnode */
	    if (errorCode = AllocVnode(&cvptr, volptr, (ViceDataType)vDirectory,
				       &cFid, &pv->fid, 
				       pv->vptr->disk.owner,
				       1, blocks)) {
		SLog(0,  "PerformResOP: Error %d in AllocV(mkdir)",
			errorCode);
		return(errorCode);
	    }
	    vle *cv = AddVLE(*vlist, &cFid);
	    cv->vptr = cvptr;
	    cv->d_inodemod = 1;
	    
	    /* make the directory */
	    int tblocks = 0;
	    PerformMkdir(0, VSGVolnum, volptr, pv->vptr,
			 cv->vptr, name, 
			 pv->vptr->disk.unixModifyTime,
			 pv->vptr->disk.modeBits,
			 0, &r->storeid, &pv->d_cinode, &tblocks);
	    CODA_ASSERT(DC_Dirty(cvptr->dh));
	    *blocks += tblocks;
	    
	    /* spool log record - NOTE: FOR MKDIR SPOOL A REGULAR MKDIR RECORD 
	       NOT A ResolveMkdir RECORD */
	    {
		SLog(9,  "PerformResOp: Spooling log record MkDir(%s)",
			name);
		int ind;
		ind = InitVMLogRecord(V_volumeindex(volptr), &pv->fid, 
				      &r->storeid, ResolveViceMakeDir_OP, 
				      name, cFid.Vnode, cFid.Unique);
		sle *SLE = new sle(ind);
		pv->sl.append(SLE);

		/* spool a log record for child too */
		ind = InitVMLogRecord(V_volumeindex(volptr), &cv->fid, 
				     &r->storeid, ViceMakeDir_OP,
				     ".", cFid.Vnode, cFid.Unique);
		SLE = new sle(ind);
		cv->sl.append(SLE);
	    }
	}
	break;
      case ResolveViceRemoveDir_OP:
      case ViceRemoveDir_OP:
	{
	    SLog(9,  "PerformResOP: Removing child dir %s(%x.%x)",
		    name, cFid.Vnode, cFid.Unique);
	    vle *cv = FindVLE(*vlist, &cFid);
	    CODA_ASSERT(cv);
	    CODA_ASSERT(cv->vptr);
	    
	    PDirHandle dh;
	    dh = VN_SetDirHandle(cv->vptr);
	    /* first make the directory empty */
	    {
		if (! DH_IsEmpty(dh)) {
		    /* remove children first */
		    TreeRmBlk	pkdparm;
		    pkdparm.init(0, VSGVolnum, volptr, 0, &r->storeid, 
				 vlist, 1, hvlog, r->serverid, blocks);
		    DH_EnumerateDir(dh, PerformTreeRemoval, (void *)&pkdparm);
		    CODA_ASSERT(DC_Dirty(cv->vptr->dh)); 
		    
		}
		CODA_ASSERT(DH_IsEmpty(dh));
	    }
	    VN_PutDirHandle(cv->vptr);
	    int tblocks = 0;
	    /* remove the empty directory */
	    {
		tblocks = 0;
		PerformRmdir(0, VSGVolnum, volptr, 
			     pv->vptr, cv->vptr, name, 
			     pv->vptr->disk.unixModifyTime,
			     0, &r->storeid, &pv->d_cinode, &tblocks);
		*blocks += tblocks;
		CODA_ASSERT(cv->vptr->delete_me);
		tblocks = (int) -nBlocks(cv->vptr->disk.length);
		CODA_ASSERT(AdjustDiskUsage(volptr, tblocks) == 0);
		*blocks += tblocks;
	    }

	    /* spool log record - should have spooled records recursively */
	    {	
		SLog(9,  "PerformResOp: Spooling Log Record RmDir(%s)",
			name);
		VNResLog *vnlog;
		pdlist *pl = GetResLogList(cv->vptr->disk.vol_index,
					   cv->fid.Vnode, cv->fid.Unique,
					   &vnlog);
		CODA_ASSERT(pl != NULL);
		int ind;
		ind = InitVMLogRecord(V_volumeindex(volptr), &pv->fid, 
				      &r->storeid, ResolveViceRemoveDir_OP,
				      name, cv->fid.Vnode, cv->fid.Unique,
				      (int)(pl->head), pl->cnt + cv->sl.count(), 
				      &(vnlog->LCP), 
				      &cv->vptr->disk.versionvector.StoreId);
		sle *SLE = new sle(ind);
		pv->sl.append(SLE);
	    }
	}
	break;
      default:
	SLog(0,  "Illegal Opcode for performresop %d", r->opcode);
	CODA_ASSERT(0);
	break;
    }

    SLog(9,  "PerformResOp: Returns %d", errorCode);
    return(errorCode);
}

/* Resolve Rename across directories i.e. Source and Target Directory are 
 * different.  
 * For now just mark both source and target directories inconsistent.
 */
static int ResolveCrossDirRename(rlent *rl, ViceFid *dFid, Volume *volptr, 
				  dlist *vlist, dlist *inclist) {

    ViceFid OldDid, NewDid;
    /* get fids */
    {
	OldDid.Volume = NewDid.Volume = dFid->Volume;
	if (rl->u.u_rename.srctgt == SOURCE) {
	    OldDid.Vnode = rl->dvnode;
	    OldDid.Unique = rl->dunique;
	    NewDid.Vnode = rl->u.u_rename.OtherDirV;
	    NewDid.Unique = rl->u.u_rename.OtherDirU;
	}
	else {
	    NewDid.Vnode = rl->dvnode;
	    NewDid.Unique = rl->dunique;
	    OldDid.Vnode = rl->u.u_rename.OtherDirV;
	    OldDid.Unique = rl->u.u_rename.OtherDirU;
	}
    }
    /* mark inc */
    {
	vle *odv = FindVLE(*vlist, &OldDid);
	if (odv && odv->vptr) {
	    MarkObjInc(&OldDid, odv->vptr);
	    if (OldDid.Unique == 1 && OldDid.Vnode == 1) {
		/* marking root of volume inconsistent */
		AddILE(*inclist, ".", 1, 1, 1, 1, vDirectory);
	    }
	    else {
		char	name[MAXNAMLEN];
		CODA_ASSERT(GetNameInParent(odv->vptr, vlist, volptr, name));
		AddILE(*inclist, name, OldDid.Vnode, OldDid.Unique, 
		       odv->vptr->disk.vparent, odv->vptr->disk.uparent, vDirectory);
	    }
	}
	vle *ndv = FindVLE(*vlist, &NewDid);
	if (ndv && ndv->vptr) {
	    MarkObjInc(&NewDid, ndv->vptr);
	    if (NewDid.Vnode == 1 && NewDid.Unique == 1) {
		/* marking root of volume inconsistent */
		AddILE(*inclist, ".", 1, 1, 1, 1, vDirectory);
	    }
	    else {
		char name[MAXNAMLEN];
		CODA_ASSERT(GetNameInParent(ndv->vptr, vlist, volptr, name));
		AddILE(*inclist, name, NewDid.Vnode, NewDid.Unique, 
		       ndv->vptr->disk.vparent, ndv->vptr->disk.uparent, vDirectory);
	    }
	}
    }
    return(0);
}
/* Create an Object which we are going to mark inconsistent
 * 	If the object doesnt exist already then create it first 
 */
int CreateObjToMarkInc(Volume *vp, ViceFid *dFid, ViceFid *cFid, 
		       char *name, int vntype, dlist *vlist, int *blocks) {
    
    vle *pv = 0;
    vle *cv = 0;
    *blocks = 0;
    long errorCode = 0;
    SLog(9,
	   "CreateIncObject: Entering (parent %x.%x child %x.%x name %s type %d)",
	   dFid->Vnode, dFid->Unique, cFid->Vnode, cFid->Unique, name, vntype);

    /* get vnode of parent */
    {
	pv = FindVLE(*vlist, dFid);
	if (!pv || !pv->vptr) {
	    SLog(0,  "CreateIncObject: Parent(%x.%x) doesnt exist!", 
		    dFid->Vnode, dFid->Unique);
	    return(EINVAL);
	}
    }
    
    {
	
	PDirHandle dh;
	ViceFid newfid;
	dh = VN_SetDirHandle(pv->vptr);
	if (DH_Lookup(dh, name, &newfid, CLU_CASE_SENSITIVE) == 0) {
	    // parent has name 
	    if ((newfid.Vnode == cFid->Vnode) && (newfid.Unique == cFid->Unique)){
		cv = FindVLE(*vlist, cFid);
		CODA_ASSERT(cv); 
		CODA_ASSERT(cv->vptr);
		CODA_ASSERT((cv->vptr->disk.linkCount > 0) && !cv->vptr->delete_me);
		SLog(9,  "CreateIncObj Child (%x.%x)already exists",
		       cFid->Vnode, cFid->Unique);
		VN_PutDirHandle(pv->vptr);
		return(0);
	    }
	    else {
		// parent has name but different object 
		SLog(0,  
		       "CreateIncObject: Parent %x.%x already has name %s",
		       dFid->Vnode, dFid->Unique, name);
		SLog(0,  
		       "              with fid %x.%x - cant create %x.%x",
		       newfid.Vnode, newfid.Unique, cFid->Vnode, cFid->Unique);
		VN_PutDirHandle(pv->vptr);
		return(EINVAL);
	    }
	}
	VN_PutDirHandle(pv->vptr);
	// name doesnt exist - look for object 
	{
	    cv = FindVLE(*vlist, cFid);
	    if (cv && cv->vptr) {
		// object exists 
		if ((cv->vptr->disk.vparent != dFid->Vnode) || 
		    (cv->vptr->disk.uparent != dFid->Unique)) {
		    SLog(0,  
			   "CreateIncObject: Object %x.%x already exists in %x.%x !",
			    cFid->Vnode, cFid->Unique, cv->vptr->disk.vparent, 
			    cv->vptr->disk.uparent);
		    return(EINVAL);
		}

		// object exists in same diretory with different name
		if (vntype != vFile) {
		    // cannot create link to object 
		    SLog(0, 
			   "CreateIncObject: Object %x.%x already exists in %x.%x !",
			    cFid->Vnode, cFid->Unique, dFid->Vnode, dFid->Unique);
		    return(EINVAL);
		}

		// have to create a link 
		CODA_ASSERT(vntype == vFile);
		VolumeId VSGVolnum = V_id(vp);
		if (!ReverseXlateVid(&VSGVolnum)) {
		    SLog(0,  "CreateIncObject: Couldnt RevXlateVid %x",
			    VSGVolnum);
		    return(EINVAL);
		}
		if (errorCode = 
		    CheckLinkSemantics(NULL, &pv->vptr, &cv->vptr, name, &vp, 
				       0, NULL, NULL, NULL, NULL, NULL, 0)) {
		    SLog(0,  "CreateObjToMarkInc: Error 0x%x to create link %s",
			    errorCode, name);
		    return(errorCode);
		}
		ViceStoreId stid;
		AllocStoreId(&stid);
		PerformLink(NULL, VSGVolnum, vp, pv->vptr, cv->vptr, 
			    name, time(0), 0, &stid, &pv->d_cinode, blocks);
		if (cv->vptr->delete_me) {
		    /* it was deleted before the link was done */
		    SLog(0,  "Undeleting Vnode %s (%x.%x)",
			    name, cFid->Vnode, cFid->Unique);
		    CODA_ASSERT(cv->vptr->disk.linkCount);
		    cv->vptr->delete_me = 0;
/*
		    cv->vptr->disk.dataVersion = 1;
		    cv->f_finode = icreate(V_device(vp), 0, V_id(vp), 
					   cv->vptr->vnodeNumber, 
					   cv->vptr->disk.uniquifier, 
					   cv->vptr->disk.dataVersion);
		    CODA_ASSERT(cv->f_finode > 0);
		    cv->vptr->disk.inodeNumber = cv->f_finode;
*/
		    CODA_ASSERT(cv->f_sinode > 0);
		    cv->vptr->disk.inodeNumber = cv->f_sinode;
		    cv->f_sinode = 0;
		    /* append log record */	
		    if (AllowResolution && V_VMResOn(vp)) {
			SLog(9,  
			       "CreateIncObj: Spooling log record Create(%s)",
			       name);
			int ind;
			ind = InitVMLogRecord(V_volumeindex(vp), &pv->fid, 
					      &stid, ResolveViceCreate_OP,
					      name, cFid->Vnode, cFid->Unique);
			sle *SLE = new sle(ind);
			pv->sl.append(SLE);
		    }
		    if (AllowResolution && V_RVMResOn(vp)) {
			// spool log record for recoverable res logs 
			if (errorCode = SpoolVMLogRecord(vlist, pv->vptr, 
							 vp, &stid, ResolveViceCreate_OP, 
							 name, cFid->Vnode, cFid->Unique))
			    SLog(0, 
				   "CreateObjToMarkInc: Error %d during SpoolVMLogRecord\n",
				   errorCode);
		    }
		}
		return(errorCode);
	    }
	}
	/* object is missing too - create the object */
	{
	    Vnode *cvptr = 0;
	    int tblocks = 0;
	    long errorCode = 0;
	    if (errorCode = AllocVnode(&cvptr, vp, (ViceDataType)vntype,
				       cFid, dFid, pv->vptr->disk.owner,
				       1, &tblocks)) {
		SLog(0,  "CreateIncObj: Error %d in AllocVnode",
			errorCode);
		return(errorCode);
	    }
	    cv = AddVLE(*vlist, cFid);
	    cv->vptr = cvptr;
	    if ( cvptr->disk.type == vDirectory )
		cv->d_inodemod = 1;
	    *blocks += tblocks;

	    /* create name in parent */
	    {
		VolumeId VSGVolnum = V_id(vp);
		if (!ReverseXlateVid(&VSGVolnum)) {
		    SLog(0,  "CreateIncObject: Couldnt RevXlateVid %x",
			    VSGVolnum);
		    return(EINVAL);
		}
		ViceStoreId stid;
		AllocStoreId(&stid);
		int tblocks = 0;
		if (cv->vptr->disk.type == vFile) {
		    if (errorCode = CheckCreateSemantics(NULL, &pv->vptr, 
							 &cv->vptr, name, &vp, 
							 0, NULL, NULL, NULL, 
							 NULL, NULL, 0)) {
			SLog(0,  "Error %d in CheckCreateSem(%x.%x %s)", 
				errorCode, cFid->Vnode, cFid->Unique, name);
			return(errorCode);
		    }
		    PerformCreate(NULL, VSGVolnum, vp, pv->vptr,
				  cv->vptr, name, 
				  pv->vptr->disk.unixModifyTime,
				  pv->vptr->disk.modeBits,
				  0, &stid, &pv->d_cinode, &tblocks);
		    *blocks += tblocks;
		    cv->vptr->disk.dataVersion = 1;
		    cv->f_finode = icreate((int) V_device(vp), 0, (int) V_id(vp),
					   (int) cv->vptr->vnodeNumber,
					   (int) cv->vptr->disk.uniquifier,
					   (int) cv->vptr->disk.dataVersion);
		    CODA_ASSERT(cv->f_finode > 0);
		    cv->vptr->disk.inodeNumber = cv->f_finode;
		    
		    // spool log record 
		    if (AllowResolution && V_VMResOn(vp)) {
			int ind;
			ind = InitVMLogRecord(V_volumeindex(vp),
					      dFid, &stid, ResolveViceCreate_OP,
					      name, cFid->Vnode, cFid->Unique);
			sle *SLE = new sle(ind);
			pv->sl.append(SLE);
		    }
		    if (AllowResolution && V_RVMResOn(vp)) {
			if (errorCode = SpoolVMLogRecord(vlist, pv->vptr, vp, 
							 &stid, ResolveViceCreate_OP, 
							 name, cFid->Vnode, cFid->Unique))
			    SLog(0, 
				   "CreateObjToMarkInc: Error %d during SpoolVMLogRecord\n",
				   errorCode);

		    }
		}
		else if (cv->vptr->disk.type == vSymlink) {
		    if (errorCode = CheckSymlinkSemantics(NULL, &pv->vptr, 
							  &cv->vptr, name, &vp,
							  0, NULL, NULL, NULL,
							  NULL, NULL, 0)) {
			SLog(0,  "Error %d in CheckSymlinkSem(%x.%x %s)",
				errorCode, cFid->Vnode, cFid->Unique, name);
			return(errorCode);
		    }
		    int tblocks = 0;
		    PerformSymlink(NULL, VSGVolnum, vp, pv->vptr,
				   cv->vptr, name, 0, 0, 
				   pv->vptr->disk.unixModifyTime,
				   pv->vptr->disk.modeBits,
				   0, &stid, &pv->d_cinode, &tblocks);
		    *blocks += tblocks;
		    cv->vptr->disk.dataVersion = 1;
		    cv->f_finode = icreate((int) V_device(vp), 0, (int) V_id(vp),
					   (int) cv->vptr->vnodeNumber,
					   (int) cv->vptr->disk.uniquifier,
					   (int) cv->vptr->disk.dataVersion);
		    CODA_ASSERT(cv->f_finode > 0);
		    cv->vptr->disk.inodeNumber = cv->f_finode;
		    
		    if (AllowResolution && V_VMResOn(vp)) {
			int ind;
			ind = InitVMLogRecord(V_volumeindex(vp),
					      dFid, &stid, ResolveViceSymLink_OP,
					      name, cFid->Vnode, cFid->Unique);
			sle *SLE = new sle(ind);
			pv->sl.append(SLE);
		    }
		    if (AllowResolution && V_RVMResOn(vp)) {
			if (errorCode = SpoolVMLogRecord(vlist, pv->vptr, vp, 
							 &stid, 
							 ResolveViceSymLink_OP, 
							 name, cFid->Vnode, cFid->Unique)) 
			    SLog(0, 
				   "CreateObjToMarkInc(symlink): Error %d in SpoolVMLogRecord\n",
				   errorCode);
		    }
		}
		else {
		    CODA_ASSERT(cv->vptr->disk.type == vDirectory);
		    if (errorCode = CheckMkdirSemantics(NULL, &pv->vptr, 
							&cv->vptr, name, &vp,
							0, NULL, NULL, NULL, 
							NULL, NULL, 0)) {
			SLog(0,  "Error %d in CheckMkdirSem(%x.%x %s)",
				errorCode, cFid->Vnode, cFid->Unique, name);
			return(errorCode);
		    }
		    int tblocks = 0;
		    PerformMkdir(NULL, VSGVolnum, vp, pv->vptr,
				 cv->vptr, name,
				 pv->vptr->disk.unixModifyTime,
				 pv->vptr->disk.modeBits,
				 0, &stid, &pv->d_cinode, &tblocks);
		    *blocks += tblocks;
		    if (AllowResolution && V_VMResOn(vp)) {
			int ind;
			ind = InitVMLogRecord(V_volumeindex(vp),
					      dFid, &stid, ResolveViceMakeDir_OP,
					      name, cFid->Vnode, cFid->Unique);
			sle *SLE = new sle(ind);
			pv->sl.append(SLE);
			
			ind = InitVMLogRecord(V_volumeindex(vp),
					      cFid, &stid, ResolveViceMakeDir_OP,
					      ".", cFid->Vnode, cFid->Unique);
			SLE = new sle(ind);
			cv->sl.append(SLE);
		    }
		    if (AllowResolution && V_RVMResOn(vp)) {
			if (errorCode = SpoolVMLogRecord(vlist, pv->vptr, vp, &stid,
							 ResolveViceMakeDir_OP, name, 
							 cFid->Vnode, cFid->Unique)) 
			    SLog(0, 
				   "CreateObjToMarkInc(Mkdir): Error %d during SpoolVMLogRecord for parent\n",
				   errorCode);
		    }
		}
	    }
	}
	return(errorCode);
    }
}

int GetPhase2Objects(ViceFid *pfid, dlist *vlist, dlist *inclist,
			     Volume **volptr) 
{
    long errorCode = 0;
    
    /* get volume */
    if (errorCode = GetVolObj(pfid->Volume, volptr, 
			      VOL_NO_LOCK, 0, 0)) {
	SLog(0,  "GetPhase2Objects: Error %d in Getting volume (Parent)",
		errorCode);
	return(errorCode);
    }
    /* need vnode of parent */
    {
	if (ObjectExists(V_volumeindex(*volptr), vLarge, 
			 vnodeIdToBitNumber(pfid->Vnode), pfid->Unique, NULL))
	    AddVLE(*vlist, pfid);
	else {
	    SLog(0,  "GetPhase2Objects: Parent (%x.%x) is missing",
		    pfid->Vnode, pfid->Unique);
	    return(EINVAL);
	}
    }

    /* add existing vnode fids to vlist */
    {
	dlist_iterator next(*inclist);
	ilink *il;
	int count = 0;
	while(il = (ilink *)next()) {
	    count++;
	    if ((count & Yield_GetP2ObjFids_Mask) == 0) 
		PollAndYield();
	    ViceFid cfid;
	    cfid.Volume = pfid->Volume;
	    cfid.Vnode = il->vnode;
	    cfid.Unique = il->unique;
	    ViceFid vpfid;
	    vpfid.Volume = pfid->Volume;

	    if (ObjectExists(V_volumeindex(*volptr), 
			     ISDIR(cfid) ? vLarge : vSmall,
			     vnodeIdToBitNumber(cfid.Vnode),
			     cfid.Unique, &vpfid)) {
		AddVLE(*vlist, &cfid);
		ViceFid ipfid;
		ipfid.Volume = pfid->Volume;
		ipfid.Vnode = il->pvnode;
		ipfid.Unique = il->punique;
		
		if (vpfid.Vnode != pfid->Vnode || 
		    vpfid.Unique != pfid->Unique)
		    AddVLE(*vlist, &vpfid);
		if (vpfid.Vnode != ipfid.Vnode || 
		    vpfid.Unique != ipfid.Unique)
		    if ((ipfid.Vnode != pfid->Vnode ||
			 ipfid.Unique != pfid->Unique) &&
			ObjectExists(V_volumeindex(*volptr),
				     ISDIR(ipfid) ? vLarge : vSmall,
				     vnodeIdToBitNumber(ipfid.Vnode),
				     ipfid.Unique, NULL))
			AddVLE(*vlist, &ipfid);
	    }
	}
    }

    /* get existing objects in fid order */
    {	
	int count = 0;
	if (vlist->count() > 0) {
	    dlist_iterator next(*vlist);
	    vle *v;
	    while (v = (vle *)next()) {
		count++;
		if ((count & Yield_GetP2Obj_Mask) == 0)
		    PollAndYield();
		SLog(9,  "GetPhase2Objects: acquiring (%x.%x.%x)",
			v->fid.Volume, v->fid.Vnode, v->fid.Unique);
		if (errorCode = GetFsObj(&v->fid, volptr, &v->vptr, 
					 WRITE_LOCK, NO_LOCK, 1, 0, 0)) {
		    SLog(0,  "GetPhase2Objects: Error %d in getting object %x.%x",
			    errorCode, v->fid.Vnode, v->fid.Unique);
		    return(errorCode);
		}
	    }
	}
    }
    SLog(9,  "GetPhase2Objects: returns %d");
    return(0);
}

/* CreateResPhase2Objects:
 *	Create vnodes for objects to be marked inconsistent
 *	If vnode exists but name doesnt, create it in the parent 
 */
int CreateResPhase2Objects(ViceFid *pfid, dlist *vlist, dlist *inclist,
				   Volume *volptr, VolumeId VSGVolnum,
				   int *blocks) {
    long errorCode = 0;
    *blocks =  0;
    
    vle *pv = FindVLE(*vlist, pfid);
    CODA_ASSERT(pv);
    CODA_ASSERT(pv->vptr);

    ilink *il = 0;
    dlist_iterator next(*inclist);
    int count = 0;
    while (il = (ilink *)next()) {
	int tmperrorCode;
	ViceFid ofid;
	ofid.Volume = pfid->Volume;
	ofid.Vnode = il->vnode;
	ofid.Unique = il->unique;

	ViceFid ipfid;
	ipfid.Volume = pfid->Volume;
	ipfid.Vnode = il->pvnode;
	ipfid.Unique = il->punique;
	
	int tblocks = 0;
	count++;
	if ((count & Yield_CreateP2Obj_Mask) == 0)
	    PollAndYield();
	if (tmperrorCode = (int)CreateObjToMarkInc(volptr, &ipfid, &ofid, il->name, 
					      il->type, vlist, &tblocks)) {
	    SLog(0,  "CreateResPhase2Obj: Error %d in creating %x.%x %s in %x.%x",
		    errorCode, ofid.Vnode, ofid.Unique, il->name, ipfid.Vnode, 
		    ipfid.Unique);
	    if (!errorCode) errorCode = tmperrorCode;
	}
	else 
	    *blocks += tblocks;
    }
    SLog(9,  "CreateResPhase2Objects: returns %d", errorCode);
    return(errorCode);
}
    
/* RUConflict
 *	Check if there is a remove/update conflict for object 
 *	referenced in r->u.u_remove.childfid.
 *	For Files, Symbolic Links:
 *		R/U conflict exists iff VV of deleted obj is inc with
 *			existing object's VV
 *	For Directories:
 *		R/U conflict exists iff an operation  in the 
 *		log of the subtree doesnt exist in the log of deleted 
 *		subtree.
 */
#define FileRemove 0
#define DirRemove 1
static RUConflict(rlent *r, dlist *vlist, olist *hvlog, 
		   ViceFid *dFid) {

    SLog(9,  "Entering RUConflict for %x.%x",
	    dFid->Vnode, dFid->Unique);
    ViceFid cFid;
    vle *cv = 0;
    int rtype = -1;
    
    CODA_ASSERT((r->opcode == ViceRemove_OP) ||
	   (r->opcode == ResolveViceRemove_OP) ||
	   (r->opcode == ViceRemoveDir_OP) ||
	   (r->opcode == ResolveViceRemoveDir_OP));
    if (r->opcode == ViceRemove_OP || r->opcode == ResolveViceRemove_OP)
	rtype = FileRemove;
    else 
	rtype = DirRemove;

    /* get object first */
    {
	cFid.Volume = dFid->Volume;
	if (rtype == FileRemove) {
	    cFid.Vnode = r->u.u_remove.cvnode;
	    cFid.Unique = r->u.u_remove.cunique;
	}
	else {
	    cFid.Vnode = r->u.u_removedir.cvnode;
	    cFid.Unique = r->u.u_removedir.cunique;
	}
	cv = FindVLE(*vlist, &cFid);
	CODA_ASSERT(cv);
    }
    
    /* handle file r/u conflicts */
    {
	if (rtype == FileRemove) {
	    int res = VV_Cmp(&Vnode_vv(cv->vptr), 
			    &(r->u.u_remove.cvv));
	    if (res == VV_EQ || res == VV_SUB) {
		SLog(9,  "RUConflict: no R/U conflict for %x.%x",
			dFid->Vnode, dFid->Unique);
		return(0);
	    }
	    else {
		SLog(9,  "RUConflict: R/U conflict for %x.%x",
			dFid->Vnode, dFid->Unique);
		return(1);
	    }
	}
    }
    
    /* handle dir r/u conflicts */
    {
	RUParm rup(vlist, hvlog, r->serverid, dFid->Volume);
	DirRUConf(&rup, r->u.u_removedir.name, cFid.Vnode, 
		  cFid.Unique);
	SLog(9,  "RUConflict: DirRUConflict returns %d",
		rup.rcode);
	return(rup.rcode);
    }
}
#undef FileRemove 0
#undef DirRemove 1

void GetRemoteRemoveStoreId(ViceStoreId *stid, olist *hvlog, unsigned long serverid, 
			    ViceFid *pFid, ViceFid *cFid, char *cname) {
    SLog(9,  "Entering GetRemoteRemoveStoreId: Parent = %x.%x; Child = %x.%x %s",
	    pFid->Vnode, pFid->Unique, cFid->Vnode, cFid->Unique, cname);
    int i = 0;

    stid->Host = 0;
    stid->Uniquifier = 0;
    he *rhe = FindHE(hvlog, serverid);
    if (!rhe) {
	SLog(0,  "GetRemoteRemoveStoreId: Couldnt get host list for host %x",
		serverid);
	return;
    }
    rmtle *r = FindRMTLE(&rhe->vlist, pFid->Vnode, pFid->Unique);
    if (!r) {
	SLog(0,  "GetRemoteRemoveStoreId: Couldnt get remote parent's log %x.%x",
		pFid->Vnode, pFid->Unique);
	return;
    }
    for (i = 0; i < r->u.remote.nentries; i++) {
	rlent *rle = &(r->u.remote.log[i]);
	if ((rle->opcode == ViceRemove_OP ||
	     rle->opcode == ResolveViceRemove_OP) &&
	    (rle->u.u_remove.cvnode == cFid->Vnode) &&
	    (rle->u.u_remove.cunique == cFid->Unique) &&
	    (!strcmp(rle->u.u_remove.name, cname))) {
	    *stid = rle->storeid;
	    break;
	}
	if ((rle->opcode == ViceRemoveDir_OP ||
	     rle->opcode == ResolveViceRemoveDir_OP) &&
	    (rle->u.u_removedir.cvnode == cFid->Vnode) &&
	    (rle->u.u_removedir.cunique == cFid->Unique) &&
	    (!strcmp(rle->u.u_removedir.name, cname))) {
	    *stid = rle->storeid;
	    break;
	}

    }
    if (i == r->u.remote.nentries) 
	SLog(0,  "GetRemoteRemoveStoreId: Couldnt find remove entry for %x.%x",
		cFid->Vnode, cFid->Unique);
    SLog(9,  "GetRemoteRemoveStoreId : returning storeid = %x.%x",
	    stid->Host, stid->Uniquifier);
}
/* DirRUConf:
 *	Called on each child via Enumerate Dir 
 *	Detects remove/update conflicts on objects.
 */
int EDirRUConf(PDirEntry de, void *data) 
{
    VnodeId vnode;
    Unique_t unique;
    char *name = de->name;
    RUParm *rup = (RUParm *)data;
    FID_NFid2Int(&de->fid, &vnode, &unique);
    
    DirRUConf(rup, name, vnode, unique);
}

int DirRUConf(RUParm *rup, char *name, long vnode, long unique)
{
    if (rup->rcode) return(1);
    if (!strcmp(name, ".") || !strcmp(name, "..")) return(0);
    
    ViceFid cFid, pFid;
    vle *cv = 0;
    vle *pv = 0;
    /* get object and parent's object */
    {
	cFid.Volume = pFid.Volume = rup->vid;
	cFid.Vnode = vnode;
	cFid.Unique = unique;
	
	cv = FindVLE(*(rup->vlist), &cFid);
	CODA_ASSERT(cv);
	
	pFid.Vnode = cv->vptr->disk.vparent;
	pFid.Unique = cv->vptr->disk.uparent;
	pv = FindVLE(*(rup->vlist), &pFid);
	CODA_ASSERT(pv);
    }

    /* do file ruconflict detection */
    {
	if (cv->vptr->disk.type != vDirectory) {
	    /* get deleted object's vv for remote site */
	    ViceVersionVector *VV;
	    {
		/* find the remote parent's log */
		CODA_ASSERT(rup->srvrid != ThisHostAddr);
		he *rhe = FindHE(rup->hvlog, rup->srvrid);
		CODA_ASSERT(rhe);
		rmtle *r = FindRMTLE(&rhe->vlist, pFid.Vnode, 
				     pFid.Unique);
		CODA_ASSERT(r);

		/* search log for child's deletion entry */
		VV = 0;
		for (int i = 0; i < r->u.remote.nentries; i++) {
		    rlent *rle = &(r->u.remote.log[i]);
		    if ((rle->opcode == ViceRemove_OP ||
			 rle->opcode == ResolveViceRemove_OP) &&
			(rle->u.u_remove.cvnode == cFid.Vnode) &&
			(rle->u.u_remove.cunique == cFid.Unique) &&
			(!strcmp(rle->u.u_remove.name, name))) {
			VV = &(rle->u.u_remove.cvv);
			break;
		    }
		    if ((rle->opcode == ViceRename_OP ||
			 rle->opcode == ResolveViceRename_OP) &&
			(rle->u.u_rename.rename_tgt.tgtexisted) &&
			(rle->u.u_rename.rename_tgt.TgtVnode == cFid.Vnode) &&
			(rle->u.u_rename.rename_tgt.TgtUnique == cFid.Unique) &&
			(!strcmp(rle->u.u_rename.rename_tgt.newname, name))) {
			VV = &(rle->u.u_rename.rename_tgt.TgtGhost.TgtGhostVV);
			break;
		    }
		}
		if (!VV) {
		    /* object did not exist when tree was removed - ru conflict */
		    rup->rcode = 1;
		    return(1);
		}
	    }
	    
	    int result = VV_Cmp(&Vnode_vv(cv->vptr), VV);
	    if (result == VV_EQ || result == VV_SUB)
		return(0);
	    else {
		rup->rcode = 1;
		return(1);
	    }
	}
    }
    
    /* do directory ruconflict detection */
    {
	/* get log for directory where it was removed */
	rlent *rmtlog = 0;
	int nrmtentries = 0;
	{
	    CODA_ASSERT(rup->srvrid != ThisHostAddr);
	    he *rhe = FindHE(rup->hvlog, rup->srvrid);
	    CODA_ASSERT(rhe);
	    rmtle *r = FindRMTLE(&rhe->vlist, cFid.Vnode, cFid.Unique);
	    if (!r) {
		/* object didnt exist to be removed */
		SLog(0, "DirRUConflict: %x.%x doesnt exist in rmt log",
			cFid.Vnode, cFid.Unique);
		rup->rcode = 1;
		return(1);
	    }
	    rmtlog = r->u.remote.log;
	    nrmtentries = r->u.remote.nentries;
	}
	CODA_ASSERT(rmtlog);
	
	/* get last entry of local log for directory */
	rlent *lastrle;
	{	
	    VNResLog *vnlog;
	    pdlist *plist = GetResLogList(cv->vptr->disk.vol_index,
					  vnode, unique, &vnlog);
	    CODA_ASSERT(plist);
	    if (plist->count() == 0) 
		CreateAfterCrashLogRecord(cv->vptr, plist);
	    pdlink *pl = plist->last();
	    CODA_ASSERT(pl);
	    lastrle = strbase(rlent, pl, link);
	}
    	/* check if last entry is in remote log */
	{
	    int i = 0;

	    CODA_ASSERT(nrmtentries > 0);
	    for (i = nrmtentries - 1; i >= 0 && lastrle ; i--) 
		if (SID_EQ(rmtlog[i].storeid, lastrle->storeid) &&
		    ((rmtlog[i].opcode == lastrle->opcode) ||
		     (lastrle->opcode == ResolveAfterCrash_OP))) {
		    rup->rcode = 0;
		    break;
		}
	    if (i < 0 || !lastrle) {
		SLog(0,  "DirRUConflict: Last log entry for %x.%x doesnt exist in rmt log",
			cFid.Vnode, cFid.Unique);
		rup->rcode = 1;
		return(1);
	    }
	}
	
	/* check ru conflict for children */
	{
	    PDirHandle dh;
	    dh = VN_SetDirHandle(cv->vptr);
	    if (DH_IsEmpty(dh)) 
                    DH_EnumerateDir(dh, EDirRUConf, (void *)rup);
	    VN_PutDirHandle(cv->vptr);
	}
    }
    return(0);
}

/* warning: name must have size MAXNAMELEN or larger */
/* Find the name of a vnode in its parent directory:
   return 0 upon finding the name
   return 0 upon failure to find vnodes
   return 0 upon failure to find name in parent
   return 1 upon success
*/

int GetNameInParent(Vnode *vptr, dlist *vlist, Volume *volptr, char *name) 
{
	long errorCode = 0;
	PDirHandle dh;
	int rc;
	ViceFid pFid;
	ViceFid Fid;
	
	VN_VN2PFid(vptr, volptr, &pFid);
	VN_VN2Fid(vptr, volptr, &Fid);

	if (pFid.Unique == 0 || pFid.Vnode == 0) {
		SLog(0,"GetNameInParent %s: Parent had 0 in id; returning 0", 
		     FID_(&pFid));
		return 0;
	}
	vle *pv = FindVLE(*vlist, &pFid);
	if (!pv) {
		pv = AddVLE(*vlist, &pFid);
		/* THE LOCK NEED NOT BE WRITE_LOCK - PARANOIA FOR OTHER OPS
		   TOUCHING THIS VNODE */
		errorCode = GetFsObj(&pFid, &volptr, &pv->vptr, WRITE_LOCK, 
				     NO_LOCK, 1, 0, 0);
		if (errorCode) {
			SLog(0, "GetNameInParent for %s: Error %d getting parent %s",
			     FID_(&pFid), errorCode, FID_2(&Fid));
			if (!pv->vptr) 
				vlist->remove(pv);
			return 0;
		}
	}
	
	dh = VN_SetDirHandle(pv->vptr);
	rc = DH_LookupByFid(dh, name, &Fid);
	VN_PutDirHandle(pv->vptr);
	return ! rc;
}

static char *ExtractNameFromRLE(rlent *a) 
{
	char *ca = NULL;

	switch((a)->opcode) {
	case ResolveViceRemove_OP:
	case ViceRemove_OP:
		ca = (a)->u.u_remove.name;
		break;
	case ResolveViceCreate_OP:
	case ViceCreate_OP:
		ca = (a)->u.u_create.name;
		break;
	case ResolveViceRename_OP:
	case ViceRename_OP:
		if ((a)->u.u_rename.srctgt == SOURCE) 
			ca = (a)->u.u_rename.rename_src.oldname;
		else 
			ca = (a)->u.u_rename.rename_tgt.newname;
		break;
	case ResolveViceSymLink_OP:
	case ViceSymLink_OP:
		ca = (a)->u.u_symlink.name;
		break;
	case ResolveViceLink_OP:
	case ViceLink_OP:
		ca = (a)->u.u_hardlink.name;
		break;
	case ResolveViceMakeDir_OP:
	case ViceMakeDir_OP:
		ca = (a)->u.u_makedir.name;
		break;
	case ResolveViceRemoveDir_OP:
	case ViceRemoveDir_OP:
		ca = (a)->u.u_removedir.name;
		break;
	default:
		break;
	}

	return(ca);
}

static int ExtractVNTypeFromRLE(rlent *a) 
{
	switch(a->opcode) {
	case ResolveViceRemove_OP:
	case ViceRemove_OP:
	case ResolveViceCreate_OP:
	case ViceCreate_OP:
	case ResolveViceLink_OP:
	case ViceLink_OP:
		return(vFile);
	case ResolveViceRename_OP:
	case ViceRename_OP: {
		ViceFid tgtFid;
		/* XXX BE CAREFUL WITH CHILD FIDS AND RENAMES */
		ExtractChildFidFromRLE(a, &tgtFid);
		if (ISDIR(tgtFid))
			return(vDirectory);
		else
			return(vFile);	/* XXX - what about symlinks ? */
	}
	break;
	case ResolveViceSymLink_OP:
	case ViceSymLink_OP:
		return(vSymlink);
	case ResolveViceMakeDir_OP:
	case ViceMakeDir_OP:
	case ResolveViceRemoveDir_OP:
	case ViceRemoveDir_OP:
		return(vDirectory);
	default:
		CODA_ASSERT(0);
		break;
	}
	
	return(0); /* dummy for g++ */		
}

static void ExtractChildFidFromRLE(rlent *a, ViceFid *fa) 
{

	fa->Volume = 0;
	fa->Vnode = 0;
	fa->Unique = 0;

	switch((a)->opcode) {
	case ViceNewStore_OP:
	case ResolveViceNewStore_OP:
		fa->Vnode = a->dvnode;
		fa->Unique = a->dunique;
		break;
	case ResolveViceRemove_OP:
	case ViceRemove_OP:
		fa->Vnode = a->u.u_remove.cvnode;
		fa->Unique = a->u.u_remove.cunique;
		break;
	case ResolveViceCreate_OP:
	case ViceCreate_OP:
		fa->Vnode = a->u.u_create.cvnode;
		fa->Unique = a->u.u_create.cunique;
		break;
	case ResolveViceRename_OP:
	case ViceRename_OP:
		if (a->u.u_rename.srctgt == SOURCE || 
		    !a->u.u_rename.rename_tgt.tgtexisted) {
			fa->Vnode = a->u.u_rename.rename_src.cvnode;
			fa->Unique = a->u.u_rename.rename_src.cunique;
		} else {
			fa->Vnode = a->u.u_rename.rename_tgt.TgtVnode;
			fa->Unique = a->u.u_rename.rename_tgt.TgtUnique;
		}
		SLog(1, "Fid %x.%x", fa->Vnode, fa->Unique);
		break;
	case ResolveViceSymLink_OP:
	case ViceSymLink_OP:
		fa->Vnode = a->u.u_symlink.cvnode;
		fa->Unique = a->u.u_symlink.cunique;
		break;
	case ResolveViceLink_OP:
	case ViceLink_OP:
		fa->Vnode = a->u.u_hardlink.cvnode;
		fa->Unique = a->u.u_hardlink.cunique;
		break;
	case ResolveViceMakeDir_OP:
	case ViceMakeDir_OP:
		fa->Vnode = a->u.u_makedir.cvnode;
		fa->Unique = a->u.u_makedir.cunique;
		break;
	case ResolveViceRemoveDir_OP:
	case ViceRemoveDir_OP:
		fa->Vnode = a->u.u_removedir.cvnode;
		fa->Unique = a->u.u_removedir.cunique;
		break;
	case ResolveNULL_OP:
		fa->Vnode = a->dvnode;		/* XXX hack! */
		fa->Unique = a->dunique;	/* XXX hack! */
		break;
	default:
		SLog(0,  "ExtractChildFidFromRLE: Illegal opcode %d",
		     a->opcode);
		CODA_ASSERT(0);
		break;
	}
}

static int CompareStoreId(ViceStoreId *a, ViceStoreId *b) 
{
	if (a->Host < b->Host) 
		return(-1);
	else if (a->Host > b->Host) 
		return(1);
	else if (a->Uniquifier < b->Uniquifier) 
		return(-1);
	else if (a->Uniquifier > b->Uniquifier) 
		return(1);
	else 
		return(0);
}

/* Compare Log Records:
 *	Storeid is the major sorting index.
 *	Serverid is the secondary sorting index
 */
static int CmpLogEntries(rlent **a, rlent **b) 
{
    int res = CompareStoreId(&((*a)->storeid), &((*b)->storeid));
    if (res) 
	    return(res);
    
    if ((*a)->serverid < (*b)->serverid) 
	    return(-1);
    if ((*a)->serverid > (*b)->serverid) 
	    return(1);
    return(0);
}

/*
 * CmpFidOp:
 * 	Compare 2 rlents by fid and operation
 *	Return -ve, 0, +ve for a < b, a=b, a > b respectively
 *	Primary sort is on fid;
 *	If fids are equal then order of operation are :
 *		CREATE(Fid) <ALL OTHER OPS (Fid)> DELETE(Fid)
 */
static int CmpFidOp(rlent **a, rlent **b) 
{
	ViceFid fa, fb;
	int res = 0;
	
	/* fill in the fids */
	{
		fa.Volume = fb.Volume = 0;
		ExtractChildFidFromRLE(*a, &fa);
		ExtractChildFidFromRLE(*b, &fb);
	}

	/* Compare the fid first */
	{
		res = FID_Cmp(&fa, &fb);
		if (res) 
			return(res);
	}
	/* Compare the ops if fids are same */
	{
		int oa = (int)((*a)->opcode);
		int ob = (int)((*b)->opcode);
	
		switch(oa) {
		case ViceCreate_OP:
		case ResolveViceCreate_OP:
		case ViceMakeDir_OP:
		case ResolveViceMakeDir_OP:
		case ViceSymLink_OP:
		case ResolveViceSymLink_OP:
			return(-1);
		case ViceRemove_OP:
		case ResolveViceRemove_OP:
		case ViceRemoveDir_OP:
		case ResolveViceRemoveDir_OP:
			return(1);
		default:
			break;
		}
		switch(ob) {
		case ViceCreate_OP:
		case ResolveViceCreate_OP:
		case ViceMakeDir_OP:
		case ResolveViceMakeDir_OP:
		case ViceSymLink_OP:
		case ResolveViceSymLink_OP:
			return(1);
		case ViceRemove_OP:
		case ResolveViceRemove_OP:
		case ViceRemoveDir_OP:
		case ResolveViceRemoveDir_OP:
			return(-1);
		default:
			break;
		}
		return(0);
	}
}

static int CmpNames(rlent *a, rlent *b) 
{
	char *ca = 0;
	char *cb = 0;
	
	ca = ExtractNameFromRLE(a);
	cb = ExtractNameFromRLE(b);
	
	if (ca && cb)
		return(strcmp(ca, cb));
	else
		return(-1);
}
