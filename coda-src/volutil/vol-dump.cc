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

#define RCSVERSION $Revision$

/* vol-dump.c */

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>

#include <ctype.h>
#include <sys/param.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/file.h>
#include <netdb.h>
#include <netinet/in.h>

#include <unistd.h>
#include <stdlib.h>

#include "coda_assert.h"
#include <struct.h>
#include <lwp.h>
#include <lock.h>
#include <rpc2.h>
#include <inodeops.h>
#include <util.h>
#include <rvmlib.h>
#include <codadir.h>
#include <partition.h>
#include <volutil.h>
#include <vice.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <voltypes.h>
#include <cvnode.h>
#include <volume.h>
#include <errors.h>
#include <viceinode.h>
#include <vutil.h>
#include <index.h>
#include <recov.h>
#include <camprivate.h>
#include <coda_globals.h>
#include <vrdb.h>
#include <srv.h>
#include <voldump.h>

#include "vvlist.h"
#include "dump.h"

extern void PollAndYield();
static int VnodePollPeriod = 32;     /* How many vnodes to dump before polling */

int VVListFd = -1;
int DumpFd = -1;
FILE *Ancient = NULL;

#define DUMPFILE "/tmp/volumedump"

static int DumpVnodeIndex(DumpBuffer_t *, Volume *, VnodeClass, 
			  RPC2_Unsigned);
static int DumpDumpHeader(DumpBuffer_t *, Volume *, RPC2_Unsigned, long);
static int DumpVolumeDiskData(DumpBuffer_t *, register VolumeDiskData *);
static int DumpVnodeDiskObject(DumpBuffer_t *, struct VnodeDiskObject *, 
			       int );
Device DumpDev;   /* Device the volume being dumped resides on */

#if (LISTLINESIZE >= SIZEOF_LARGEDISKVNODE)	/* Compile should fail.*/
Help, LISTLINESIZE >= SIZEOF_LARGEDISKVNODE)!
#endif

#define DUMPBUFSIZE 512000
long S_VolDump(RPC2_Handle rpcid)
{
    SLog(0,0,stdout,"S_VolDump called -- should be S_VolNewDump!");
    return -1;
}

