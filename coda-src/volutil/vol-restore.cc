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

#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <ctype.h>
#include <varargs.h>

#ifdef __MACH__
#include <sysent.h>
#include <libc.h>
#else	/* __linux__ || __BSD44__ */
#include <unistd.h>
#include <stdlib.h>
#endif

#include <struct.h>
#include <lwp.h>
#include <lock.h>
#include <rpc2.h>
#include <inodeops.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <util.h>
#include <rvmlib.h>
#include <voltypes.h>
#include <vldb.h>
#include <vice.h>
#include <cvnode.h>
#include <volume.h>
#include <camprivate.h>
#include <coda_dir.h>
#include <errors.h>
#include <recov.h>
#include <dump.h>
#include <fssync.h>
#include <al.h>
#include <rvmdir.h>
#include <rec_smolist.h>
#include <partition.h>
#include <viceinode.h>
#include <vutil.h>
#include <index.h>
#include <volhash.h>
#include <coda_globals.h>
#include <voldump.h>

PRIVATE int RestoreVolume(DumpBuffer_t *, char *, char *, VolumeId *);
PRIVATE int ReadLargeVnodeIndex(DumpBuffer_t *, Volume *);
PRIVATE int ReadSmallVnodeIndex(DumpBuffer_t *, Volume *);
PRIVATE void ReadVnodeDiskObject(DumpBuffer_t *, VnodeDiskObject *, DirInode **, Volume *, long *);
extern void VMFreeDirInode(DirInode *inode);

PRIVATE int VnodePollPeriod = 16;  /* Number of vnodes restored per transaction */
extern void PollAndYield();
#define DUMPBUFSIZE 512000


/*
  BEGIN_HTML
  <a name="S_VolRestore"><strong>Restore a volume from a dump file</strong></a>
  END_HTML
*/ 
long S_VolRestore(RPC2_Handle rpcid, RPC2_String formal_partition, RPC2_String formal_volname,
	RPC2_Unsigned *formal_volid)
{
    int status = 0;
    long rc = 0;
    ProgramType *pt;
    DumpBuffer_t *dbuf;
    char *DumpBuf;
    RPC2_HostIdent hid;
    RPC2_PortalIdent pid;
    RPC2_SubsysIdent sid;
    RPC2_BindParms bparms;
    RPC2_Handle cid;
    RPC2_PeerInfo peerinfo;

    /* To keep C++ 2.0 happy */
    char *partition = (char *)formal_partition;
    char *volname = (char *)formal_volname;
    VolumeId *volid = (VolumeId *)formal_volid;
    
    
    assert(LWP_GetRock(FSTAG, (char **)&pt) == LWP_SUCCESS);

    LogMsg(9, VolDebugLevel, stdout, "Entering S_VolRestore for %x--%s, partition %s", *volid, volname,
	partition);

    rc = VInitVolUtil(volumeUtility);
    if (rc != 0) {
	LogMsg(0, VolDebugLevel, stdout, "S_VolRestore: VInitVolUtil failed with error code %d", rc);
	return(rc);
    }

    /* Avoid using a bogus partition. */
    if (VGetPartition(partition) == NULL) {
	LogMsg(0, VolDebugLevel, stdout, "VolRestore: %s is not in the partition list; not restored.", partition);
	return VFAIL;
    }
    
    
    /* Verify that volid isn't already in use on this server. */
    if (*volid != 0) {
	if (HashLookup(*volid) != -1) {
	    LogMsg(0, VolDebugLevel, stdout, "Restore: Volid %x already in use; restore aborted.",*volid);
	    VDisconnectFS();
	    return VVOLEXISTS;
	}
    }
    /* To allow r/o replication reuse of volume names is allowed. */

    /* Set up a connection with the client. */
    if ((rc = RPC2_GetPeerInfo(rpcid, &peerinfo)) != RPC2_SUCCESS) {
	LogMsg(0, VolDebugLevel, stdout,"VolRestore: GetPeerInfo failed with %s", RPC2_ErrorMsg((int)rc));
	VDisconnectFS();
	return rc;
    }

    hid = peerinfo.RemoteHost;
    pid = peerinfo.RemotePortal;
    sid.Tag = RPC2_SUBSYSBYID;
    sid.Value.SubsysId = VOLDUMP_SUBSYSTEMID;
    bparms.SecurityLevel = RPC2_OPENKIMONO;
    bparms.SideEffectType = SMARTFTP;
    bparms.EncryptionType = NULL;
    bparms.ClientIdent = NULL;
    bparms.SharedSecret = NULL;
    
    if ((rc = RPC2_NewBinding(&hid, &pid, &sid, &bparms, &cid))!=RPC2_SUCCESS) {
	LogMsg(0, VolDebugLevel, stdout, "VolRestore: Bind to client failed, %s!", RPC2_ErrorMsg((int)rc));
	VDisconnectFS();
	return rc;
    }

    DumpBuf = (char *)malloc(DUMPBUFSIZE);
    if (!DumpBuf) {
	LogMsg(0, VolDebugLevel, stdout, "VolRestore: Can't malloc buffer (%d)!", errno);
	VDisconnectFS();
	return VFAIL;
    }
    /* Initialize dump buffer. */
    dbuf = InitDumpBuf((byte *)DumpBuf, (long)DUMPBUFSIZE, *volid, cid);
    
    status = RestoreVolume(dbuf, partition, volname, volid);

    if (status != 0) {
	HashDelete(*volid);	       /* Just in case VCreateVolume was called */
    } 

    VDisconnectFS();
    LogMsg(2, VolDebugLevel, stdout, "Restore took %d seconds to dump %d bytes.",dbuf->secs,dbuf->nbytes);
    free(dbuf);
    free(DumpBuf);

    if (RPC2_Unbind(cid) != RPC2_SUCCESS) {
	LogMsg(0, VolDebugLevel, stdout, "S_VolNewDump: Can't close binding %s", RPC2_ErrorMsg((int)rc));
    }

    return(status?status:rc);
}

