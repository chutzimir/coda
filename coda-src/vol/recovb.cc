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
 * recovb.c:
 * Routines for accessing volume abstractions in recoverable storage
 */

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
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
#include <util.h>

#include <rvmlib.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <vice.h>
#include <rec_smolist.h>
#include "cvnode.h"
#include "volume.h"
#include "vutil.h"
#include "recov.h"
#include "camprivate.h"
#include "coda_globals.h"
#include "volres.h"
#include "volhash.h"

extern void PrintCamVnode(int,int,int,VnodeId, Unique_t);
extern void print_VnodeDiskObject(VnodeDiskObject *);
extern void PrintCamDiskData(int,int,VolumeDiskData*);


int bitmap_flag = 0;

static int DeleteVnode(int, int, VnodeId, Unique_t, VnodeDiskObject *);

/* Copy the specified vnode into the structure provided. Returns 0 if */
/* successful, -1 if an error occurs. */
int ExtractVnode(Error *ec, int volindex, int vclass, 
		  VnodeId vnodeindex, Unique_t uniquifier,
		  VnodeDiskObject *vnode)
{
	rec_smolist *vlist;
	VnodeDiskObject *vind;
	int status = 0;	// transaction status variable
	VolumeId maxid = 0;

	LogMsg(9, VolDebugLevel, stdout,  "Entering ExtractVnode(volindex = %d, vclass = %d, vnodeindex = %x, Unique = %x vnode = 0x%x)",
	       volindex, vclass, vnodeindex, uniquifier, vnode);

	*ec = 0;
	
	maxid = (SRV_RVM(MaxVolId) & 0x00FFFFFF);
	if (volindex < 0 || volindex > maxid || volindex > MAXVOLS) {
		LogMsg(0, VolDebugLevel, stdout,  "ExtractVnode: bogus volume index %d", volindex);
		return(-1);
	}


	if (vclass == vSmall) {
		if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nsmallLists) {
			LogMsg(0, VolDebugLevel, stdout,  "ExtractVnode: bogus small vnode index %d", vnodeindex);
			return(-1);
		}
		vlist = &(SRV_RVM(VolumeList[volindex]).data.smallVnodeLists[vnodeindex]);
	/* set up iterator and return correct vnode */
		rec_smolist_iterator next(*vlist);
		rec_smolink *p;
		while(p = next()){
			vind = strbase(VnodeDiskObject, p, nextvn);
			if (vind->uniquifier == uniquifier){
				bcopy((const void *)vind, (void *)vnode, SIZEOF_SMALLDISKVNODE);
				break;
			}
		}
		if (!p) {
			LogMsg(9, VolDebugLevel, stdout,  "No Vnode with uniq %x at index %x",
			       uniquifier, vnodeindex);
			return(-1);
		}
	}
	else {
		if (vclass == vLarge) {
			if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nlargeLists) {
				LogMsg(0, VolDebugLevel, stdout,  "ExtractVnode: bogus large vnode index %d", vnodeindex);
				return(-1);
			}
			vlist = &(SRV_RVM(VolumeList[volindex]).data.largeVnodeLists[vnodeindex]);
			rec_smolist_iterator next(*vlist);
			rec_smolink *p;
			while(p = next()) {
				vind = strbase(VnodeDiskObject, p, nextvn);
				if (vind->uniquifier == uniquifier) {
					bcopy((const void *)vind, (void *)vnode, SIZEOF_LARGEDISKVNODE);
					break;
				}
			}
			if (!p) {
				LogMsg(9, VolDebugLevel, stdout,  "No Vnode with uniq %x at index %x",
				       uniquifier, vnodeindex);
				return(-1);
			}
		}
	}

	
	if (vnode->type != vNull)
		LogMsg(59, VolDebugLevel, stdout,  "ExtractVnode: vnode->type = %u", vnode->type);
	return(0);
}
/*
 * ObjectExists:
 * 	check if a vnode exists in a volume - 
 * 	return 1 if it does; 0 otherwise
 */
