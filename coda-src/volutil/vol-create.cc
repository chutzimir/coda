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


#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/signal.h>

#ifdef __MACH__
#include <sysent.h>
#include <libc.h>
#include <mach.h>
#else	/* __linux__ || __BSD44__ */
#include <unistd.h>
#include <stdlib.h>
#endif

#include <lwp.h>
#include <lock.h>
#include <rpc2.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <util.h>
#include <rvmlib.h>
#include <coda_dir.h>
#include <vice.h>
#include <voltypes.h>
#include <cvnode.h>
#include <volume.h>
#include <errors.h>
#include <partition.h>
#include <viceinode.h>
#include <vutil.h>
#include <index.h>
#include <recov.h>
#include <rvmdir.h>
#include <camprivate.h>
#include <logalloc.h>
#include <reslog.h>
#include <coda_globals.h>
#include <recov_vollog.h>
#include "volutil.private.h"

Error error;
extern void PrintVnode(FILE *outfile, VnodeDiskObject *vnode, VnodeId vnodeNumber);
PRIVATE int ViceCreateRoot(Volume *vp);

/* Create a new volume (readwrite or replicated). Invoked through rpc2 by volume utility. */
/*
  BEGIN_HTML
  <a name="S_VolCreate"><strong>Service request to create a volume</strong></a>
  END_HTML
*/
long S_VolCreate(RPC2_Handle rpcid, RPC2_String formal_partition,
	RPC2_String formal_volname, VolumeId *volid, RPC2_Integer repvol,
	VolumeId grpId /* , RPC2_Integer resflag - not added until ready to release new server */) {
    VolumeId volumeId = 0;
    VolumeId parentId = 0;
    Volume *vp = NULL;
    int status = 0;    // transaction status variable
    int rc = 0;
    int volidx;
    ProgramType *pt;
    int resflag = RVMRES;

    /* To keep C++ 2.0 happy */
    char *partition = (char *)formal_partition;
    char *volname = (char *)formal_volname;    

    error = 0;

    assert(LWP_GetRock(FSTAG, (char **)&pt) == LWP_SUCCESS);

    LogMsg(9, VolDebugLevel, stdout,
	   "Entering S_VolCreate: rpcid = %d, partition = %s, volname = %s,"
	   " volumeid = %x, repvol = %d, grpid = %x",
	   rpcid, partition, volname, volid ? *volid : 0, repvol, grpId);
    CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)

    rc = VInitVolUtil(volumeUtility);
    if (rc != 0)
	CAMLIB_ABORT(rc);
    /* DInit(10); - Should not be called everytime - called in main of file server */

    /* Use a new volumeId only if the user didn't specify any */
    if (!volid  || !(*volid) )
	volumeId = VAllocateVolumeId(&error);
    else {
	volumeId = *volid;

	/* check that volume id is legal */
	if (volumeId > VGetMaxVolumeId()) {
	    LogMsg(0, VolDebugLevel, stdout,
		   "Warning: %x is > MaxVolID; setting MaxVolID to %x\n",
		   volumeId, volumeId);
	    VSetMaxVolumeId(volumeId);
	}
    }
    LogMsg(9, VolDebugLevel, stdout,
	   "VolCreate: VAllocateVolumeId returns %x", volumeId);
    if (error) {
	LogMsg(0, VolDebugLevel, stdout, "Unable to allocate a volume number; volume not created");
	CAMLIB_ABORT(VNOVOL);
    }

    parentId = volumeId;    // we are creating a readwrite (or replicated) volume

    if (repvol && grpId == 0) {
        LogMsg(0, VolDebugLevel, stdout, "S_VolCreate: can't create replicated volume without group id");
	CAMLIB_ABORT(VFAIL);
    }

    /* If we are creating a replicated volume, pass along group id */
    vp = VCreateVolume(&error, partition, volumeId, parentId, repvol?grpId:0, readwriteVolume, repvol? resflag : 0);
    if (error) {
	LogMsg(0, VolDebugLevel, stdout, "Unable to create the volume; aborted");
	CAMLIB_ABORT(VNOVOL);
    }
    V_uniquifier(vp) = 1;
    V_creationDate(vp) = V_copyDate(vp);
    V_inService(vp) = V_blessed(vp) = 1;
    V_type(vp) = readwriteVolume;
    AssignVolumeName(&V_disk(vp), volname, 0);

/* could probably begin transaction here instead of at beginning */
/* must include both ViceCreateRoot and VUpdateVolume for vv atomicity */
    ViceCreateRoot(vp);
    V_destroyMe(vp) = V_needsSalvaged(vp) = 0;
    V_linkcount(vp) = 1;
    volidx = V_volumeindex(vp);
    VUpdateVolume(&error, vp);
    VDetachVolume(&error, vp);	/* Allow file server to grab it */
    assert(error == 0);
    CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
    /* to make sure that rvm records are getting flushed - to find this bug */
    assert(rvm_flush() == RVM_SUCCESS);
    VDisconnectFS();
    if (status == 0) {
	LogMsg(0, VolDebugLevel, stdout, "create: volume %x (%s) created", volumeId, volname);
	*volid = volumeId;	    /* set value of out parameter */
	if (CAMLIB_REC(VolumeList[volidx]).data.volumeInfo)
	    if (CAMLIB_REC(VolumeList[volidx]).data.volumeInfo->maxlogentries)
	        LogStore[volidx] = new PMemMgr(sizeof(rlent), 0, volidx,
						 CAMLIB_REC(VolumeList[volidx]).data.volumeInfo->maxlogentries);
	    else
	        LogStore[volidx] = new PMemMgr(sizeof(rlent), 0, volidx, MAXLOGSIZE);
	else
	    assert(0);
    }
    else {
	LogMsg(0, VolDebugLevel, stdout, "create: volume creation failed for volume %x", volumeId);
	LogMsg(0, VolDebugLevel, stdout, "status = (%d) ", status);
    }
    return(status?status:rc);
}