PRIVATE int RestoreVolume(DumpBuffer_t *buf, char *partition, char *volname, VolumeId *volid)
{
    VolumeDiskData vol;
    struct DumpHeader header;
    Error error;
    register Volume *vp;
    VolumeId parentid;
    int status = 0;

    /* Get header information from the dump buffer. */
    if (!ReadDumpHeader(buf, &header)) {
	LogMsg(0, VolDebugLevel, stdout, "Error reading dump header; aborted");
	return VFAIL;
    }
    LogMsg(9, VolDebugLevel, stdout, "Volume id = %x, Volume name = %s", header.volumeId, header.volumeName);
    LogMsg(9, VolDebugLevel, stdout, "Dump from backup taken from %x at %s", header.parentId, ctime((long *)&header.backupDate));
    LogMsg(9, VolDebugLevel, stdout, "Dump uniquifiers %x -> %x", header.oldest, header.latest);
    
    if (header.Incremental) {
	LogMsg(0, VolDebugLevel, stdout, "VolRestore Error -- trying to restore incremental dump!");
	return VFAIL;
    }
    
    if (ReadTag(buf) != D_VOLUMEDISKDATA) {
	LogMsg(0, VolDebugLevel, stdout, "VolRestore: Volume header missing from dump; not restored");
	return VFAIL;
    }
    bzero(&vol, sizeof(VolumeDiskData));
    if (!ReadVolumeDiskData(buf, &vol)) {
	LogMsg(0, VolDebugLevel, stdout, "VolRestore: ReadVolDiskData failed; not restored.");
	return VFAIL;
    }
    int volumeType = vol.type;
    if ((volumeType == readwriteVolume) || (volumeType == replicatedVolume)) {
	LogMsg(0, VolDebugLevel, stdout, "VolRestore: Dumped volume has Illegal type; not restored.");
	return VFAIL;
    }

    CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)
    if (*volid != 0) {
	/* update Maxid of the volumes so that the volume routines will work */
	unsigned long maxid = *volid & 0x00FFFFFF;
	if (maxid > (CAMLIB_REC(MaxVolId) & 0x00FFFFFF)){
	    LogMsg(0, VolDebugLevel, stdout, "Restore: Updating MaxVolId; maxid = 0x%x MaxVolId = 0x%x", 
		maxid, CAMLIB_REC(MaxVolId));
	    CAMLIB_MODIFY(CAMLIB_REC(MaxVolId),
			  (maxid + 1) | (CAMLIB_REC(MaxVolId) & 0xFF000000));
	}
    } else {
	*volid = VAllocateVolumeId(&error);
	if (error) {
	    LogMsg(0, VolDebugLevel, stdout, "Unable to allocate volume id; restore aborted");
	    CAMLIB_ABORT(-1);
	}
    }
    /* NOTE:  Do NOT set the parentId of restore RO volumes to the ORIGINAL
       parentID. If they are the same, then the restored volume will look
       like a RO copy of the parent and it would start being used instead
       of the parent in some cases.- still true, true for backups? dcs */
    parentid = (volumeType == readonlyVolume) ? *volid : vol.parentId;

    vp = VCreateVolume(&error, partition, *volid, parentid, 0, volumeType);
    if (error) {
	LogMsg(0, VolDebugLevel, stdout,"Unable to create volume %x; not restored", *volid);
	CAMLIB_ABORT(-1);
    }

    V_blessed(vp) = 0;
    VUpdateVolume(&error, vp);
    if (error) {
	LogMsg(0, VolDebugLevel, stdout, "S_VolClone: Trouble updating voldata for %#8x!", *volid);
	CAMLIB_ABORT(VFAIL);
    }

    CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
    if (status == 0)
	LogMsg(9, VolDebugLevel, stdout, "restore createvol completed successfully");
    else {
	LogMsg(0, VolDebugLevel, stdout, "restore: createvol failed.");
	return status;
    }
    
    /* Read in and create the large vnode list. */
    if (ReadTag(buf) != D_LARGEINDEX) {
	LogMsg(0, VolDebugLevel, stdout, "Large Vnode Index not found; restore aborted");
	return VNOVNODE;
    }
    if (!ReadLargeVnodeIndex(buf, vp)) 
	return VNOVNODE;	/* Assume message is already printed. */

    /* Read in and create the small vnode list. */
    if (ReadTag(buf) != D_SMALLINDEX) {
	LogMsg(0, VolDebugLevel, stdout, "Small Vnode Index not found; restore aborted");
	return VNOVNODE;
    }
    if (!ReadSmallVnodeIndex(buf, vp))
	return VNOVNODE;	/* Assume message is already printed. */

    /* Verify that we're really at the end of the dump. */
    if (!EndOfDump(buf)){
	LogMsg(0, VolDebugLevel, stdout, "End of dump doesnt look good; restore aborted");
	return VFAIL;
    }

    /* Modify vp (data from create) with vol (data from the dump.) */
    if (strlen(volname) == 0) {
	AssignVolumeName(&vol, vol.name,
			 (volumeType==readonlyVolume?".readonly":".restored"));
    } else {
	strcpy(vol.name, volname);
    }

    ClearVolumeStats(&vol);
    CopyVolumeHeader(&vol, &V_disk(vp));	/* Doesn't use RVM */

    strcpy(V_partname(vp), partition);
    V_restoredFromId(vp) = vol.parentId;
    V_needsSalvaged(vp) = V_destroyMe(vp) = 0; /* Make sure it gets online. */
    V_inService(vp) = V_blessed(vp) = 1;
    
    LogMsg(0, VolDebugLevel, stdout, "partname -%s-", V_partname(vp));
    CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)

    VUpdateVolume(&error, vp);
    if (error) {
	LogMsg(0, VolDebugLevel, stdout, "restore: Unable to rewrite volume header; restore aborted");
	CAMLIB_ABORT(-1);
    }

    VDetachVolume(&error, vp); /* Let file server get its hands on it */
    if (error) {
	LogMsg(0, VolDebugLevel, stdout, "restore: Unable to detach volume; restore aborted");
	CAMLIB_ABORT(-1);
    }
    VListVolumes();			/* Create updated /vice/vol/VolumeList */

    CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
    if (status == 0)
	LogMsg(9, VolDebugLevel, stdout, "S_VolRestore: VUpdateVolume completed successfully");
    else {
	LogMsg(0, VolDebugLevel, stdout, "restore: VUpdateVolume failed ");
	return status;
    }

    LogMsg(0, VolDebugLevel, stdout, "Volume %x (%s) restored successfully", *volid, vol.name);
    return 0;
}