int ObjectExists(int volindex, int vclass, VnodeId vnodeindex, Unique_t u, 
		 ViceFid *ParentFid) {
    rec_smolist *vlist = 0;
    VolumeId maxid = 0;
    VnodeDiskObject *vind;

    LogMsg(9, VolDebugLevel, stdout,  "Entering ObjectExists(volindex= %d, (%x.%x)",
	volindex, vnodeindex, u);

    /* check volume index */
    {    
	maxid = (SRV_RVM(MaxVolId) & 0x00FFFFFF);
	if (volindex < 0 || volindex > maxid || volindex > MAXVOLS) {
	    LogMsg(0, VolDebugLevel, stdout,  "ObjectExists: bogus volume index %d", volindex);
	    return(0);
	}
    }


    if (vclass == vSmall) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nsmallLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "ObjectExists: bogus small vnode index %d", vnodeindex);
	    return(0);
	}
	vlist = &(SRV_RVM(VolumeList[volindex]).data.smallVnodeLists[vnodeindex]);
	/* set up iterator and return correct vnode */
	rec_smolist_iterator next(*vlist);
	rec_smolink *p;
	while(p = next()){
	    vind = strbase(VnodeDiskObject, p, nextvn);
	    if (vind->uniquifier == u) {
		if (ParentFid) {
		    assert(vind->uparent != 0);
		    ParentFid->Vnode = vind->vparent;
		    ParentFid->Unique = vind->uparent;
		}
		return(1);
	    }
	}
	LogMsg(9, VolDebugLevel, stdout,  "ObjectExists: NO  object %x.%x",
	    vnodeindex, u);
	return(0);
    }
    else if (vclass == vLarge) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nlargeLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "ObjectExists: bogus large vnode index %d", vnodeindex);
	    return(0);
	}
	vlist = &(SRV_RVM(VolumeList[volindex]).data.largeVnodeLists[vnodeindex]);
	rec_smolist_iterator next(*vlist);
	rec_smolink *p;
	while(p = next()) {
	    vind = strbase(VnodeDiskObject, p, nextvn);
	    if (vind->uniquifier == u) {
		if (ParentFid) {
		    if (vind->uparent == 0) {
			/* root vnode */
			ParentFid->Vnode = bitNumberToVnodeNumber(vnodeindex, 
								  vLarge);
			ParentFid->Unique = u;
		    }
		    else {
			ParentFid->Vnode = vind->vparent;
			ParentFid->Unique = vind->uparent;
		    }
		}
		return(1);
	    }
	}
	LogMsg(9, VolDebugLevel, stdout,  "ObjectExists: NO  object %x.%x",
	    vnodeindex, u);
	
	return(0);
    }

    return(0);
    
}

/* Get fid of parent of a given fid - the child fid exists
 * This violates locking - but we are assuming this is called only from 
 * resolution where the volume is locked.  So no mutations can occur.
 */