/* Adapted from the file server; create a root directory for */
/* a new volume */
PRIVATE int ViceCreateRoot(Volume *vp)
{
    DirHandle dir;
    AL_AccessList * ACL;
    ViceFid	did;
    Inode inodeNumber;
    char buf[SIZEOF_LARGEDISKVNODE];
    struct VnodeDiskObject *vnode = (struct VnodeDiskObject *) buf;
    struct VnodeClassInfo *vcp = &VnodeClassInfo_Array[vLarge];
    vindex v_index(V_id(vp), vLarge, vp->device, SIZEOF_LARGEDISKVNODE);

    bzero((char *)vnode, SIZEOF_LARGEDISKVNODE);    

    /* create an inode to hold the volume's root directory */
/*    inodeNumber = icreate(vp->device, 0, V_id(vp), 1, 1, 0);
    assert(inodeNumber >= 0);
*/
    /* since we have dirpages in rvm dont create inode now */
/*  SetSalvageDirHandle(&dir, V_id(vp), vp->device, inodeNumber);
*/
    SetSalvageDirHandle(&dir, V_id(vp), vp->device, 0);
    /* Updated the vnode number in the dirhandle for rvm dir package */
    dir.vnode = bitNumberToVnodeNumber(0, vLarge);
    dir.unique = 1;
    did.Volume = V_id(vp);
    /* not sure if this wants bit number or vnode id **ehs***/
    did.Vnode = (VnodeId)bitNumberToVnodeNumber(0, vLarge);
    LogMsg(29, VolDebugLevel, stdout, "ViceCreateRoot: did.Vnode = %d", did.Vnode);
    did.Unique = 1;

    /* set up the physical directory */
    assert(!(MakeDir((long *)&dir, (long *)&did, (long *)&did)));
    DFlush();

 /* build a single entry ACL that gives all rights to everyone */
    ACL = VVnodeDiskACL(vnode);
    ACL->MySize = sizeof(AL_AccessList);
    ACL->Version = AL_ALISTVERSION;
    ACL->TotalNoOfEntries = 1;
    ACL->PlusEntriesInUse = 1;
    ACL->MinusEntriesInUse = 0;
    ACL->ActualEntries[0].Id = -101;
    ACL->ActualEntries[0].Rights = 127;

    /* set up vnode info */
    vnode->type = vDirectory;
    vnode->cloned = 0;
    vnode->modeBits = 0777;
    vnode->linkCount = 2;
    vnode->length = Length((long *)&dir);
    vnode->uniquifier = 1;
    V_uniquifier(vp) = vnode->uniquifier+1;
    vnode->dataVersion = 1;
    vnode->inodeNumber = 0;
    /* we need to simultaneously update vv in VolumeDiskData struct ***/
    InitVV(&vnode->versionvector);
    vnode->vol_index = vp->vol_index;	/* index of vnode's volume in recoverable storage */
    vnode->unixModifyTime = vnode->serverModifyTime = V_creationDate(vp);
    vnode->author = 0;
    vnode->owner = 0;
    vnode->vparent = 0;
    vnode->uparent = 0;
    vnode->vnodeMagic = vcp->magic;

    /* write out the directory in rvm - that will create the inode */
    /* set up appropriate fields in a vnode for DCommit */
    char buf3[sizeof(Vnode)];
    Vnode *vn = (Vnode *)buf3;
    bzero((void *)vn, sizeof(Vnode));
    vn->changed = 1;
    vn->delete_me = 0;
    vn->vnodeNumber = (VnodeId)bitNumberToVnodeNumber(0, vLarge);
    vn->volumePtr = vp;
    bcopy((const void *)vnode, (void *)&vn->disk, sizeof(VnodeDiskObject));
    DCommit(vn);   
    bcopy((const void *)&(vn->disk), (void *) vnode, sizeof(VnodeDiskObject));
    /* should be cautious here - it is a large vnode - so acl should also be 
      copied.  But DCommit doesnt touch it */
    assert(vnode->inodeNumber != 0);
    assert(vnode->uniquifier == 1);

    /* create the resolution log for this vnode if rvm resolution is turned on */
    if (AllowResolution && V_RVMResOn(vp)) {
	LogMsg(0, SrvDebugLevel, stdout, "Creating new log for root vnode\n");
	CreateRootLog(vp, vn);
	vnode->log = VnLog(vn);
    }
    /* write out the new vnode */
    if (VolDebugLevel >= 9) {
	PrintVnode(stdout, vnode, bitNumberToVnodeNumber(0, vLarge));
    }
    assert(v_index.oput((bit32) 0, 1, vnode) == 0);

    V_diskused(vp) = nBlocks(vnode->length);
    
    return (1);
}