/* This is sort of conservative. When a volume is created, two null vnode
 * lists are created as well, but no vnodes should be added. So this routine
 * will free up the empty indexes and would delete any vnodes if there are any.
 */
PRIVATE void FreeVnodeIndex(Volume *vp, VnodeClass vclass)
{
    rec_smolist *list;
    bit32 listsize;
    int volindex = V_volumeindex(vp);
    VnodeDiskObject *vdo;
    int status = 0;

    CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)

    if (vclass == vSmall){
	list = CAMLIB_REC(VolumeList[volindex]).data.smallVnodeLists;
	listsize = CAMLIB_REC(VolumeList[volindex]).data.nsmallLists;
	CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.smallVnodeLists, 0); 
	CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nsmallvnodes, 0);
	CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nsmallLists, 0);
    }
    else if (vclass == vLarge){
	list = CAMLIB_REC(VolumeList[volindex]).data.largeVnodeLists;
	listsize = CAMLIB_REC(VolumeList[volindex]).data.nlargeLists;
	CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.largeVnodeLists, 0); 
	CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nlargevnodes, 0);
	CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nlargeLists, 0);
    }
    else 
	assert(1==2);
    /* now free the list */
    for (int i = 0; i < listsize; i++){
	rec_smolink *p;
	while (p = list[i].get()){
	    LogMsg(0, VolDebugLevel, stdout, "Vol_Restore: Found a vnode on the new volume's %s list!",
		(vclass == vLarge) ? "Large" : "Small");
	    vdo = strbase(VnodeDiskObject, p, nextvn);
	    CAMLIB_REC_FREE((char *)vdo);
	    
	}
    }	
    CAMLIB_REC_FREE(((char *)list));

    CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
    assert(status == 0);	/* Never aborts ... */
}