int GetParentFid(Volume *vp, ViceFid *cFid, ViceFid *pFid) {
    rec_smolist *vlist = 0;
    VolumeId maxid = 0;
    VnodeDiskObject *vind;

    LogMsg(9, VolDebugLevel, stdout,  "Entering GetParentFid(%x.%x)", cFid->Vnode, cFid->Unique);

    int volindex = V_volumeindex(vp);
    /* check volume index */
    {    
	maxid = (SRV_RVM(MaxVolId) & 0x00FFFFFF);
	if (volindex < 0 || volindex > maxid || volindex > MAXVOLS) {
	    LogMsg(0, VolDebugLevel, stdout,  "GetParentFid: bogus volume index %d", volindex);
	    return(0);
	}
    }

    unsigned long vclass = vnodeIdToClass(cFid->Vnode);
    unsigned long vnodeindex = vnodeIdToBitNumber(cFid->Vnode);
    if (vclass == vSmall) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nsmallLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "GetParentFid: bogus small vnode index %x", vnodeindex);
	    return(0);
	}
	vlist = &(SRV_RVM(VolumeList[volindex]).data.smallVnodeLists[vnodeindex]);
	/* set up iterator and return correct vnode */
	rec_smolist_iterator next(*vlist);
	rec_smolink *p;
	while(p = next()){
	    vind = strbase(VnodeDiskObject, p, nextvn);
	    if (vind->uniquifier == cFid->Unique) {
		assert(vind->uparent != 0);
		pFid->Volume = cFid->Volume;
		pFid->Vnode = vind->vparent;
		pFid->Unique = vind->uparent;
		return(1);
	    }
	}
	LogMsg(9, VolDebugLevel, stdout,  "GetParentFid: NO  object %x.%x", cFid->Vnode, cFid->Unique);
	return(0);
    }
    else if (vclass == vLarge) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nlargeLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "GetParentFid: bogus large vnode index %x", vnodeindex);
	    return(0);
	}
	vlist = &(SRV_RVM(VolumeList[volindex]).data.largeVnodeLists[vnodeindex]);
	rec_smolist_iterator next(*vlist);
	rec_smolink *p;
	while(p = next()) {
	    vind = strbase(VnodeDiskObject, p, nextvn);
	    if (vind->uniquifier == cFid->Unique) {
		if (vind->uparent != 0) {
		    pFid->Volume = cFid->Volume;
		    pFid->Vnode = vind->vparent;
		    pFid->Unique = vind->uparent;
		}
		else /* root vnode */
		    *pFid = *cFid;
		return(1);
	    }
	}
	LogMsg(9, VolDebugLevel, stdout,  "GetParentFid: NO  object %x.%x", cFid->Vnode, cFid->Unique);
	return(0);
    }
    return(0);
}
int ReplaceVnode(int volindex, int vclass, VnodeId vnodeindex, 
		 Unique_t u, VnodeDiskObject *vnode) {
    char buf1[SIZEOF_SMALLDISKVNODE];
    char buf2[SIZEOF_LARGEDISKVNODE];
    VnodeDiskObject *zerovnode;
    VolumeId maxid = 0;

    LogMsg(9, VolDebugLevel, stdout,  "Entering ReplaceVnode(%u, %u, %u, %ld)", volindex, vclass,
			    vnodeindex, vnode);
    /* if it's been zeroed out, delete the slot */
    if (vnode->type == vNull) {
	LogMsg(9, VolDebugLevel, stdout,  "ReplaceVnode: bogus vnode %u.%u, deleting");
	return(DeleteVnode(volindex, vclass, vnodeindex, u, vnode));
    }


    maxid = (SRV_RVM(MaxVolId) & 0x00FFFFFF);
    if (volindex < 0 || volindex > maxid || volindex > MAXVOLS) {
	LogMsg(0, VolDebugLevel, stdout,  "ReplaceVnode: bogus volume index %d", volindex);
	rvmlib_abort(VFAIL);	// invalid volume index
	return VNOVOL;
    }

    /* if vnodeindex is larger than array, need to alloc a new one and */
    /* copy over the data (must grow by same amount as bitvector!) */

    if (vclass == vSmall) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nsmallLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "ReplaceVnode: bogus small vnode index %d", vnodeindex);
	    rvmlib_abort(VFAIL);	// invalid vnode index
	}
	rec_smolist *vnlist = &(SRV_RVM(VolumeList[volindex]).data.smallVnodeLists[vnodeindex]);
	/* check if vnode already exists */
	VnodeDiskObject *vdo = FindVnode(vnlist, u);
	if (vdo == NULL) {
	    LogMsg(39, VolDebugLevel, stdout,  "ReplaceVnode: no small vnode at index %d; allocating",
				    vnodeindex);
	    /* take a vnode off the free list if one exists */
	    if (SRV_RVM(SmallVnodeIndex) >= 0) {
		LogMsg(9, VolDebugLevel, stdout,  "ReplaceVnode: taking small vnode from FreeList");
		vdo = SRV_RVM(SmallVnodeFreeList[SRV_RVM(SmallVnodeIndex)]);
		RVMLIB_MODIFY(SRV_RVM(SmallVnodeFreeList[SRV_RVM(SmallVnodeIndex)]),
									NULL);
		RVMLIB_MODIFY(SRV_RVM(SmallVnodeIndex),
			      SRV_RVM(SmallVnodeIndex) - 1);
	    }
	    else { /* otherwise, malloc a new one and zero it out */
		LogMsg(9, VolDebugLevel, stdout,  "ReplaceVnode: malloc'ing small vnode");
		vdo = (VnodeDiskObject *)rvmlib_rec_malloc(SIZEOF_SMALLDISKVNODE);
		zerovnode = (VnodeDiskObject *)buf1;
		bzero((void *)zerovnode, sizeof(buf1));
		rvmlib_modify_bytes(vdo, zerovnode, sizeof(buf1));
	    }
	    /* increment vnode count */
	    RVMLIB_MODIFY(SRV_RVM(VolumeList[volindex]).data.nsmallvnodes,
			(SRV_RVM(VolumeList[volindex]).data.nsmallvnodes) + 1);
    	    /* append vnode into the appropriate rec_smolist */
	    char buf[sizeof(rec_smolink)];
	    bzero(buf, sizeof(rec_smolink));
	    if (bcmp((const void *)&(vdo->nextvn), (const void *) buf, sizeof(rec_smolink))){
		LogMsg(0, VolDebugLevel, stdout,  "ERROR: REC_SMOLINK ON VNODE DURING ALLOCATION WAS NOT ZERO");
		rvmlib_modify_bytes(&(vdo->nextvn), buf, sizeof(rec_smolink));
	    }
	    vnlist->append(&(vdo->nextvn));
	}
	bcopy( (const void *)&(vdo->nextvn), (void *)&(vnode->nextvn), sizeof(rec_smolink));
	rvmlib_modify_bytes(vdo, vnode, SIZEOF_SMALLDISKVNODE);
    }
    else if (vclass == vLarge) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nlargeLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "ReplaceVnode: bogus large vnode index %d", vnodeindex);
	    rvmlib_abort(VFAIL);	// invalid vnode index
	}
	rec_smolist *vnlist = &(SRV_RVM(VolumeList[volindex]).data.largeVnodeLists[vnodeindex]);
	/* check if vnode already exists */
	VnodeDiskObject *vdo = FindVnode(vnlist, u);
	if (vdo == NULL){
	    LogMsg(39, VolDebugLevel, stdout,  "ReplaceVnode: no large vnode at index %d; allocating",
				    vnodeindex);
	    /* take a vnode off the free list if one exists */
	    if (SRV_RVM(LargeVnodeIndex) >= 0) {
		LogMsg(9, VolDebugLevel, stdout,  "ReplaceVnode: taking large vnode from freelist");
		LogMsg(19, VolDebugLevel, stdout,  "Taking vnode off of largefreelist[%d] for index %d",
				    SRV_RVM(LargeVnodeIndex), vnodeindex);
		vdo = SRV_RVM(LargeVnodeFreeList[SRV_RVM(LargeVnodeIndex)]);
		RVMLIB_MODIFY(SRV_RVM(LargeVnodeFreeList[SRV_RVM(LargeVnodeIndex)]), NULL);
		RVMLIB_MODIFY(SRV_RVM(LargeVnodeIndex),
			      SRV_RVM(LargeVnodeIndex) - 1);
	    }
	    else { /* otherwise, malloc a new one */
		LogMsg(9, VolDebugLevel, stdout,  "ReplaceVnode: malloc'ing large vnode");
		vdo = (VnodeDiskObject *)rvmlib_rec_malloc(SIZEOF_LARGEDISKVNODE);
		zerovnode = (VnodeDiskObject *)buf2;
		bzero((void *)zerovnode, sizeof(buf2));
		rvmlib_modify_bytes(vdo, zerovnode, sizeof(buf2));
	    }
	    /* increment vnode count */
	    RVMLIB_MODIFY(SRV_RVM(VolumeList[volindex]).data.nlargevnodes,
		  (SRV_RVM(VolumeList[volindex]).data.nlargevnodes) + 1);
	    char buf[sizeof(rec_smolink)];
	    bzero(buf, sizeof(rec_smolink));
	    if (bcmp((const void *)&(vdo->nextvn),(const void *) buf, sizeof(rec_smolink))){
		LogMsg(0, VolDebugLevel, stdout,  "ERROR: REC_SMOLINK ON VNODE DURING ALLOCATION WAS NOT ZERO");
		rvmlib_modify_bytes(&(vdo->nextvn), buf, sizeof(rec_smolink));
	    }
	    vnlist->append(&(vdo->nextvn));
	    if (AllowResolution) {
		/* allocate res log header */
		assert(AllocateResLog(volindex, 
		    bitNumberToVnodeNumber(vnodeindex, vLarge), u));
	    }
	}
	bcopy((char *)&(vdo->nextvn), (char *)&(vnode->nextvn), sizeof(rec_smolink));
	/* store the data into recoverable storage */
	rvmlib_modify_bytes(vdo, vnode, SIZEOF_LARGEDISKVNODE);
    }
    else assert(0);	/* vclass is neither vSmall nor vLarge */
    LogMsg(19, VolDebugLevel, stdout, "Replace vnode - VnodeDiskObject passed to rtn:");
    if (VolDebugLevel > 19)  
	print_VnodeDiskObject(vnode);
    PrintCamVnode(19, volindex, vclass, vnodeindex, u);
    return(0);
}