/*
  S_VolNewDump: Dump the contents of the requested volume into a file in a 
  host independent manner
*/
long S_VolNewDump(RPC2_Handle rpcid, RPC2_Unsigned formal_volumeNumber, 
	RPC2_Unsigned *Incremental)
{
    register Volume *vp = 0;
    long rc = 0, retcode = 0;
    Error error;
    ProgramType *pt;
    DumpBuffer_t *dbuf = NULL;
    char *DumpBuf = 0;
    RPC2_HostIdent hid;
    RPC2_PortIdent pid;
    RPC2_SubsysIdent sid;
    RPC2_BindParms bparms;
    RPC2_Handle cid;
    RPC2_PeerInfo peerinfo;

    /* To keep C++ 2.0 happy */
    VolumeId volumeNumber = (VolumeId)formal_volumeNumber;

    CODA_ASSERT(LWP_GetRock(FSTAG, (char **)&pt) == LWP_SUCCESS);

    SLog(9, "S_VolNewDump: conn: %d, volume:  %#x, Inc?: %u", 
	 rpcid, volumeNumber, *Incremental);
    rc = VInitVolUtil(volumeUtility);
    if (rc != 0) {
	return rc;
    }

    vp = VGetVolume(&error, volumeNumber);    
    if (error) {
	SLog(0, "Unable to get the volume %x, not dumped", volumeNumber);
	VDisconnectFS();
	return (int)error;
    }

    SLog(0, "volumeNumber %x V_id %x", volumeNumber, V_id(vp));
    
    /* Find the vrdb entry for the parent volume */
    vrent *vre = VRDB.ReverseFind(V_parentId(vp));

    if (V_type(vp) == RWVOL) {
	SLog(0, "Volume is read-write.  Dump not allowed");
	retcode = VFAIL;
	goto failure;
    }

    DumpBuf = (char *)malloc(DUMPBUFSIZE);
    if (!DumpBuf) {
	SLog(0, "S_VolDumpHeader: Can't malloc buffer!");
	retcode = VFAIL;
	goto failure;
    }
    
    long volnum, unique;
    if (vre != NULL) {	/* The volume is replicated */
	/* Look up the index of this host. */
	int ix = vre->index(ThisHostAddr);
	if (ix < 0) {
	    SLog(0, "S_VolDumpHeader: this host not found!");
	    retcode = VFAIL;
	    goto failure;
	}

	/* Uniquely identify incremental via primary slot of VVV */
	unique = (&V_versionvector(vp).Versions.Site0)[ix]; 
	volnum = vre->volnum;
    } else {		/* The volume is non-replicated (I hope) */
	volnum = 0; /* parent volume, nonexistent in the case... */
	/* Uniquely identify incrementals of non-rep volumes by updateDate */
	unique = V_updateDate(vp);
    }

    char VVlistfile[MAXLISTNAME];
    getlistfilename(VVlistfile, volnum, V_parentId(vp), "newlist");
    SLog(0, "NewDump: file %s volnum %x id %x parent %x",
	VVlistfile, volnum, volumeNumber, V_parentId(vp));
    VVListFd = open(VVlistfile, O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (VVListFd < 0) {
	SLog(0, "S_VolDumpHeader: Couldn't open VVlistfile.");
	retcode = VFAIL;
	goto failure;
    }

    if (*Incremental) {
	char listfile[MAXLISTNAME];
	getlistfilename(listfile, volnum, V_parentId(vp), "ancient");

	Ancient = fopen(listfile, "r");
	if (Ancient == NULL) {
	    SLog(0, "S_VolDump: Couldn't open Ancient vvlist %s, will do a full backup instead.", listfile);
	    *Incremental = 0;
	} else 
	    SLog(9, "Dump: Just opened listfile %s", listfile);
    }

    /* Set up a connection with the client. */
    if ((rc = RPC2_GetPeerInfo(rpcid, &peerinfo)) != RPC2_SUCCESS) {
	SLog(0,"VolDump: GetPeerInfo failed with %s", RPC2_ErrorMsg((int)rc));
	retcode = rc;
	goto failure;
    }

    hid = peerinfo.RemoteHost;
    pid = peerinfo.RemotePort;
    sid.Tag = RPC2_SUBSYSBYID;
    sid.Value.SubsysId = VOLDUMP_SUBSYSTEMID;
    bparms.SecurityLevel = RPC2_OPENKIMONO;
    bparms.SideEffectType = SMARTFTP;
    bparms.EncryptionType = 0;
    bparms.ClientIdent = NULL;
    bparms.SharedSecret = NULL;
    
    if ((rc = RPC2_NewBinding(&hid, &pid, &sid, &bparms, &cid))!=RPC2_SUCCESS) {
	SLog(0, "VolDump: Bind to client failed, %s!", RPC2_ErrorMsg((int)rc));
	retcode = rc;
	goto failure;
    }

    /* Overload the fd parameter if using newstyle dump -- this will go away. */
    dbuf = InitDumpBuf((byte *)DumpBuf, (long)DUMPBUFSIZE, V_id(vp), cid); 
    DumpListVVHeader(VVListFd, vp, (int)*Incremental, (int)unique);/* Dump the volume.*/
    if ((DumpDumpHeader(dbuf, vp, *Incremental, unique) == -1) ||
	(DumpVolumeDiskData(dbuf, &V_disk(vp)) == -1)	      ||
	(DumpVnodeIndex(dbuf, vp, vLarge, *Incremental) == -1) ||
	(DumpVnodeIndex(dbuf, vp, vSmall, *Incremental) == -1) ||
	(DumpEnd(dbuf) == -1)) {
	SLog(0, "Dump failed due to FlushBuf failure.");
	retcode = VFAIL;
    }

failure:

    close(VVListFd);
    if (Ancient)
	fclose(Ancient);

    /* zero the pointer, so we won't re-close it */
    Ancient = NULL;
    
    VDisconnectFS();
    VPutVolume(vp);
    
    if (dbuf) {
	SLog(2, "Dump took %d seconds to dump %d bytes.",
	     dbuf->secs, dbuf->nbytes);
	free(dbuf);
    }
    if (DumpBuf) 
	    free(DumpBuf);

    if (RPC2_Unbind(cid) != RPC2_SUCCESS) {
	SLog(0, "S_VolNewDump: Can't close binding %s", 
	     RPC2_ErrorMsg((int)rc));
    }
    
    if (retcode == 0) {
	SLog(0, "S_VolNewDump: %s volume dump succeeded",
	    *Incremental?"Incremental":"");
	return 0;
    }

    unlink(VVlistfile);
    SLog(0, "S_VolNewDump: %s volume dump failed with status = %d",
	*Incremental?"Incremental":"", retcode);

    return retcode;
}


/* Guts of the dump code */

static int DumpDumpHeader(DumpBuffer_t *dbuf, Volume *vp, RPC2_Unsigned Incremental, long unique)
{
    DumpDev = V_device(vp);
    DumpDouble(dbuf, (byte) D_DUMPHEADER, DUMPBEGINMAGIC, DUMPVERSION);
    DumpLong(dbuf, 'v', V_id(vp));
    DumpLong(dbuf, 'p', V_parentId(vp));
    DumpString(dbuf, 'n',V_name(vp));
    DumpLong(dbuf, 'b', V_copyDate(vp));	/* Date the backup clone was made */
    DumpLong(dbuf, 'i', Incremental);

    if (Incremental) {
	/* Read in the header, hope it doesn't break */
	int oldUnique;
	if (!ValidListVVHeader(Ancient, vp, &oldUnique)) {
	    SLog(0, "Dump: Ancient list file has invalid header");
	    return -1;
	}

	return DumpDouble(dbuf, (byte) 'I', oldUnique, unique);
    } else
	/* Full dumps are w.r.t themselves */
	return DumpDouble(dbuf, (byte) 'I', unique, unique);
}

static int DumpVolumeDiskData(DumpBuffer_t *dbuf, register VolumeDiskData *vol)
{
    DumpTag(dbuf, (byte) D_VOLUMEDISKDATA);
    DumpLong(dbuf, 'i',vol->id);
    DumpLong(dbuf, 'v',vol->stamp.version);
    DumpString(dbuf, 'n',vol->name);
    DumpString(dbuf, 'P',vol->partition);
    DumpBool(dbuf, 's',vol->inService);
    DumpBool(dbuf, '+',vol->blessed);
    DumpLong(dbuf, 'u',vol->uniquifier);
    DumpByte(dbuf, 't',vol->type);
    DumpLong(dbuf, 'p',vol->parentId);
    DumpLong(dbuf, 'g',vol->groupId);
    DumpLong(dbuf, 'c',vol->cloneId);
    DumpLong(dbuf, 'b',vol->backupId);
    DumpLong(dbuf, 'q',vol->maxquota);
    DumpLong(dbuf, 'm',vol->minquota);
    DumpLong(dbuf, 'x',vol->maxfiles);
    DumpLong(dbuf, 'd',vol->diskused);
    DumpLong(dbuf, 'f',vol->filecount);
    DumpShort(dbuf, 'l',(int)(vol->linkcount));
    DumpLong(dbuf, 'a', vol->accountNumber);
    DumpLong(dbuf, 'o', vol->owner);
    DumpLong(dbuf, 'C',vol->creationDate);	/* Rw volume creation date */
    DumpLong(dbuf, 'A',vol->accessDate);
    DumpLong(dbuf, 'U',vol->updateDate);
    DumpLong(dbuf, 'E',vol->expirationDate);
    DumpLong(dbuf, 'B',vol->backupDate);		/* Rw volume backup clone date */
    DumpString(dbuf, 'O',vol->offlineMessage);
    DumpString(dbuf, 'M',vol->motd);
    DumpArrayLong(dbuf, 'W', (unsigned long *)vol->weekUse, sizeof(vol->weekUse)/sizeof(vol->weekUse[0]));
    DumpLong(dbuf, 'D', vol->dayUseDate);
    DumpLong(dbuf, 'Z', vol->dayUse);
    return DumpVV(dbuf, 'V', &(vol->versionvector));
}

static int DumpVnodeIndex(DumpBuffer_t *dbuf, Volume *vp, 
			  VnodeClass vclass, RPC2_Unsigned Incremental)
{
    register struct VnodeClassInfo *vcp;
    char buf[SIZEOF_LARGEDISKVNODE];
    struct VnodeDiskObject *vnode;
    bit32	nLists, nVnodes;
    
    SLog(9, "Entering DumpVnodeIndex()");

    vcp = &VnodeClassInfo_Array[vclass];

    if (vclass == vLarge) {
	DumpTag(dbuf, (byte) D_LARGEINDEX);
	nVnodes = SRV_RVM(VolumeList[V_volumeindex(vp)]).data.nlargevnodes;
	nLists = SRV_RVM(VolumeList[V_volumeindex(vp)]).data.nlargeLists;
    } else {
	DumpTag(dbuf, (byte) D_SMALLINDEX);
	nVnodes = SRV_RVM(VolumeList[V_volumeindex(vp)]).data.nsmallvnodes;
	nLists = SRV_RVM(VolumeList[V_volumeindex(vp)]).data.nsmallLists;
    }
    DumpLong(dbuf, 'v', nVnodes);
    if (DumpLong(dbuf, 's', nLists) == -1)
	return -1;
    
    sprintf(buf, "Start of %s list, %ld vnodes, %ld lists.\n",
	    ((vclass == vLarge)? "Large" : "Small"), nVnodes, nLists);
    if (write(VVListFd, buf, strlen(buf)) != (int)strlen(buf)) {
	SLog(0, "Write %s Index header didn't succeed.", ((vclass == vLarge)? "Large" : "Small"));
	VPutVolume(vp);
	return -1;
    }

   /* Currently we have two counts of vnodes: nVnodes and nvnodes; the # of vnodes
    * in the real volume and the # of vnodes in the vvlist. However, we are not
    * exploiting this info as a sanity check. Should we be doing this? It's hard
    * because we currently can't tell the difference between creation and
    * modification of vnodes.
    */

    vindex v_index(V_id(vp), vclass, V_device(vp), vcp->diskSize);
    vindex_iterator vnext(v_index);
	
       if (Incremental) {
	SLog(9, "Beginning Incremental dump of vnodes.");

	/* Determine how many entries in the list... */
	if (fgets(buf, LISTLINESIZE, Ancient) == NULL) {
	    SLog(10, "Dump: fgets indicates error."); /* Abort? */
	}

	/* Read in enough info from vvlist file to start a vvlist class object. */
	long nvnodes;
	int nlists;
	char Class[7];
	
	if (sscanf(buf, "Start of %s list, %ld vnodes, %d lists.\n",
		   Class, &nvnodes, &nlists)!=3) { 
	    SLog(0, "Couldn't scan head of %s Index.", 
		(vclass==vLarge?"Large":"Small"));
	    VPutVolume(vp);
	    return -1;
	}

	CODA_ASSERT(strcmp(Class,((vclass == vLarge)? "Large" : "Small")) == 0);
	SLog(9, "Ancient clone had %d vnodes, %d lists.", nvnodes, nlists);

	rec_smolist *vnList;
	vvtable vvlist(Ancient, vclass, nlists);

	if (vclass == vLarge) 
	    vnList = SRV_RVM(VolumeList[V_volumeindex(vp)]).data.largeVnodeLists;
	else
	    vnList = SRV_RVM(VolumeList[V_volumeindex(vp)]).data.smallVnodeLists;
	/* Foreach list, check to see if vnodes on the list were created,
	 * modified, or deleted.
	 */
	
	for (int vnodeIndex = 0; vnodeIndex < (int)nLists; vnodeIndex++) {
	    rec_smolist_iterator nextVnode(vnList[vnodeIndex]);
	    rec_smolink *vptr;
	    
	    while ( (vptr = nextVnode()) ) {	/* While more vnodes */
		vnode = strbase(VnodeDiskObject, vptr, nextvn);
		int VnodeNumber = bitNumberToVnodeNumber(vnodeIndex, vclass);
		nVnodes--;

		/* If the vnode was modified or created, add it to dump. */
		if (vvlist.IsModified(vnodeIndex, vnode->uniquifier,
				      &(vnode->versionvector.StoreId))) {
		    if (DumpVnodeDiskObject(dbuf, vnode, VnodeNumber) == -1) {
			SLog(0, 0, stdout, "DumpVnodeDiskObject (%s) failed.",
			       (vclass == vLarge) ? "large" : "small");
			return -1;
		    }
		    SLog(9,
			   "Dump: Incremental found %x.%x modified.",
			   VnodeNumber, vnode->uniquifier);
		    if ((vnodeIndex % VnodePollPeriod) == 0)
			PollAndYield();
		}

		/* No matter what, add the vnode to our new list. */
		ListVV(VVListFd, VnodeNumber, vnode);
		
	    }
	    
	    /* Check for deleted files. If there exists an entry which wasn't
	     * marked by isModified(), put a delete record for it in the dump.
	     */
	    vvent_iterator vvnext(vvlist, vnodeIndex);
	    vvent *vventry;
		
	    while ( (vventry = vvnext()) ) { 
		if (!vventry->isThere) {
		    /* Note: I could define VnodeNumber in the outer loop and
		     * seemingly save effort. However, it would be done for every
		     * list and since only 1 in (repfactor) is non-zero, seems
		     * like even more of a waste to me.  --- DCS
		     */
		    
		    int VnodeNumber = bitNumberToVnodeNumber(vnodeIndex, vclass);
		    if (DumpDouble(dbuf, (byte) D_RMVNODE, VnodeNumber, vventry->unique) == -1) {
			SLog(0, 0, stdout, "Dump RMVNODE failed, aborting.");
			return -1;
		    }
		    SLog(9,
			   "Dump: Incremental found %x.%x deleted.",
			   VnodeNumber, vventry->unique);
		}
	    }
	}
    } else {
	SLog(9, "Beginning Full dump of vnodes.");
	int count = 0;

	vnode = (struct VnodeDiskObject *) buf;
	for( int vnodeIndex = 0;
	     nVnodes && ((vnodeIndex = vnext(vnode)) != -1);
	     nVnodes--, count++) {
	    int VnodeNumber = bitNumberToVnodeNumber(vnodeIndex, vclass);
	    if (DumpVnodeDiskObject(dbuf, vnode, VnodeNumber) == -1) {
		SLog(0, "DumpVnodeDiskObject (%s) failed.",
		    (vclass == vLarge) ? "large" : "small");
		return -1;
	    }
	    
	    ListVV(VVListFd, VnodeNumber, vnode);
	    if ((count % VnodePollPeriod) == 0)
		PollAndYield();
       }
	CODA_ASSERT(vnext(vnode) == -1);
    }

    if (vclass == vLarge) { 	/* Output End of Large Vnode list */
	    sprintf(buf, "%s", ENDLARGEINDEX);	/* Macro contains a new-line */
	    if (write(VVListFd, buf, strlen(buf)) != (int)strlen(buf))
		    SLog(0, "EndLargeIndex write didn't succeed.");
    }
    SLog(9, "Leaving DumpVnodeIndex()");
    return 0;
}

static int DumpVnodeDiskObject(DumpBuffer_t *dbuf, struct VnodeDiskObject *v, int vnodeNumber)
{
    int fd;
    int i;
    SLog(9, "Dumping vnode number %x", vnodeNumber);
    if (!v || v->type == vNull) {
	return DumpTag(dbuf, (byte) D_NULLVNODE);
    }
    DumpDouble(dbuf, (byte) D_VNODE, vnodeNumber, v->uniquifier);
    DumpByte(dbuf, 't', v->type);
    DumpShort(dbuf, 'b', v->modeBits);
    DumpShort(dbuf, 'l', v->linkCount); /* May not need this */
    DumpLong(dbuf, 'L', v->length);
    DumpLong(dbuf, 'v', v->dataVersion);
    DumpVV(dbuf, 'V', (ViceVersionVector *)(&(v->versionvector)));
    DumpLong(dbuf, 'm', v->unixModifyTime);
    DumpLong(dbuf, 'a', v->author);
    DumpLong(dbuf, 'o', v->owner);
    DumpLong(dbuf, 'p', v->vparent);
    if (DumpLong(dbuf, 'q', v->uparent) == -1)
	return -1;
    
    if (v->type != vDirectory) {
	if (v->inodeNumber) {
	    fd = iopen((int)DumpDev, (int)v->inodeNumber, O_RDONLY);
	    if (fd < 0) {

		/* Instead of causing the dump to fail, I should just stick in
		   a null marker, which will cause an empty inode to be created
		   upon restore.
		 */
		
		SLog(0, 0, stdout,
		       "dump: Unable to open inode %d for vnode 0x%x; not dumped",
		       v->inodeNumber, vnodeNumber);
		DumpTag(dbuf, (byte) D_BADINODE);
	    } else {
		if (DumpFile(dbuf, D_FILEDATA, fd, vnodeNumber) == -1) {
		    SLog(0, "Dump: DumpFile failed.");
		    return -1;
		}
		close(fd);
	    }
	} else {
	    SLog(0, 0, stdout,
		   "dump: ACKK! Found barren inode in RO volume. (%x.%x)\n",
		   vnodeNumber, v->uniquifier);
	    DumpTag(dbuf, (byte) D_BADINODE);
	}
    } else {
            DirInode *dip;
	int size;

	CODA_ASSERT(v->inodeNumber != 0);
	dip = (DirInode *)(v->inodeNumber);

	/* Dump the Access Control List */
	if (DumpByteString(dbuf, 'A', (byte *) VVnodeDiskACL(v), VAclDiskSize(v)) == -1) {
	    SLog(0, "DumpVnode: BumpByteString failed.");
	    return -1;
	}

	/* Count number of pages in DirInode */
        size = DI_Pages(dip);
	if (DumpLong(dbuf, D_DIRPAGES, size) == -1)
		return -1;

	for ( i = 0; i < size; i++) {
		if (DumpByteString(dbuf, (byte)'P', (byte *)DI_Page(dip, i), DIR_PAGESIZE) == -1)
		return -1;
	}

	SLog(9, "DumpVnode finished dumping directory");
    }
    return 0;
}

    