PRIVATE int ReadLargeVnodeIndex(DumpBuffer_t *buf, Volume *vp)
{
    int num_vnodes, list_size = 0, status = 0;
    long vnodenumber;
    int	nvnodes = 0;
    char    vbuf[SIZEOF_LARGEDISKVNODE];
    VnodeDiskObject *vdo = (VnodeDiskObject *)vbuf;
    rec_smolist *rlist;
    DirInode *dinode = NULL;
    DirInode *camdInode = NULL;
    register char tag;
    int volindex = V_volumeindex(vp);

    LogMsg(9, VolDebugLevel, stdout, "Restore: Reading in large vnode array.");
    FreeVnodeIndex(vp, vLarge);
    while ((tag = ReadTag(buf)) > D_MAX && tag != EOF) {
	switch(tag) {
	    case 'v':
		ReadLong(buf, (unsigned long *)&num_vnodes);
		break;
	    case 's':
		ReadLong(buf, (unsigned long *)&list_size);
		break;
	    default:
		LogMsg(0, VolDebugLevel, stdout, "Unexpected field of Vnode found; restore aborted");
		return FALSE;
	}
    }
    assert(list_size > 0);
    PutTag(tag, buf);

    /* Set up a list structure. */
    CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)

    rlist = (rec_smolist *)(CAMLIB_REC_MALLOC(sizeof(rec_smolist) * list_size));
    rec_smolist *vmrlist = (rec_smolist *)malloc(sizeof(rec_smolist) * list_size);
    bzero(vmrlist, sizeof(rec_smolist) * list_size);
    CAMLIB_MODIFY_BYTES(rlist, vmrlist, sizeof(rec_smolist) * list_size);
    free(vmrlist);

    /* Update Volume structure to point to new vnode list. 
     * Doing this here so that if an abort happens during the loop the
     * commited vnodes will be scavanged.
     */
    CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nlargevnodes, num_vnodes);
    CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nlargeLists, list_size);
    CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.largeVnodeLists, rlist);
    
    CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
    assert(status == 0);			/* Never aborts... */

    VnodeDiskObject *camvdo;
    long tmp = 0, i = 0;
    while (i < num_vnodes) {
	CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)
	do {
	    ReadVnodeDiskObject(buf, vdo, &dinode, vp, &vnodenumber);
	    LogMsg(19, VolDebugLevel, stdout, "Just read vnode for index %d.", vnodenumber);
	    if (vdo->type != vNull){
		/* this vnode is allocated */
		/* copy the directory pages into rec storage */
		dinode->refcount = 1;
		CopyDirInode(dinode, &camdInode);
		VMFreeDirInode(dinode);
		vdo->inodeNumber = (Inode)camdInode;
		camvdo=(VnodeDiskObject *)CAMLIB_REC_MALLOC(SIZEOF_LARGEDISKVNODE);
		CAMLIB_MODIFY_BYTES(&(camvdo->nextvn), &tmp, sizeof(long));
		rlist[vnodeIdToBitNumber(vnodenumber)].append(&(camvdo->nextvn));
		vdo->cloned = 0;
		vdo->vol_index = volindex;
		vdo->vnodeMagic = LARGEVNODEMAGIC;
		ViceLockClear((&vdo->lock));
		bcopy(&(camvdo->nextvn), &(vdo->nextvn), sizeof(rec_smolink));
		CAMLIB_MODIFY_BYTES(camvdo, vdo, SIZEOF_LARGEDISKVNODE);
		nvnodes ++;
	    }
	} while ((i++ < num_vnodes) && (i % VnodePollPeriod));
	CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
	assert(status == 0);			/* Never aborts... */
	LogMsg(9, VolDebugLevel, stdout, "S_VolRestore: Did another series of Vnode restores.");
	PollAndYield();
    }
    LogMsg(9, VolDebugLevel, stdout, "S_VolRestore: Done with vnode restores for large vnodes.");

    if (nvnodes != num_vnodes){	
	LogMsg(0, VolDebugLevel, stdout, "ReadLargeVnodeIndex: #vnodes read != #vnodes dumped");
	return FALSE;
    }
    return TRUE;
}