static int DeleteVnode(int volindex, int vclass, VnodeId vnodeindex, 
			 Unique_t u, VnodeDiskObject *vnode)
{
    VolumeId maxid = 0;

    LogMsg(9, VolDebugLevel, stdout,  
	   "Entering DeleteVnode(%d, %d, %d, <struct>)", 
	   volindex, vclass, vnodeindex);
    maxid = (SRV_RVM(MaxVolId) & 0x00FFFFFF);
    if (volindex < 0 || volindex > maxid || volindex > MAXVOLS) {
	LogMsg(0, VolDebugLevel, stdout,  "DeleteVnode: bogus volume index %d", volindex);
	rvmlib_abort(VFAIL);	// invalid volume index
    }

    if (vclass == vSmall) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nsmallLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "DeleteVnode: deleting nonexistent vnode (index %d)", vnodeindex);
	    rvmlib_abort(VFAIL);
	}

	rec_smolist *vnlist = &(SRV_RVM(VolumeList[volindex]).data.smallVnodeLists[vnodeindex]);
	VnodeDiskObject *vdo = FindVnode(vnlist, u);
	if (vdo != NULL) {
	    /* remove vnode from index */
	    vnlist->remove(&(vdo->nextvn));
	    /* put the freed vnode on the free list if there's room */
	    if (SRV_RVM(SmallVnodeIndex) < SMALLFREESIZE - 1) {
		LogMsg(9, VolDebugLevel, stdout,  "DeleteVnode: putting small vnode on freelist");
		bzero((void *)vnode, SIZEOF_SMALLDISKVNODE); /* just to be sure */
		rvmlib_modify_bytes(vdo, vnode, SIZEOF_SMALLDISKVNODE);
		RVMLIB_MODIFY(SRV_RVM(SmallVnodeIndex),
			      SRV_RVM(SmallVnodeIndex) + 1);
		RVMLIB_MODIFY(SRV_RVM(SmallVnodeFreeList[SRV_RVM(SmallVnodeIndex)]), vdo);
	    }
	    else {
		LogMsg(9, VolDebugLevel, stdout,  "DeleteVnode: freeing small vnode structure");
		rvmlib_rec_free((char *)vdo);
	    }
	
	    /* decrement small vnode count */
	    RVMLIB_MODIFY(SRV_RVM(VolumeList[volindex]).data.nsmallvnodes,
		    (SRV_RVM(VolumeList[volindex]).data.nsmallvnodes) - 1);
	}
    }

    if (vclass == vLarge) {
	if (vnodeindex >= SRV_RVM(VolumeList[volindex]).data.nlargeLists) {
	    LogMsg(0, VolDebugLevel, stdout,  "DeleteVnode: deleting nonexistent vnode (index %d)", vnodeindex);
	    rvmlib_abort(VFAIL);
	}
	rec_smolist *vnlist = &(SRV_RVM(VolumeList[volindex]).data.largeVnodeLists[vnodeindex]);
	VnodeDiskObject *vdo = FindVnode(vnlist, u);
	if (vdo != NULL) {
	    vnlist->remove(&(vdo->nextvn));
	    /* put the removed vnode on the free list if there's room */
	    if (SRV_RVM(LargeVnodeIndex) < LARGEFREESIZE - 1) {
		LogMsg(9, VolDebugLevel, stdout,  "DeleteVnode: putting large vnode on free list");
		bzero((void *)vnode, SIZEOF_LARGEDISKVNODE);    /* just to be sure */
		rvmlib_modify_bytes(vdo, vnode,	SIZEOF_LARGEDISKVNODE);
		RVMLIB_MODIFY(SRV_RVM(LargeVnodeIndex),
			      SRV_RVM(LargeVnodeIndex) + 1);
		RVMLIB_MODIFY(SRV_RVM(LargeVnodeFreeList[SRV_RVM(LargeVnodeIndex)]), vdo);
	    }
	    else {
		LogMsg(9, VolDebugLevel, stdout,  "DeleteVnode: freeing large vnode");
		rvmlib_rec_free((char *)vdo);
	    }
	    /* decrement large vnode count */
	    RVMLIB_MODIFY(SRV_RVM(VolumeList[volindex]).data.nlargevnodes,
		  (SRV_RVM(VolumeList[volindex]).data.nlargevnodes) - 1);
	    if (AllowResolution) 
		/* delete the resolution log list header */
		DeAllocateVMResLogListHeader(volindex, 
			bitNumberToVnodeNumber(vnodeindex, vLarge), u);
	}
    }
    PrintCamVnode(19, volindex, vclass, vnodeindex, u);
    return(0);
}