PRIVATE int ReadSmallVnodeIndex(DumpBuffer_t *buf, Volume *vp)
{
    long vnodenumber;
    int num_vnodes, list_size = 0, status = 0;
    int	nvnodes = 0;
    char    vbuf[SIZEOF_SMALLDISKVNODE];
    VnodeDiskObject *vdo = (VnodeDiskObject *)vbuf;
    register char tag;
    rec_smolist *rlist;
    int volindex = V_volumeindex(vp);

    LogMsg(9, VolDebugLevel, stdout, "Restore: Reading in small vnode array.");
    FreeVnodeIndex(vp, vSmall);
    while ((tag = ReadTag(buf)) > D_MAX && tag != EOF) {
	switch(tag) {
	    case 'v':
		ReadLong(buf, (unsigned long *)&num_vnodes);
		break;
	    case 's':
		ReadLong(buf, (unsigned long *)&list_size);
		break;
	    default:
		LogMsg(0, VolDebugLevel, stdout, "Unexpected field of Vnode found; restore aborted");
		CAMLIB_ABORT(-1);
	}
    }
    PutTag(tag, buf);

    /* Set up a list structure. */
    CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)

    rlist = (rec_smolist *)(CAMLIB_REC_MALLOC(sizeof(rec_smolist) * list_size));
    rec_smolist *vmrlist = (rec_smolist *)malloc(sizeof(rec_smolist) * list_size);
    bzero(vmrlist, sizeof(rec_smolist) * list_size);
    CAMLIB_MODIFY_BYTES(rlist, vmrlist, sizeof(rec_smolist) * list_size);
    free(vmrlist);

    /* Update Volume structure to point to new vnode list. 
     * Doing this here so that if an abort happens during the loop the
     * commited vnodes will be scavanged.
     */
    CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nsmallvnodes, num_vnodes);
    CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.nsmallLists, list_size);
    CAMLIB_MODIFY(CAMLIB_REC(VolumeList[volindex]).data.smallVnodeLists, rlist);

    CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
    assert(status == 0);			/* Never aborts... */
    
    VnodeDiskObject *camvdo;
    long    tmp = 0, i = 0, count = 0;
    while (i < num_vnodes) {
	CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED)
	do {
	    ReadVnodeDiskObject(buf, vdo, NULL, vp, &vnodenumber);
	    LogMsg(19, VolDebugLevel, stdout, "Just read vnode for index %d.", vnodenumber);
	    if (vdo->type != vNull){
		/* this vnode is allocated */
		camvdo=(VnodeDiskObject *)CAMLIB_REC_MALLOC(SIZEOF_SMALLDISKVNODE);
		CAMLIB_MODIFY_BYTES(&(camvdo->nextvn), &tmp, sizeof(long));
		rlist[vnodeIdToBitNumber(vnodenumber)].append(&(camvdo->nextvn));
		vdo->cloned = 0;
		vdo->vol_index = volindex;
		vdo->vnodeMagic = SMALLVNODEMAGIC;
		ViceLockClear((&vdo->lock));
		bcopy(&(camvdo->nextvn), &(vdo->nextvn), sizeof(rec_smolink));
		CAMLIB_MODIFY_BYTES(camvdo, vdo, SIZEOF_SMALLDISKVNODE);
		nvnodes ++;
	    }
 	} while ((i++ < num_vnodes) && (i % VnodePollPeriod));

	CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status)
	if (status != 0)
	    return FALSE;
	LogMsg(9, VolDebugLevel, stdout, "S_VolRestore: Did another series of Vnode restores.");
	PollAndYield();
    }
    LogMsg(9, VolDebugLevel, stdout, "S_VolRestore: Done with vnode restores for small vnodes.");
    
    if (nvnodes != num_vnodes){
	LogMsg(0, VolDebugLevel, stdout, "ReadSmallVnodeIndex: #vnodes read != #vnodes dumped");
	return FALSE;
    }
    return TRUE;
}