/* initialize a new volumediskinfo structure and store it in */
/* the appropriate slot in recoverable storage */
/* Note: volindex is checked in ReplaceVolDiskInfo */
void NewVolDiskInfo(Error *ec, int volindex, VolumeDiskData *vol)
{
    LogMsg(9, VolDebugLevel, stdout,  "Entering NewVolDiskInfo for index %d, volume %x",
						volindex, vol->id);
    /* how much needs to be initalized here? */
    InitVV(&(vol->versionvector));
    vol->stamp.magic = VOLUMEINFOMAGIC;
    vol->stamp.version = VOLUMEINFOVERSION;
    ReplaceVolDiskInfo(ec, volindex, vol);
}

/* Extracts the VolumeDiskInfo for the specified volume, and returns the */
/* volume's index in recoverable storage */
int VolDiskInfoById(Error *ec, VolumeId volid, VolumeDiskData *vol) {

    int myind = -1;

    *ec = 0;

    LogMsg(9, VolDebugLevel, stdout,  "Entering VolDiskInfoById for volume %x", volid);
    myind = HashLookup(volid);
    if (myind == -1) {
	LogMsg(0, VolDebugLevel, stdout,  "VolDiskInfoById: HashLookup failed for volume %x", volid);
	*ec = VNOVOL;  /* volume not found */
    }
    else {
	ExtractVolDiskInfo(ec, myind, vol);
    }

    LogMsg(29, VolDebugLevel, stdout,  "VolDiskInfoById: vol->stamp.magic = %u, vol->stamp.version = %u",
					vol->stamp.magic, vol->stamp.version);
    return (myind);
}