PRIVATE void ReadVnodeDiskObject(DumpBuffer_t *buf, VnodeDiskObject *vdop, DirInode **dinode, Volume *vp, long *vnodeNumber)
{
    register char tag;
    *vnodeNumber = -1;
    tag = ReadTag(buf);
    if (tag == D_NULLVNODE){
	vdop->type = vNull;
	return;
    }
    else if (tag != D_VNODE){	/* Shouldn't this be an error? */
	PutTag(tag, buf);
	vdop->type = vNull;
	return;
    }
    
    if (!ReadLong(buf, (unsigned long *)vnodeNumber) || !ReadLong(buf, (unsigned long *)&vdop->uniquifier)) {
	LogMsg(0, VolDebugLevel, stdout, "ReadVnodeDiskObject: Readstuff failed, aborting.");
	CAMLIB_ABORT(-1);
    }

    bzero(&(vdop->nextvn), sizeof(rec_smolink));
    /* Let the following ReadTag catch any errors. */
    while ((tag = ReadTag(buf)) > D_MAX && tag) {
	switch (tag) {
	  case 't':
	    vdop->type = (VnodeType) ReadTag(buf);
	    break;
	  case 'b': 
	    short modeBits;
	    ReadShort(buf, (unsigned short *)&modeBits);
	    vdop->modeBits = modeBits;
	    break;
	  case 'l':
	    ReadShort(buf, (unsigned short *)&vdop->linkCount);
	    break;
	  case 'L':
	    ReadLong(buf, (unsigned long *)&vdop->length);
	    break;
	  case 'v':
	    ReadLong(buf, (unsigned long *)&vdop->dataVersion);
	    break;
	  case 'V':
	    ReadVV(buf, &vdop->versionvector);
	    break;
	  case 'm':
	    ReadLong(buf, (unsigned long *)&vdop->unixModifyTime);
	    break;
	  case 'a':
	    ReadLong(buf, (unsigned long *)&vdop->author);
	    break;
	  case 'o':
	    ReadLong(buf, (unsigned long *)&vdop->owner);
	    break;
	  case 'p':
	    ReadLong(buf, (unsigned long *)&vdop->vparent);
	    break;
	  case 'q':
	    ReadLong(buf, (unsigned long *)&vdop->uparent);
	    break;
	  case 'A':
	    assert(vdop->type == vDirectory);
	    ReadByteString(buf, (byte *)VVnodeDiskACL(vdop), VAclDiskSize(vdop));
	    break;
	}
    }

    if (!tag) {
	LogMsg(0, VolDebugLevel, stdout, "ReadVnodeDiskObject: Readstuff failed, aborting.");
	CAMLIB_ABORT(-1);
    }

    PutTag(tag, buf);
    
    if (vdop->type == vDirectory) {
	int npages = 0;
	assert(ReadTag(buf) == D_DIRPAGES);
	if (!ReadLong(buf, (unsigned long *)&npages) || (npages > MAXPAGES)) {
	    LogMsg(0, VolDebugLevel, stdout, "Restore: Dir has to many pages for vnode %d", *vnodeNumber);
	    CAMLIB_ABORT(-1);
	}
	*dinode = (DirInode *)malloc(sizeof(DirInode));
	bzero(*dinode, sizeof(DirInode));
	for (int i = 0; i < npages; i++){
	    (*dinode)->Pages[i] = (long *)malloc(PAGESIZE);
	    register tmp = ReadTag(buf);
	    if ((byte)tmp != 'P'){
		LogMsg(0, VolDebugLevel, stdout, "Restore: Dir page does not have a P tag");
		CAMLIB_ABORT(-1);
	    }
	    if (!ReadByteString(buf, (byte *)((*dinode)->Pages[i]), PAGESIZE)) {
		LogMsg(0, VolDebugLevel, stdout, "Restore: Failure reading dir page, aborting.");
		CAMLIB_ABORT(-1);
	    }
		
	}
	vdop->inodeNumber = (Inode)(*dinode);
	(*dinode)->refcount = 1;
    } else {
	tag = ReadTag(buf);
	if (tag == D_FILEDATA) {
	    int fd;
	
	    vdop->inodeNumber = icreate((int)vp->device, 0, (int)V_parentId(vp),
					(int)*vnodeNumber, (int)vdop->uniquifier,
					(int)vdop->dataVersion);
	    if (vdop->inodeNumber < 0) {
		LogMsg(0, VolDebugLevel, stdout,
		       "Unable to allocate inode for vnode %d: Restore aborted",
		       *vnodeNumber);
		CAMLIB_ABORT(-1);
	    }
	    fd = iopen((int)vp->device, (int)vdop->inodeNumber, O_WRONLY);
	    if (fd == -1){
		LogMsg(0, VolDebugLevel, stdout, "Failure to open inode for writing %d", errno);
		CAMLIB_ABORT(-1);
	    }

	    FILE *ifile = fdopen(fd, "w");
	    if (ifile == NULL) {
		LogMsg(0, VolDebugLevel, stdout, "Failure to open inode file stream %d", errno);
		CAMLIB_ABORT(-1);
	    }

	    vdop->length = ReadFile(buf, ifile);
	    if (vdop->length == -1) {
		LogMsg(0, VolDebugLevel, stdout, "Failure to read in data for vnode %d: Restore aborted", *vnodeNumber);
		CAMLIB_ABORT(-1);
	    }

	    fclose(ifile);
	    close(fd);
	} else if (tag == D_BADINODE) {
	    /* Create a null inode. */
	    vdop->inodeNumber = icreate((int)vp->device, 0, (int)V_parentId(vp),
					(int)*vnodeNumber, (int)vdop->uniquifier,
					(int)vdop->dataVersion);
	    if (vdop->inodeNumber < 0) {
		LogMsg(0, VolDebugLevel, stdout,
		       "Unable to allocate inode for vnode %d: Restore aborted",
		       *vnodeNumber);
		CAMLIB_ABORT(-1);
	    }
	} else {
	    LogMsg(0, VolDebugLevel, stdout, "Restore: FileData not properly tagged, aborting.");
	    CAMLIB_ABORT(-1);
	}
	
    }
}