/* Must be called from within a transaction */
void ReplaceVolDiskInfo(Error *ec, int volindex, VolumeDiskData *vol)
{
    int status = 0;	/* transaction status variable */
    VolumeId maxid = 0;
    *ec = 0;

    LogMsg(9, VolDebugLevel, stdout,  "Entering ReplaceVolDiskInfo for volume index %d", volindex);
    /* consistency check */
    assert(vol->stamp.magic == VOLUMEINFOMAGIC);
    assert(vol->stamp.version == VOLUMEINFOVERSION);

    maxid = (SRV_RVM(MaxVolId) & 0x00FFFFFF);
    if ((volindex < 0) || (volindex > maxid) || (volindex > MAXVOLS)) {
	char volname[32];
	sprintf(volname, VFORMAT, vol->id);
	LogMsg(0, VolDebugLevel, stdout,  "ReplaceVolDiskInfo: bogus volume index %d for volume %s",
			    volindex, volname);
	*ec = VNOVOL;	// invalid volume index
	rvmlib_abort(VFAIL);
    }

    LogMsg(1, VolDebugLevel, stdout,  "ReplaceVolDiskInfo: about to acquire locks");
    LogMsg(1, VolDebugLevel, stdout,  "ReplacevolDiskInfo: got locks!");
    rvmlib_modify_bytes(SRV_RVM(VolumeList[volindex]).data.volumeInfo,
				    vol, sizeof(VolumeDiskData));
    LogMsg(29, VolDebugLevel, stdout,  "ReplaceVolDiskInfo: recoverable stamp = %u, %u",
	    SRV_RVM(VolumeList[volindex]).data.volumeInfo->stamp.magic,
	    SRV_RVM(VolumeList[volindex]).data.volumeInfo->stamp.version);

    PrintCamDiskData(29, volindex,
			SRV_RVM(VolumeList[volindex].data.volumeInfo));
}


/* find a vnode with uniquifier u in a given index */
VnodeDiskObject *FindVnode(rec_smolist *vnlist, Unique_t u) {
    rec_smolist_iterator next(*vnlist);
    rec_smolink *p;
    VnodeDiskObject *vdo;
    while (p = next()) {
	vdo = strbase(VnodeDiskObject, p, nextvn);
	if (vdo->uniquifier == u)
	    return(vdo);
    }
    return(NULL);
}

