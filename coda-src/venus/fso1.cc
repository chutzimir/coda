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
 *
 *    Implementation of the Venus File-System Object (fso) abstraction.
 *
 *    ToDo:
 *       1. Need to allocate meta-data by priority (escpecially in the case of dir pages and modlog entries)
 */


/* Following block is shared with worker.c. */
/* It is needed to ensure that C++ makes up "anonymous types" in the same order.  It sucks! */
#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <struct.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#ifdef __MACH__
#include <sysent.h>
#include <libc.h>
#else	/* __linux__ || __BSD44__ */
#include <unistd.h>
#include <stdlib.h>
#endif
#include <rpc2.h>

#include <math.h>

#ifdef __cplusplus
}
#endif __cplusplus

/* interfaces */
#include <vice.h>

/* cfs/{cfs,cnode}.h must follow vice.h */
#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <cfs/coda.h>

#ifdef __cplusplus
}
#endif __cplusplus

/* from vicedep */
#include <venusioctl.h>

/* from venus */
#include "advice.h"
#include "advice_daemon.h"
#include "comm.h"
#include "fso.h"
#include "local.h"
#include "mariner.h"
#include "simulate.h"
#include "user.h"
#include "venus.private.h"
#include "venusrecov.h"
#include "venusvol.h"
#include "vproc.h"
#include "worker.h"


static int NullRcRights = 0;
static AcRights NullAcRights = { ALL_UIDS, 0, 0, 0 };


/*  *****  Constructors, Destructors  *****  */

/* Pre-allocation routine. */
/* MUST be called from within transaction! */

void *fsobj::operator new(size_t len, fso_alloc_t fromwhere){
    fsobj *f = 0;

    assert(fromwhere == FROMHEAP);
    /* Allocate recoverable store for the object. */
    f = (fsobj *)rvmlib_rec_malloc((int)len);
    assert(f);
    return(f);
}

void *fsobj::operator new(size_t len, fso_alloc_t fromwhere, int AllocPriority){
    fsobj *f = 0;
    int rc = 0;

    assert(fromwhere == FROMFREELIST); 
    /* Find an existing object that can be reclaimed. */
    rc = FSDB->AllocFso(AllocPriority, &f);
    if (rc == ENOSPC)
      LOG(0, ("fsobj::new returns 0 (fsdb::AllocFso returned ENOSPC)\n"));
//    assert(f);
    return(f);
}

void *fsobj::operator new(size_t len){
    abort(); /* should never be called */
}

fsobj::fsobj(int i) : cf(i) {

    RVMLIB_REC_OBJECT(*this);
    ix = i;

    /* Put ourselves on the free-list. */
    FSDB->FreeFso(this);
}


/* MUST be called from within transaction! */
fsobj::fsobj(ViceFid *key, char *name) : cf() {
    LOG(10, ("fsobj::fsobj: fid = (%x.%x.%x), comp = %s\n",
	    key->Volume, key->Vnode, key->Unique, name));

    RVMLIB_REC_OBJECT(*this);
    ResetPersistent();
    fid = *key;
    {
	int len = (name ? (int) strlen(name) : 0) + 1;
	comp = (char *)rvmlib_rec_malloc(len);
	rvmlib_set_range(comp, len);
	if (name) strcpy(comp, name);
	else comp[0] = '\0';
    }
    if (fid.Vnode == ROOT_VNODE && fid.Unique == ROOT_UNIQUE)
	mvstat = ROOT;
    ResetTransient();

    /* Insert into hash table. */
    (FSDB->htab).append(&fid, &primary_handle);
}

/* local-repair modification */
/* MUST be called from within transaction! */
/* Caller sets range for whole object! */
void fsobj::ResetPersistent() {
    MagicNumber = FSO_MagicNumber;
    fid = NullFid;
    comp = 0;
    vol = 0;
    state = FsoRunt;
    stat.VnodeType = Invalid;
    stat.LinkCount = (unsigned char)-1;
    stat.Length = (unsigned long)-1;
    stat.DataVersion = -1;
    stat.VV = NullVV;
    stat.VV.StoreId.Host = NO_HOST;
    stat.Date = (Date_t)-1;
    stat.Author = ALL_UIDS;
    stat.Owner = ALL_UIDS;
    stat.Mode = (unsigned short)-1;
    ClearAcRights(ALL_UIDS);
    flags.fake = 0;
    flags.owrite = 0;
    flags.fetching = 0;
    flags.dirty = 0;
    flags.local = 0;
    flags.discread = 0;	    /* Read/Write Sharing Stat Collection */
    mvstat = NORMAL;
    pfid = NullFid;
    CleanStat.Length = (unsigned long)-1;
    CleanStat.Date = (Date_t)-1;
    data.havedata = 0;
    DisconnectionsSinceUse = 0;
    DisconnectionsUnused = 0;
}

/* local-repair modification */
/* Needn't be called from within transaction. */
void fsobj::ResetTransient() {
    /* Sanity checks. */
    if (MagicNumber != FSO_MagicNumber)
	{ print(logFile); Choke("fsobj::ResetTransient: bogus MagicNumber"); }

    /* This is a horrible way of resetting handles! */
    bzero((void *)&vol_handle, (int)sizeof(vol_handle));
    bzero((void *)&prio_handle, (int)sizeof(prio_handle));
    bzero((void *)&del_handle, (int)sizeof(del_handle));
    bzero((void *)&owrite_handle, (int)sizeof(owrite_handle));

    if (HAVEDATA(this) && stat.VnodeType == Directory && mvstat != MOUNTPOINT) {
	data.dir->udcfvalid = 0;
	data.dir->udcf = 0;
    }
    ClearRcRights();
    DemoteAcRights(ALL_UIDS);
    flags.backup = 0;
    flags.readonly = 0;
    flags.replicated = 0;
    flags.rwreplica = 0;
    flags.usecallback = 0;
    flags.replaceable = 0;
    flags.era = 1;
    flags.ckmtpt = 0;
    flags.created = 0;
    flags.marked = 0;
    flags.random = ::random();

    bzero((void *)&u, (int)sizeof(u));

    pfso = 0;
    children = 0;
    bzero((void *)&child_link, (int)sizeof(child_link));

    priority = -1;
    HoardPri = 0;
    HoardVuid = HOARD_UID;
    hdb_bindings = 0;
    FetchAllowed = HF_DontFetch;
    AskingAllowed = HA_Ask;

    mle_bindings = 0;
    shadow = 0;
    
    /* 
     * sync doesn't need to be initialized. 
     * It's used only for LWP_Wait and LWP_Signal. 
     */
    readers = 0;
    writers = 0;
    openers = 0;
    Writers = 0;
    Execers = 0;
    refcnt = 0;

    cachehit.count = 0;
    cachehit.blocks = 0;
    cachemiss.count = 0;
    cachemiss.blocks = 0;
    cachenospace.count = 0;
    cachenospace.blocks = 0;

    lastresolved = 0;

    /* Link to volume, and initialize volume specific members. */
    {
	if ((vol = VDB->Find(fid.Volume)) == 0)
	    { print(logFile); Choke("fsobj::ResetTransient: couldn't find volume"); }
	vol->hold();
	if (vol->IsBackup()) flags.backup = 1;
	if (vol->IsReadOnly()) flags.readonly = 1;
	if (vol->IsReplicated()) flags.replicated = 1;
	if (vol->IsReadWriteReplica()) flags.rwreplica = 1;
	if (vol->flags.usecallback) flags.usecallback = 1;
    }

    /* Add to volume list */
    vol->fso_list->append(&vol_handle);

    if (flags.local == 1) {
	/* set valid RC status for local object */
	SetRcRights(RC_DATA | RC_STATUS);
    }
}


/* MUST be called from within transaction! */
fsobj::~fsobj() {
    RVMLIB_REC_OBJECT(*this);

#ifdef	VENUSDEBUG
    /* Sanity check. */
    if (!GCABLE(this) || DIRTY(this))
	{ print(logFile); Choke("fsobj::~fsobj: !GCABLE || DIRTY"); }
#endif	VENUSDEBUG

    LOG(10, ("fsobj::~fsobj: fid = (%x.%x.%x), comp = %s\n",
	      fid.Volume, fid.Vnode, fid.Unique, comp));

    /* Reset reference counter for this slot. */
    FSDB->LastRef[ix] = 0;

    /* MLE bindings must already be detached! */
    if (mle_bindings) {
	if (mle_bindings->count() != 0)
	    { print(logFile); Choke("fsobj::~fsobj: mle_bindings->count() != 0"); }
	delete mle_bindings;
	mle_bindings = 0;
    }

    /* Detach hdb bindings. */
    if (hdb_bindings) {
	DetachHdbBindings();
	if (hdb_bindings->count() != 0)
	    { print(logFile); Choke("fsobj::~fsobj: hdb_bindings->count() != 0"); }
	delete hdb_bindings;
	hdb_bindings = 0;
    }

    /* Detach ourselves from our parent (if necessary). */
    if (pfso != 0) {
	pfso->DetachChild(this);
	pfso = 0;
    }

    /* Detach any children of our own. */
    if (children != 0) {
	dlink *d = 0;
	while (d = children->first()) {
	    fsobj *cf = strbase(fsobj, d, child_link);

	    /* If this is a FakeDir delete all associated FakeMtPts since they are no longer useful! */
	    if (IsFakeDir())
		cf->Kill();

	    DetachChild(cf);
	    cf->pfso = 0;
	}
	if (children->count() != 0)
	    { print(logFile); Choke("fsobj::~fsobj: children->count() != 0"); }
	delete children;
    }

    /* Do mount cleanup. */
    switch(mvstat) {
	case NORMAL:
	    /* Nothing to do. */
	    break;

	case MOUNTPOINT:
	    /* Detach volume root. */
	    {
		fsobj *root_fso = u.root;
		if (root_fso == 0) {
		    print(logFile);
		    Choke("fsobj::~fsobj: root_fso = 0");
		}
		if (root_fso->u.mtpoint != this) {
		    print(logFile);
		    root_fso->print(logFile);
		    Choke("fsobj::~fsobj: rf->mtpt != mf");
		}
		root_fso->UnmountRoot();
		UncoverMtPt();
	    }
	    break;

	case ROOT:
	    /* Detach mtpt. */
	    if (u.mtpoint != 0) {
		fsobj *mtpt_fso = u.mtpoint;
		if (mtpt_fso->u.root != this) {
		    mtpt_fso->print(logFile);
		    print(logFile);
		    Choke("fsobj::~fsobj: mf->root != rf");
		}
		UnmountRoot();
		mtpt_fso->UncoverMtPt();
	    }
	    break;

	default:
	    print(logFile);
	    Choke("fsobj::~fsobj: bogus mvstat");
    }

    /* Remove from volume's fso list */
    if (vol->fso_list->remove(&vol_handle) != &vol_handle)
	{ print(logFile); Choke("fsobj::~fsobj: fso_list remove"); }

    /* Unlink from volume. */
    VDB->Put(&vol);

    /* Release data. */
    if (HAVEDATA(this))
	DiscardData();

    /* Remove from the delete queue. */
    if (FSDB->delq->remove(&del_handle) != &del_handle)
	{ print(logFile); Choke("fsobj::~fsobj: delq remove"); }

    /* Remove from the table. */
    if ((FSDB->htab).remove(&fid, &primary_handle) != &primary_handle)
	{ print(logFile); Choke("fsobj::~fsobj: htab remove"); }

    /* Notify waiters of dead runts. */
    if (!HAVESTATUS(this)) {
	LOG(10, ("fsobj::~fsobj: dead runt = (%x.%x.%x)\n",
		 fid.Volume, fid.Vnode, fid.Unique));

	FSDB->matriculation_count++;
	VprocSignal(&FSDB->matriculation_sync);
    }

    /* Return component string to heap. */
    rvmlib_rec_free(comp);

}

void fsobj::operator delete(void *deadobj, size_t len) {

    LOG(10, ("fsobj::operator delete()\n"));

    /* Stick on the free list. */
    FSDB->FreeFso((fsobj *)deadobj);
}

/* local-repair modification */
/* MUST NOT be called from within transaction. */
void fsobj::Recover() {
    ASSERT(!Simulating);

    /* Validate state. */
    switch(state) {
	case FsoRunt:
	    /* Objects that hadn't matriculated can be safely discarded. */
	    eprint("\t(%s, %x.%x.%x) runt object being discarded...",
		   comp, fid.Volume, fid.Vnode, fid.Unique);
	    goto Failure;

	case FsoNormal:
	    break;

	case FsoDying:
	    /* Dying objects should shortly be deleted. */
	    FSDB->delq->append(&del_handle);
	    break;

	default:
	    print(logFile);
	    Choke("fsobj::Recover: bogus state");
    }

    /* Uncover mount points. */
    if (mvstat == MOUNTPOINT) {
	Recov_BeginTrans();
	RVMLIB_REC_OBJECT(stat.VnodeType);
	stat.VnodeType = SymbolicLink;
	RVMLIB_REC_OBJECT(mvstat);
	mvstat = NORMAL;
	Recov_EndTrans(MAXFP);
    }

    /* Rebuild priority queue. */
    ComputePriority();

    /* Garbage collect data that was in the process of being fetched. */
    if (flags.fetching != 0) {
	FSO_ASSERT(this, HAVEDATA(this));
	eprint("\t(%s, %x.%x.%x) freeing garbage data contents",
	       comp, fid.Volume, fid.Vnode, fid.Unique);
	Recov_BeginTrans();
	RVMLIB_REC_OBJECT(flags);
	flags.fetching = 0;
	DiscardData();
	Recov_EndTrans(0);
    }

    /* Files that were open for write must be "closed" and discarded. */
    if (flags.owrite != 0) {
	FSO_ASSERT(this, HAVEDATA(this));
	eprint("\t(%s, %x.%x.%x) discarding owrite object",
	       comp, fid.Volume, fid.Vnode, fid.Unique);
	Recov_BeginTrans();
	RVMLIB_REC_OBJECT(flags);
	flags.owrite = 0;
	Recov_EndTrans(0);
	goto Failure;
    }

    /* Get rid of fake objects, and other objects that are not likely to be useful anymore. */
    if ((IsFake() && !LRDB->RFM_IsFakeRoot(&fid)) || flags.backup || flags.rwreplica) {
	goto Failure;
    }

    /* Get rid of a former mount-root whose fid is not a volume root and whose pfid is NullFid */
    if ((mvstat == NORMAL) && (fid.Vnode != ROOT_VNODE) && FID_EQ(&pfid, &NullFid) && !IsLocalObj()) {
	LOG(0, ("fsobj::Recover: 0x%x.%x.%x is a non-volume root whose pfid is NullFid\n",
		fid.Volume, fid.Vnode, fid.Unique));
	goto Failure;
    }

    /* Check the cache file. */
    switch(stat.VnodeType) {
	case File:
	    {
	    if ((HAVEDATA(this) && cf.Length() != stat.Length) ||
		(!HAVEDATA(this) && cf.Length() != 0)) {
		eprint("\t(%s, %x.%x.%x) cache file validation failed",
		       comp, fid.Volume, fid.Vnode, fid.Unique);
		goto Failure;
	    }
	    }
	    break;

	case Directory:
	case SymbolicLink:
	    /* 
	     * Reclaim cache-file blocks. Since directory contents are stored
	     * in RVM (as are all fsobjs), cache file blocks for directories 
	     * are thrown out at startup because they are the ``Unix format'' 
	     * version of the object.  The stuff in RVM is the ``Vice format'' 
	     * version. 
	     */
	    if (cf.Length() != 0) {
		FSDB->FreeBlocks(NBLOCKS(cf.Length()));
		cf.Reset();
	    }
	    break;

	case Invalid:
	    Choke("fsobj::Recover: bogus VnodeType (%d)", stat.VnodeType);
    }

    if (LogLevel >= 1) print(logFile);
    return;

Failure:
    {
	LOG(0, ("fsobj::Recover: invalid fso (%s, %x.%x.%x), attempting to GC...",
		comp, fid.Volume, fid.Vnode, fid.Unique));
	print(logFile);

	/* Scavenge data for dirty, bogus file. */
	/* Note that any store of this file in the CML must be cancelled (in later step of recovery). */
	if (DIRTY(this)) {
	    if (!IsFile()) {
		print(logFile);
		Choke("recovery failed on local, non-file object (%s, %x.%x.%x)",
		    comp, fid.Volume, fid.Vnode, fid.Unique);
	    }

	    if (HAVEDATA(this)) {
		    Recov_BeginTrans();
		    DiscardData();
		    Recov_EndTrans(MAXFP);
	    }
	    else {
		/* Reclaim cache-file blocks. */
		if (cf.Length() != 0) {
		    FSDB->FreeBlocks(NBLOCKS(cf.Length()));
		    cf.Reset();
		}
	    }

	    return;
	}

	/* Kill bogus object. */
	/* Caution: Do NOT GC since linked objects may not be valid yet! */
	{
	    /* Reclaim cache-file blocks. */
	    if (cf.Length() != 0) {
		FSDB->FreeBlocks(NBLOCKS(cf.Length()));
		cf.Reset();
	    }

	    Recov_BeginTrans();
	    Kill();
	    Recov_EndTrans(MAXFP);
	}
    }
}


/*  *****  General Status  *****  */

/* MUST be called from within transaction! */
/* Call with object write-locked. */
void fsobj::Matriculate() {
    if (HAVESTATUS(this))
	{ print(logFile); Choke("fsobj::Matriculate: HAVESTATUS"); }

    LOG(10, ("fsobj::Matriculate: (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique));

    RVMLIB_REC_OBJECT(state);
    state = FsoNormal;

    /* Notify waiters. */
    FSDB->matriculation_count++;
    VprocSignal(&FSDB->matriculation_sync);	/* OK; we are in transaction, but signal is NO yield */
}


/* Need not be called from within transaction. */
/* Call with object write-locked. */
/* CallBack handler calls this with NoLock (to avoid deadlock)! -JJK */
void fsobj::Demote(int TellServers) {
    if (!HAVESTATUS(this) || DYING(this)) return;
    if (flags.readonly || IsMtPt() || IsFakeMTLink()) return;

    LOG(10, ("fsobj::Demote: fid = (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique));

    /* Only return RC rights if the servers don't know that we're demoting! */
    if (TellServers)
	ReturnRcRights();
    else
	ClearRcRights();

    if (IsDir())
	DemoteAcRights(ALL_UIDS);

    DemoteHdbBindings();

    /* Kernel demotion must be severe for non-directories (i.e., purge name- as well as attr-cache) */
    /* because pfid is suspect and the only way to revalidate it is via a cfs_lookup call. -JJK */
    int severely = (!IsDir() || IsFakeDir());
    k_Purge(&fid, severely);
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
void fsobj::Kill(int TellServers) {
#ifdef	VENUSDEBUG
    /* Sanity check. */
    if ((DYING(this) && *((dlink **)&del_handle) == 0) ||
	 (!DYING(this) && *((dlink **)&del_handle) != 0))
	{ print(logFile); Choke("fsobj::Delete: state != link"); }
#endif	VENUSDEBUG
    if (DYING(this)) return;

    LOG(10, ("fsobj::Kill: (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique));


    DisableReplacement();

    FSDB->delq->append(&del_handle);
    RVMLIB_REC_OBJECT(state);
    state = FsoDying;

    /* Only return RC rights if the servers don't know that we're demoting! */
    if (TellServers)
	ReturnRcRights();
    else
	ClearRcRights();

    if (IsDir())
	DemoteAcRights(ALL_UIDS);

    /* Inform advice servers of loss of availability of this object */
    NotifyUsersOfKillEvent(hdb_bindings, NBLOCKS(stat.Length));

    DetachHdbBindings();

    k_Purge(&fid, 1);

    /* Reset pfid (this is needed for simulating). */
    if (Simulating)
        pfid = NullFid;
}


/* MUST be called from within transaction! */
void fsobj::GC() {
	/* Only GC the data now if the file has been locally modified! */
    if (DIRTY(this)) {
	if (Simulating && IsDir())
	    /* Need to preserve directory contents when simulating! */
	    ;
	else
	    DiscardData();
    }
    else
	delete this;
}


/* MUST NOT be called from within transaction! */
int fsobj::Flush() {
    /* Flush all children first. */
    /* N.B. Recursion here could overflow smallish stacks! */
    if (children != 0) {
	dlist_iterator next(*children);
	dlink *d = next();
	if (d != 0) {
	    do {
		fsobj *cf = strbase(fsobj, d, child_link);
		d = next();
		(void)cf->Flush();
	    } while(d != 0);
	}
    }

    if (!FLUSHABLE(this)) {
	LOG(10, ("fsobj::Flush: (%x.%x.%x) !FLUSHABLE\n",
		 fid.Volume, fid.Vnode, fid.Unique));
	Demote();
	k_Purge(&fid, 1);	/* Just in case Demote() didn't do it! -JJK */
	return(EMFILE);
    }

    LOG(10, ("fsobj::Flush: flushed (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique));
    Recov_BeginTrans();
    Kill();
    GC();
    Recov_EndTrans(MAXFP);

    return(0);
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
/* Called as result of {GetAttr, ValidateAttr, GetData, ValidateData}. */
void fsobj::UpdateStatus(ViceStatus *vstat, vuid_t vuid) {
    /* Mount points are never updated. */
    if (IsMtPt())
	{ print(logFile); Choke("fsobj::UpdateStatus: IsMtPt!"); }
    /* Fake objects are never updated. */
    if (IsFake())
	{ print(logFile); Choke("fsobj::UpdateStatus: IsFake!"); }

    LOG(100, ("fsobj::UpdateStatus: (%x.%x.%x), uid = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, vuid));

    if (HAVESTATUS(this)) {		/* {ValidateAttr, GetData, ValidateData} */
	if (!StatusEq(vstat, 0))
	    ReplaceStatus(vstat, 0);
    }
    else {				/* {GetAttr} */
	Matriculate();
	ReplaceStatus(vstat, 0);
    }

    /* Set access rights and parent (if they differ). */
    if (IsDir()) {
	SetAcRights(ALL_UIDS, vstat->AnyAccess);
	SetAcRights(vuid, vstat->MyAccess);
    }
    SetParent(vstat->vparent, vstat->uparent);
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
/* Called for mutating operations. */
void fsobj::UpdateStatus(ViceStatus *vstat, vv_t *UpdateSet, vuid_t vuid) {
    /* Mount points are never updated. */
    if (IsMtPt())
	{ print(logFile); Choke("fsobj::UpdateStatus: IsMtPt!"); }
    /* Fake objects are never updated. */
    if (IsFake())
	{ print(logFile); Choke("fsobj::UpdateStatus: IsFake!"); }

    LOG(100, ("fsobj::UpdateStatus: (%x.%x.%x), uid = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, vuid));

    /* Install the new status block. */
    if (!StatusEq(vstat, 1))
	/* Ought to Die in this event! */;
    ReplaceStatus(vstat, UpdateSet);

    /* Set access rights and parent (if they differ). */
    /* N.B.  It should be a fatal error if they differ! */
    if (IsDir()) {
	SetAcRights(ALL_UIDS, vstat->AnyAccess);
	SetAcRights(vuid, vstat->MyAccess);
    }
    SetParent(vstat->vparent, vstat->uparent);
}


/* Need not be called from within transaction. */
int fsobj::StatusEq(ViceStatus *vstat, int Mutating) {
    int eq = 1;
    int log = (Mutating || HAVEDATA(this));

    if (stat.Length != vstat->Length) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), Length %d != %d\n",
		    fid.Volume, fid.Vnode, fid.Unique,
		    stat.Length, vstat->Length));
    }
    if (stat.DataVersion != vstat->DataVersion) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), DataVersion %d != %d\n",
		    fid.Volume, fid.Vnode, fid.Unique,
		    stat.DataVersion, vstat->DataVersion));
    }
    if (flags.replicated && !Mutating && VV_Cmp(&stat.VV, &vstat->VV) != VV_EQ) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), VVs differ\n",
		    fid.Volume, fid.Vnode, fid.Unique));
    }
    if (stat.Date != vstat->Date) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), Date %d != %d\n",
		    fid.Volume, fid.Vnode, fid.Unique,
		    stat.Date, vstat->Date));
    }
    if (stat.Owner != vstat->Owner) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), Owner %d != %d\n",
		    fid.Volume, fid.Vnode, fid.Unique,
		    stat.Owner, vstat->Owner));
    }
    if (stat.Mode != vstat->Mode) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), Mode %d != %d\n",
		    fid.Volume, fid.Vnode, fid.Unique,
		    stat.Mode, vstat->Mode));
    }
    if (stat.LinkCount != vstat->LinkCount) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), LinkCount %d != %d\n",
		    fid.Volume, fid.Vnode, fid.Unique,
		    stat.LinkCount, vstat->LinkCount));
    }
    if (stat.VnodeType != (int)vstat->VnodeType) {
	eq = 0;
	if (log)
	    LOG(0, ("fsobj::StatusEq: (%x.%x.%x), VnodeType %d != %d\n",
		    fid.Volume, fid.Vnode, fid.Unique,
		    stat.VnodeType, (int)vstat->VnodeType));
    }

    return(eq);
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
void fsobj::ReplaceStatus(ViceStatus *vstat, vv_t *UpdateSet) {
    FSO_ASSERT(this, !Simulating);

    RVMLIB_REC_OBJECT(stat);
    stat.Length = vstat->Length;
    stat.DataVersion = vstat->DataVersion;
    if (flags.replicated || flags.rwreplica) {
	if (UpdateSet == 0)
	    stat.VV = vstat->VV;
	else {
	    stat.VV.StoreId = vstat->VV.StoreId;
	    AddVVs(&stat.VV, UpdateSet);
	}
    }
    stat.Date = vstat->Date;
    stat.Owner = (vuid_t) vstat->Owner;
    stat.Mode = (short) vstat->Mode;
    stat.LinkCount = (unsigned char) vstat->LinkCount;
    stat.VnodeType = vstat->VnodeType;
}


int fsobj::CheckRcRights(int rights) {
    return((rights & RcRights) == rights);
}


void fsobj::SetRcRights(int rights) {
    if (flags.readonly || flags.backup  || IsFake())
	return;

    LOG(100, ("fsobj::SetRcRights: (%x.%x.%x), rights = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, rights));

    FSO_ASSERT(this, flags.discread == 0 || flags.local == 1);
    RcRights = rights;
}


void fsobj::ReturnRcRights() {
    if (RcRights != NullRcRights)
	/*vol->ReturnRcRights(&fid)*/;

    ClearRcRights();
}


void fsobj::ClearRcRights() {
    LOG(100, ("fsobj::ClearRcRights: (%x.%x.%x), rights = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, RcRights));

    RcRights = NullRcRights;
}


int fsobj::IsValid(int rcrights) {
    int haveit = 0;

    if (rcrights & RC_STATUS) 
        haveit = (state != FsoRunt);
    if (rcrights & RC_DATA)  /* should work if both set */
        haveit = (data.havedata?data.havedata:haveit);

    /* hook for VCB statistics -- valid due to VCB only? */
    if ((haveit &&
	 !Simulating &&
	 ((vol->state == Hoarding) || 
	  (vol->IsWriteDisconnected() && !flags.dirty)) &&
	 !flags.readonly &&
	 !IsMtPt() &&
	 !IsFakeMTLink() &&
	 !CheckRcRights(rcrights) &&
	 vol->HaveCallBack()))
	vol->VCBHits++;

    return(haveit && 
	   (Simulating || 
	    (vol->IsDisconnected() && flags.replicated) ||
	    (vol->IsWriteDisconnected() && flags.dirty && flags.replicated) ||
	    flags.readonly ||
	    CheckRcRights(rcrights) ||
	    vol->HaveCallBack() ||
	    IsMtPt() ||
	    IsFakeMTLink()));
}


/* Returns {0, EACCES, ENOENT}. */
int fsobj::CheckAcRights(vuid_t vuid, long rights, int validp) {
    if (vuid == ALL_UIDS) {
	/* Do we have access via System:AnyUser? */
	if ((AnyUser.inuse) &&
	    (!validp || AnyUser.valid))
	    return((rights & AnyUser.rights) ? 0 : EACCES);
    }
    else {
	/* Do we have this user's rights in the cache? */
	for (int i = 0; i < CPSIZE; i++) {
	    if ((!SpecificUser[i].inuse) ||
		(validp && !SpecificUser[i].valid))
		continue;

	    if (vuid == SpecificUser[i].uid)
		return((rights & SpecificUser[i].rights) ? 0 : EACCES);
	}
    }

    LOG(10, ("fsobj::CheckAcRights: not found, (%x.%x.%x), (%d, %d, %d)\n",
	      fid.Volume, fid.Vnode, fid.Unique, vuid, rights, validp));
    return(ENOENT);
}


/* MUST be called from within transaction! */
void fsobj::SetAcRights(vuid_t vuid, long rights) {
    LOG(100, ("fsobj::SetAcRights: (%x.%x.%x), vuid = %d, rights = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, vuid, rights));

    if (vuid == ALL_UIDS) {
	if (AnyUser.rights != rights || !AnyUser.inuse) {
	    RVMLIB_REC_OBJECT(AnyUser);
	    AnyUser.rights = (unsigned char) rights;
	    AnyUser.inuse = 1;
	}
	AnyUser.valid = 1;
    }
    else {
	/* Don't record rights if we're really System:AnyUser! */
	userent *ue;
	GetUser(&ue, vuid);
	int tokensvalid = ue->TokensValid();
	PutUser(&ue);
	if (!tokensvalid) return;

	int i;
	int j = -1;
	int k = -1;
	for (i = 0; i < CPSIZE; i++) {
	    if (vuid == SpecificUser[i].uid) break;
	    if (!SpecificUser[i].inuse) j = i;
	    if (!SpecificUser[i].valid) k = i;
	}
	if (i == CPSIZE && j != -1) i = j;
	if (i == CPSIZE && k != -1) i = k;
	if (i == CPSIZE) i = (int) (Vtime() % CPSIZE);

	if (SpecificUser[i].uid != vuid ||
	    SpecificUser[i].rights != rights ||
	    !SpecificUser[i].inuse) {
	    RVMLIB_REC_OBJECT(SpecificUser[i]);
	    SpecificUser[i].uid = vuid;
	    SpecificUser[i].rights = (unsigned char) rights;
	    SpecificUser[i].inuse = 1;
	}
	SpecificUser[i].valid = 1;
    }
}


/* Need not be called from within transaction. */
void fsobj::DemoteAcRights(vuid_t vuid) {
    LOG(100, ("fsobj::DemoteAcRights: (%x.%x.%x), vuid = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, vuid));

    if (vuid == ALL_UIDS && AnyUser.valid)
	AnyUser.valid = 0;

    for (int i = 0; i < CPSIZE; i++)
	if ((vuid == ALL_UIDS || SpecificUser[i].uid == vuid) && SpecificUser[i].valid)
	    SpecificUser[i].valid = 0;
}


/* Need not be called from within transaction. */
void fsobj::PromoteAcRights(vuid_t vuid) {
    LOG(100, ("fsobj::PromoteAcRights: (%x.%x.%x), vuid = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, vuid));

    if (vuid == ALL_UIDS) {
	AnyUser.valid = 1;

	/* 
	 * if other users who have rights in the cache also have
	 * tokens, promote their rights too. 
	 */
	for (int i = 0; i < CPSIZE; i++)
	    if (SpecificUser[i].inuse && !SpecificUser[i].valid) {
		userent *ue;
		GetUser(&ue, SpecificUser[i].uid);
		int tokensvalid = ue->TokensValid();
		PutUser(&ue);
		if (tokensvalid) SpecificUser[i].valid = 1;
	    }
    } else {
	/* 
	 * Make sure tokens didn't expire for this user while
	 * the RPC was in progress. If we set them anyway, and
	 * he goes disconnected, he may have access to files he 
	 * otherwise wouldn't have because he lost tokens.
	 */
	userent *ue;
	GetUser(&ue, vuid);
	int tokensvalid = ue->TokensValid();
	PutUser(&ue);
	if (!tokensvalid) return;

	for (int i = 0; i < CPSIZE; i++)
	    if (SpecificUser[i].uid == vuid)
		SpecificUser[i].valid = 1;
    }
}


/* MUST be called from within transaction! */
void fsobj::ClearAcRights(vuid_t vuid) {
    LOG(100, ("fsobj::ClearAcRights: (%x.%x.%x), vuid = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, vuid));

    if (vuid == ALL_UIDS) {
	RVMLIB_REC_OBJECT(AnyUser);
	AnyUser = NullAcRights;
    }

    for (int i = 0; i < CPSIZE; i++)
	if (vuid == ALL_UIDS || SpecificUser[i].uid == vuid) {
	    RVMLIB_REC_OBJECT(SpecificUser[i]);
	    SpecificUser[i] = NullAcRights;
	}
}


/* local-repair modification */
/* MUST be called from within transaction (at least if <vnode, unique> != pfid.<Vnode, Unique>)! */
void fsobj::SetParent(VnodeId vnode, Unique_t unique) {
    if (IsRoot() || (vnode == 0 && unique == 0) || LRDB->RFM_IsGlobalRoot(&fid))
	return;

    /* Update pfid if necessary. */
    if (pfid.Vnode != vnode || pfid.Unique != unique) {
	/* Detach from old parent if necessary. */
	if (pfso != 0) {
	    pfso->DetachChild(this);
	    pfso = 0;
	}

	/* Install new parent fid. */
	RVMLIB_REC_OBJECT(pfid);
	pfid.Volume = fid.Volume;
	pfid.Vnode = vnode;
	pfid.Unique = unique;
    }

    /* Attach to new parent if possible. */
    if (pfso == 0) {
	fsobj *pf = FSDB->Find(&pfid);
	if (pf != 0 && HAVESTATUS(pf) && !GCABLE(pf)) {
	    pfso = pf;
	    pfso->AttachChild(this);
	}
    }
}


/* MUST be called from within transaction! */
void fsobj::MakeDirty() {
    if (DIRTY(this)) return;

    LOG(1, ("fsobj::MakeDirty: (%x.%x.%x)\n",
	     fid.Volume, fid.Vnode, fid.Unique));

    RVMLIB_REC_OBJECT(flags);
    flags.dirty = 1;
    RVMLIB_REC_OBJECT(CleanStat);
    CleanStat.Length = stat.Length;
    CleanStat.Date = stat.Date;

    DisableReplacement();
}


/* MUST be called from within transaction! */
void fsobj::MakeClean() {
    if (!DIRTY(this)) return;

    LOG(1, ("fsobj::MakeClean: (%x.%x.%x)\n",
	     fid.Volume, fid.Vnode, fid.Unique));

    RVMLIB_REC_OBJECT(flags);
    flags.dirty = 0;
    flags.created = 0;

    EnableReplacement();
}


/*  *****  Mount State  *****  */
/* local-repair modification */
/* MUST NOT be called from within transaction! */
/* Call with object write-locked. */
int fsobj::TryToCover(ViceFid *inc_fid, vuid_t vuid) {
    if (!HAVEDATA(this))
	{ print(logFile); Choke("fsobj::TryToCover: called without data"); }

    LOG(10, ("fsobj::TryToCover: fid = (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique));

    int code = 0;

    /* Don't cover mount points in backup volumes! */
    if (flags.backup)
	return(ENOENT);

    /* Check for bogosities. */
    int len = (int) stat.Length;
    if (len < 2) {
	eprint("TryToCover: bogus link length");
	return(EINVAL);
    }
    char type = data.symlink[0];
    switch(type) {
	case '%':
	    eprint("TryToCover: '%'-style mount points no longer supported");
	    return(EOPNOTSUPP);

	case '#':
	case '@':
	    break;

	default:
	    eprint("TryToCover: bogus mount point type (%c)", type);
	    return(EINVAL);
    }

    /* Look up the volume that is to be mounted on us. */
    volent *tvol = 0;
    if (IsFake()) {
	VolumeId tvid;
	if (sscanf(data.symlink, "@%x.%*x.%*x", &tvid) != 1)
	    { print(logFile); Choke("fsobj::TryToCover: couldn't get tvid"); }
	code = VDB->Get(&tvol, tvid);
    }
    else {
	/* Turn volume name into a proper string. */
	data.symlink[len - 1] = 0;				/* punt transaction! */
	code = VDB->Get(&tvol, &data.symlink[1]);
    }
    if (code != 0) {
/*
	 eprint("TryToCover(%s) failed (%d)", data.symlink, code);
*/
	LOG(100, ("fsobj::TryToCover: vdb::Get(%s) failed (%d)\n", data.symlink, code));
	return(code);
    }

    /* Don't allow a volume to be mounted inside itself! */
    /* but only when its mount root is the global-root-obj of a local subtree */
    if ((fid.Volume == tvol->vid) && !LRDB->RFM_IsGlobalChild(&fid)) {
	eprint("TryToCover(%s): recursive mount!", data.symlink);
	VDB->Put(&tvol);
	return(ELOOP);
    }

    /* Don't allow backup vols in backup vols (e.g., avoid OldFiles/OldFiles problem). */
    if (tvol->IsBackup()) {
	if (vol->IsBackup()) {
	    eprint("Mount of BU volume (%s) detected inside BU volume (%s)",
		   tvol->name, vol->name);
	    VDB->Put(&tvol);
	    return(ELOOP);
	}
    }

    /* Get volume root. */
    fsobj *rf = 0;
    ViceFid root_fid;
    root_fid.Volume = tvol->vid;
    if (IsFake()) {
	if (sscanf(data.symlink, "@%*x.%x.%x", &root_fid.Vnode, &root_fid.Unique) != 2)
	    { print(logFile); Choke("fsobj::TryToCover: couldn't get <tvolid, tunique>"); }
    }
    else {
	root_fid.Vnode = ROOT_VNODE;
	root_fid.Unique = ROOT_UNIQUE;
    }
    code = FSDB->Get(&rf, &root_fid, vuid, RC_STATUS, comp);
    if (code != 0) {
	LOG(100, ("fsobj::TryToCover: Get root (%x.%x.%x) failed (%d)\n",
		  root_fid.Volume, root_fid.Vnode, root_fid.Unique, code));

	VDB->Put(&tvol);
	if (code == EINCONS && inc_fid != 0) *inc_fid = root_fid;
	return(code);
    }
    rf->PromoteLock();

    /* If root is currently mounted, uncover the mount point and unmount. */
    Recov_BeginTrans();
    fsobj *mf = rf->u.mtpoint;
    if (mf != 0) {
	    if (mf == this) {
		    eprint("TryToCover: re-mounting (%s) on (%s)", tvol->name, comp);
		    UncoverMtPt();
	    } else {
		    if (mf->u.root != rf)
			    { mf->print(logFile); rf->print(logFile); Choke("TryToCover: mf->root != rf"); }
		    mf->UncoverMtPt();
	    }
	    
	    rf->UnmountRoot();
    }
    Recov_EndTrans(MAXFP);

    /* Do the mount magic. */
    Recov_BeginTrans();
    if (IsFake() && rf->mvstat != ROOT) {
	    RVMLIB_REC_OBJECT(rf->mvstat);
	    rf->mvstat = ROOT;
	    rf->u.mtpoint = 0;
    }
    rf->MountRoot(this);
    CoverMtPt(rf);
    Recov_EndTrans(MAXFP);

    FSDB->Put(&rf);
    VDB->Put(&tvol);
    return(0);
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
void fsobj::CoverMtPt(fsobj *root_fso) {
    if (mvstat != NORMAL)
	{ print(logFile); Choke("fsobj::CoverMtPt: mvstat != NORMAL"); }
    if (!data.symlink)
	{ print(logFile); Choke("fsobj::CoverMtPt: no data.symlink!"); }

    LOG(10, ("fsobj::CoverMtPt: fid = (%x.%x.%x), rootfid = (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique,
	      root_fso->fid.Volume, root_fso->fid.Vnode, root_fso->fid.Unique));

    RVMLIB_REC_OBJECT(*this);

    /* Exit old state (NORMAL). */
    k_Purge(&fid, 1);

    /* Enter new state (MOUNTPOINT). */
    stat.VnodeType = Directory;
    mvstat = MOUNTPOINT;
    u.root = root_fso;
    DisableReplacement();
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
void fsobj::UncoverMtPt() {
    if (mvstat != MOUNTPOINT) 
	{ print(logFile); Choke("fsobj::UncoverMtPt: mvstat != MOUNTPOINT"); }
    if (!u.root)
	{ print(logFile); Choke("fsobj::UncoverMtPt: no u.root!"); }

    LOG(10, ("fsobj::UncoverMtPt: fid = (%x.%x.%x), rootfid = (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique,
	      u.root->fid.Volume, u.root->fid.Vnode, u.root->fid.Unique));

    RVMLIB_REC_OBJECT(*this);

    /* Exit old state (MOUNTPOINT). */
    u.root = 0;
    k_Purge(&fid, 1);			/* I don't think this is necessary. */
    k_Purge(&pfid, 1);			/* This IS necessary. */

    /* Enter new state (NORMAL). */
    stat.VnodeType = SymbolicLink;
    mvstat = NORMAL;
    EnableReplacement();
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
void fsobj::MountRoot(fsobj *mtpt_fso) {
    if (mvstat != ROOT)
	{ print(logFile); Choke("fsobj::MountRoot: mvstat != ROOT"); }
    if (u.mtpoint)
	{ print(logFile); Choke("fsobj::MountRoot: u.mtpoint exists!"); }

    LOG(10, ("fsobj::MountRoot: fid = (%x.%x.%x), mtptfid = (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique,
	      mtpt_fso->fid.Volume, mtpt_fso->fid.Vnode, mtpt_fso->fid.Unique));

    RVMLIB_REC_OBJECT(*this);

    /* Exit old state (ROOT, without link). */
    k_Purge(&fid);

    /* Enter new state (ROOT, with link). */
    u.mtpoint = mtpt_fso;
}


/* MUST be called from within transaction! */
/* Call with object write-locked. */
void fsobj::UnmountRoot() {
    if (mvstat != ROOT) 
	{ print(logFile); Choke("fsobj::UnmountRoot: mvstat != ROOT"); }
    if (!u.mtpoint)
	{ print(logFile); Choke("fsobj::UnmountRoot: no u.mtpoint!"); }

    LOG(10, ("fsobj::UnmountRoot: fid = (%x.%x.%x), mtptfid = (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique,
	      u.mtpoint->fid.Volume, u.mtpoint->fid.Vnode, u.mtpoint->fid.Unique));

    RVMLIB_REC_OBJECT(*this);

    /* Exit old state (ROOT, with link). */
    u.mtpoint = 0;
    k_Purge(&fid);

    /* Enter new state (ROOT, without link). */
    if (fid.Vnode != ROOT_VNODE) {
	mvstat = NORMAL;	    /* couldn't be mount point, could it? */       
	/* this object could be the global root of a local/global subtree */
	if (FID_EQ(&pfid, &NullFid) && !IsLocalObj()) {
	    LOG(0, ("fsobj::UnmountRoot: 0x%x.%x.%x a previous mtroot without pfid, kill it\n",
		    fid.Volume, fid.Vnode, fid.Unique));
	    Kill();
	}
    }
}


/*  *****  Child/Parent Linkage  *****  */

/* Need not be called from within transaction. */
void fsobj::AttachChild(fsobj *child) {
    if (!IsDir())
	{ print(logFile); child->print(logFile); Choke("fsobj::AttachChild: not dir"); }

    LOG(100, ("fsobj::AttachChild: (%x.%x.%x), (%x.%x.%x)\n",
	       fid.Volume, fid.Vnode, fid.Unique,
	       child->fid.Volume, child->fid.Vnode, child->fid.Unique));

    DisableReplacement();

    if (*((dlink **)&child->child_link) != 0)
	{ print(logFile); child->print(logFile); Choke("fsobj::AttachChild: bad child"); }
    if (children == 0)
	children = new dlist;
    children->prepend(&child->child_link);

    DemoteHdbBindings();	    /* in case an expansion would now be satisfied! */
}


/* Need not be called from within transaction. */
void fsobj::DetachChild(fsobj *child) {
    if (!IsDir())
	{ print(logFile); child->print(logFile); Choke("fsobj::DetachChild: not dir"); }

    LOG(100, ("fsobj::DetachChild: (%x.%x.%x), (%x.%x.%x)\n",
	       fid.Volume, fid.Vnode, fid.Unique,
	       child->fid.Volume, child->fid.Vnode, child->fid.Unique));

    DemoteHdbBindings();	    /* in case an expansion would no longer be satisfied! */

    if (child->pfso != this || *((dlink **)&child->child_link) == 0 ||
	 children == 0 || children->count() == 0)
	{ print(logFile); child->print(logFile); Choke("fsobj::DetachChild: bad child"); }
    if (children->remove(&child->child_link) != &child->child_link)
	{ print(logFile); child->print(logFile); Choke("fsobj::DetachChild: remove failed"); }

    EnableReplacement();
}


/*  *****  Priority State  *****  */

/* Need not be called from within transaction. */
void fsobj::Reference() {
    LOG(100, ("fsobj::Reference: (%x.%x.%x), old = %d, new = %d\n",
	       fid.Volume, fid.Vnode, fid.Unique, FSDB->LastRef[ix], FSDB->RefCounter));

    FSDB->LastRef[ix] = FSDB->RefCounter++;
}

/* local-repair modification */
/* Need not be called from within transaction. */
void fsobj::ComputePriority() {
    LOG(1000, ("fsobj::ComputePriority: (%x.%x.%x)\n",
		fid.Volume, fid.Vnode, fid.Unique));

    if (IsLocalObj()) {
	LOG(1000, ("fsobj::ComputePriority: local object\n"));
	return;
    }
    FSDB->Recomputes++;

    int new_priority = 0;
    {
	/* Short-term priority (spri) is a function of how recently object was used. */
	/* Define "spread" to be the difference between the most recent */
	/* reference to any object and the most recent reference to this object. */
	/* Let "rank" be a function which scales "spread" over 0 to FSO_MAX_SPRI - 1. */
	/* Then, spri(f) :: FSO_MAX_SPRI - rank(spread(f)) */
	int spri = 0;
	int LastRef = (int) FSDB->LastRef[ix];
	if (LastRef > 0) {
	    int spread = (int) FSDB->RefCounter - LastRef - 1;
	    int rank;
	    {
		/* "rank" depends upon FSO_MAX_SPRI, fsdb::MaxFiles, and a scaling factor. */
		static int initialized = 0;
		static int RightShift;
		if (!initialized) {
#define	log2(x)\
    (ffs(binaryfloor((x) + (x) - 1) - 1))
		    int LOG_MAXFILES = log2(FSDB->MaxFiles);
		    int	LOG_SSF = log2(FSDB->ssf);
		    int LOG_MAX_SPRI = log2(FSO_MAX_SPRI);
		    RightShift = (LOG_MAXFILES + LOG_SSF - LOG_MAX_SPRI);
		    initialized = 1;
		}
		if (RightShift > 0) rank = spread >> RightShift;
		else if (RightShift < 0) rank = spread << (-RightShift);
		if (rank >= FSO_MAX_SPRI) rank = FSO_MAX_SPRI - 1;
	    }
	    spri = FSO_MAX_SPRI - rank;
	}

	/* Medium-term priority (mpri) is just the current Hoard priority. */
	int mpri = HoardPri;

	new_priority = FSDB->MakePri(spri, mpri);
    }

    if (priority == -1 || new_priority != priority) {
	FSDB->Reorders++;		    /* transient value; punt set_range */

	DisableReplacement();		    /* remove... */
	priority = new_priority;	    /* update key... */
	EnableReplacement();		    /* reinsert... */
    }
}


/* local-repair modification */
/* Need not be called from within transaction. */
void fsobj::EnableReplacement() {
#ifdef	VENUSDEBUG
    /* Sanity checks. */
/*
    if (DYING(this)) {
	if (*((dlink **)&del_handle) == 0)
	    { print(logFile); Choke("fsobj::EnableReplacement: dying && del_handle = 0"); }
	return;
    }
*/
    if ((!REPLACEABLE(this) && prio_handle.tree() != 0) ||
	 (REPLACEABLE(this) && prio_handle.tree() != FSDB->prioq))
	{ print(logFile); Choke("fsobj::EnableReplacement: state != link"); }
#endif	VENUSDEBUG

    /* Already replaceable? */
    if (REPLACEABLE(this))
	return;

    /* Are ALL conditions for replaceability met? */
    if (DYING(this) || !HAVESTATUS(this) || DIRTY(this) ||
	 READING(this) || WRITING(this) || (children && children->count() > 0) ||
	 IsMtPt() || (IsSymLink() && hdb_bindings && hdb_bindings->count() > 0))
	return;

    /* Sanity check. */
    if (priority == -1 && !IsLocalObj())
	eprint("EnableReplacement(%x.%x.%x): priority unset",
	       fid.Volume, fid.Vnode, fid.Unique);

    LOG(1000, ("fsobj::EnableReplacement: (%x.%x.%x), priority = [%d (%d) %d %d]\n",
		fid.Volume, fid.Vnode, fid.Unique,
		priority, flags.random, HoardPri, FSDB->LastRef[ix]));

#ifdef	VENUSDEBUG
    if (LogLevel >= 10000)
	FSDB->prioq->print(logFile);
#endif	VENUSDEBUG

    FSDB->prioq->insert(&prio_handle);
    flags.replaceable = 1;

#ifdef	VENUSDEBUG
    if (LogLevel >= 10000 && !(FSDB->prioq->IsOrdered()))
	{ print(logFile); FSDB->prioq->print(logFile); Choke("fsobj::EnableReplacement: !IsOrdered after insert"); }
#endif	VENUSDEBUG
}


/* Need not be called from within transaction. */
void fsobj::DisableReplacement() {
#ifdef	VENUSDEBUG
    /* Sanity checks. */
/*
    if (DYING(this)) {
	if (*((dlink **)&del_handle) == 0)
	    { print(logFile); Choke("fsobj::DisableReplacement: dying && del_handle = 0"); }
	return;
    }
*/
    if ((!REPLACEABLE(this) && prio_handle.tree() != 0) ||
	 (REPLACEABLE(this) && prio_handle.tree() != FSDB->prioq))
	{ print(logFile); Choke("fsobj::DisableReplacement: state != link"); }
#endif	VENUSDEBUG

    /* Already not replaceable? */
    if (!REPLACEABLE(this))
	return;

    LOG(1000, ("fsobj::DisableReplacement: (%x.%x.%x), priority = [%d (%d) %d %d]\n",
		fid.Volume, fid.Vnode, fid.Unique,
		priority, flags.random, HoardPri, FSDB->LastRef[ix]));

#ifdef	VENUSDEBUG
    if (LogLevel >= 10000)
	FSDB->prioq->print(logFile);
#endif	VENUSDEBUG

    flags.replaceable = 0;
    if (FSDB->prioq->remove(&prio_handle) != &prio_handle)
	{ print(logFile); Choke("fsobj::DisableReplacement: prioq remove"); }

#ifdef	VENUSDEBUG
    if (LogLevel >= 10000 && !(FSDB->prioq->IsOrdered()))
	{ print(logFile); FSDB->prioq->print(logFile); Choke("fsobj::DisableReplacement: !IsOrdered after remove"); }
#endif	VENUSDEBUG
}


int CheckForDuplicates(dlist *hdb_bindings_list, void *binder) {
    /* If the list is empty, this can't be a duplicate */
    if (hdb_bindings_list == NULL)
        return(0);

    /* Look for this binder */
    dlist_iterator next(*hdb_bindings_list);
    dlink *d;
    while (d = next()) {
	binding *b = strbase(binding, d, bindee_handle);
	if (b->binder == binder) {
	  /* Found it! */
	  return(1);
	}
    }

    /* If we had found it, we wouldn't have gotten here! */
    return(0);
}

/* Need not be called from within transaction. */
void fsobj::AttachHdbBinding(binding *b) {
    /* Sanity checks. */
    if (b->bindee != 0) {
	print(logFile);
	b->print(logFile);
	Choke("fsobj::AttachHdbBinding: bindee != 0");
    }

    /* Check for duplicates */
    if (CheckForDuplicates(hdb_bindings, b->binder) == 1) {
      b->IncrRefCount();
      LOG(100, ("This is a duplicate binding...skip it.\n"));
      //      return;
    }

    if (LogLevel >= 1000) {
	dprint("fsobj::AttachHdbBinding:\n");
	print(logFile);
    }

    /* Attach ourselves to the binding. */
    if (hdb_bindings == 0)
	hdb_bindings = new dlist;
    hdb_bindings->insert(&b->bindee_handle);
    b->bindee = this;

    if (LogLevel >= 10) {
      dprint("fsobj::AttachHdbBinding:\n");
      print(logFile);
      b->print(logFile);
    }

    if (IsSymLink())
	DisableReplacement();

    /* Recompute our priority if necessary. */
    namectxt *nc = (namectxt *)b->binder;
    if (nc->priority > HoardPri) {
	HoardPri = nc->priority;
	HoardVuid = nc->vuid;
	ComputePriority();
    }
}


/* Need not be called from within transaction. */
void fsobj::DemoteHdbBindings() {
    if (hdb_bindings == 0) return;

    dlist_iterator next(*hdb_bindings);
    dlink *d;
    while (d = next()) {
	binding *b = strbase(binding, d, bindee_handle);
	DemoteHdbBinding(b);
    }
}


/* Need not be called from within transaction. */
void fsobj::DemoteHdbBinding(binding *b) {
    /* Sanity checks. */
    if (b->bindee != this) {
	print(logFile);
	if (b != 0) b->print(logFile);
	Choke("fsobj::DemoteHdbBinding: bindee != this");
    }
    if (LogLevel >= 1000) {
	dprint("fsobj::DemoteHdbBinding:\n");
	print(logFile);
	b->print(logFile);
    }

    /* Update the state of the binder. */
    namectxt *nc = (namectxt *)b->binder;
    nc->Demote();
}


/* Need not be called from within transaction. */
void fsobj::DetachHdbBindings() {
    if (hdb_bindings == 0) return;

    dlink *d;
    while (d = hdb_bindings->first()) {
	binding *b = strbase(binding, d, bindee_handle);
	DetachHdbBinding(b, 1);
    }
}


/* Need not be called from within transaction. */
void fsobj::DetachHdbBinding(binding *b, int DemoteNameCtxt) {
  struct timeval StartTV;
  struct timeval EndTV;
  float elapsed;

    /* Sanity checks. */
    if (b->bindee != this) {
	print(logFile);
	if (b != 0) b->print(logFile);
	Choke("fsobj::DetachHdbBinding: bindee != this");
    }
    if (LogLevel >= 1000) {
	dprint("fsobj::DetachHdbBinding:\n");
	print(logFile);
	b->print(logFile);
    }

    /* Detach ourselves from the binding. */
    if (hdb_bindings->remove(&b->bindee_handle) != &b->bindee_handle)
	{ print(logFile); b->print(logFile); Choke("fsobj::DetachHdbBinding: bindee remove"); }
    b->bindee = 0;
    if (IsSymLink() && hdb_bindings->count() == 0)
	EnableReplacement();

    /* Update the state of the binder. */
    namectxt *nc = (namectxt *)b->binder;
    if (DemoteNameCtxt)
	nc->Demote();

    /* Recompute our priority if necessary. */
    if (nc->priority == HoardPri) {
	int new_HoardPri = 0;
	vuid_t new_HoardVuid = HOARD_UID;
    gettimeofday(&StartTV, 0);
    LOG(0, ("Detach: hdb_binding list contains %d namectxts\n", hdb_bindings->count()));
	dlist_iterator next(*hdb_bindings);
	dlink *d;
	while (d = next()) {
	    binding *b = strbase(binding, d, bindee_handle);
	    namectxt *nc = (namectxt *)b->binder;
	    if (nc->priority > new_HoardPri) {
		new_HoardPri = nc->priority;
		new_HoardVuid = nc->vuid;
	    }
	}
    gettimeofday(&EndTV, 0);
    elapsed = SubTimes(EndTV, StartTV);
    LOG(0, ("fsobj::DetachHdbBinding: recompute, elapsed= %3.1f\n", elapsed));

	if (new_HoardPri < HoardPri) {
	    HoardPri = new_HoardPri;
	    HoardVuid = new_HoardVuid;
	    ComputePriority();
	}
    }
}

/*
 * This routine attempts to automatically decide whether or not the hoard
 * daemon should fetch an object.  There are three valid return values:
 *	-1: the object should definitely NOT be fetched
 *	 0: the routine cannot automatically determine the fate of this 
 *	    object; the user should be given the option
 *	 1: the object should definitely be fetched
 *
 * As a first guess to the "real" function, we will use 
 * 		ALPHA + (BETA * e^(priority/GAMMA)).
 * An object's priority ranges as high as 100,000 (use formula in Jay's thesis 
 * but instead use 75 for the value of alpha and 25 for the value of 1-alpha).
 * This is (presumably) to keep all of the priorities integers.
 * 
 */
int fsobj::PredetermineFetchState(int estimatedCost, int hoard_priority) {
    double acceptableCost;
    double x;

    if (estimatedCost == -1)
        return(0);

    /* Scale it up correctly... from range 1-1000 to 1-100000 */
    hoard_priority = hoard_priority * 100;

    LOG(100, ("fsobj::PredetermineFetchState(%d)\n",estimatedCost));
    LOG(100, ("PATIENCE_ALPHA = %d, PATIENCE_BETA = %d; PATIENCE_GAMMA = %d\n", 
	      PATIENCE_ALPHA, PATIENCE_BETA, PATIENCE_GAMMA));
    LOG(100, ("priority = %d; HoardPri = %d, hoard_priority = %d\n",
	      priority, HoardPri, hoard_priority));

    x = (double)hoard_priority / (double)PATIENCE_GAMMA;
    acceptableCost = (double)PATIENCE_ALPHA + ((double)PATIENCE_BETA * exp(x));

    if ((hoard_priority == 100000) || (estimatedCost < acceptableCost)) {
        LOG(100, ("fsobj::PredetermineFetchState returns 1 (definitely fetch) \n"));
        return(1);
    }
    else {
	LOG(100, ("fsobj::PredetermineFetchState returns 0 (ask the user) \n"));
        return(0);
    }
}

CacheMissAdvice 
fsobj::ReadDisconnectedCacheMiss(vproc *vp, vuid_t vuid) {
    userent *u;
    char pathname[MAXPATHLEN];
    CacheMissAdvice advice;

    LOG(100, ("E fsobj::ReadDisconnectedCacheMiss\n"));

    GetUser(&u, vuid);
    assert(u != NULL);

    /* If advice not enabled, simply return */
    if (!AdviceEnabled) {
        LOG(100, ("ADMON STATS:  RDCM Advice NOT enabled.\n"));
        u->AdviceNotEnabled();
        return(FetchFromServers);
    }

    /* Check that:                                                     *
     *     (a) the request did NOT originate from the Hoard Daemon     *
     *     (b) the request did NOT originate from that AdviceMonitor,  *
     * and (c) the user is running an AdviceMonitor,                   */
    assert(vp != NULL);
    if (vp->type == VPT_HDBDaemon) {
	LOG(100, ("ADMON STATS:  RDCM Advice inappropriate.\n"));
        return(FetchFromServers);
    }
    if (u->IsAdvicePGID(vp->u.u_pgid)) {
        LOG(100, ("ADMON STATS:  RDCM Advice inappropriate.\n"));
        return(FetchFromServers);
    }
    if (u->IsAdviceValid(ReadDisconnectedCacheMissEventID,1) != TRUE) {
        LOG(100, ("ADMON STATS:  RDCM Advice NOT valid. (uid = %d)\n", vuid));
        return(FetchFromServers);
    }

    GetPath(pathname, 1);

    LOG(100, ("Requesting ReadDisconnected CacheMiss Advice for path=%s, pid=%d...\n", pathname, vp->u.u_pid));
    advice = u->RequestReadDisconnectedCacheMissAdvice(&fid, pathname, vp->u.u_pgid);
    return(advice);
}

CacheMissAdvice fsobj::WeaklyConnectedCacheMiss(vproc *vp, vuid_t vuid) {
    userent *u;
    char pathname[MAXPATHLEN];
    CacheMissAdvice advice;
    long CurrentBandwidth;

    LOG(100, ("E fsobj::WeaklyConnectedCacheMiss\n"));

    GetUser(&u, vuid);
    assert(u != NULL);

    /* If advice not enabled, simply return */
    if (!AdviceEnabled) {
        LOG(100, ("ADMON STATS:  WCCM Advice NOT enabled.\n"));
        u->AdviceNotEnabled();
        return(FetchFromServers);
    }

    /* Check that:                                                     *
     *     (a) the request did NOT originate from the Hoard Daemon     *
     *     (b) the request did NOT originate from that AdviceMonitor,  *
     * and (c) the user is running an AdviceMonitor,                   */
    assert(vp != NULL);
    if (vp->type == VPT_HDBDaemon) {
	LOG(100, ("ADMON STATS:  WCCM Advice inappropriate.\n"));
        return(FetchFromServers);
    }
    if (u->IsAdvicePGID(vp->u.u_pgid)) {
        LOG(100, ("ADMON STATS:  WCCM Advice inappropriate.\n"));
        return(FetchFromServers);
    }
    if (u->IsAdviceValid(WeaklyConnectedCacheMissEventID, 1) != TRUE) {
        LOG(100, ("ADMON STATS:  WCCM Advice NOT valid. (uid = %d)\n", vuid));
        return(FetchFromServers);
    }

    GetPath(pathname, 1);

    LOG(100, ("Requesting WeaklyConnected CacheMiss Advice for path=%s, pid=%d...\n", 
	      pathname, vp->u.u_pid));
    vol->vsg->GetBandwidth(&CurrentBandwidth);
    advice = u->RequestWeaklyConnectedCacheMissAdvice(&fid, pathname, vp->u.u_pid, stat.Length, 
						      CurrentBandwidth, cf.Name());
    return(advice);
}

void fsobj::DisconnectedCacheMiss(vproc *vp, vuid_t vuid, char *comp) {
    userent *u;
    char pathname[MAXPATHLEN];

    GetUser(&u, vuid);
    assert(u != NULL);

    /* If advice not enabled, simply return */
    if (!AdviceEnabled) {
        LOG(100, ("ADMON STATS:  DMQ Advice NOT enabled.\n"));
        u->AdviceNotEnabled();
        return;
    }

    /* Check that:                                                     *
     *     (a) the request did NOT originate from the Hoard Daemon     *
     *     (b) the request did NOT originate from that AdviceMonitor,  *
     *     (c) the user is running an AdviceMonitor,                   *
     * and (d) the volent is non-NULL.                                 */
    assert(vp != NULL);
    if (vp->type == VPT_HDBDaemon) {
	LOG(100, ("ADMON STATS:  DMQ Advice inappropriate.\n"));
        return;
    }
    if (u->IsAdvicePGID(vp->u.u_pgid)) {
        LOG(100, ("ADMON STATS:  DMQ Advice inappropriate.\n"));
        return;
    }
    if (u->IsAdviceValid(DisconnectedCacheMissEventID, 1) != TRUE) {
        LOG(100, ("ADMON STATS:  DMQ Advice NOT valid. (uid = %d)\n", vuid));
        return;
    }
    if (vol == NULL) {
        LOG(100, ("ADMON STATS:  DMQ volent is NULL.\n"));
        u->VolumeNull();
        return;
    }

    /* Get the pathname */
    GetPath(pathname, 1);
 /*
    if (f != NULL)
        f->GetPath(pathname, 1);
    else {
        v->GetMountPath(pathname, 0);
	assert(key != NULL);
	if ((key->Vnode != ROOT_VNODE) || (key->Unique != ROOT_UNIQUE)) 
	    strcat(pathname, "/???");
    }
 */
    if (comp) {
        strcat(pathname, "/");
	strcat(pathname,comp);
    }

    /* Make the request */
    LOG(100, ("Requesting Disconnected CacheMiss Questionnaire...1\n"));
    u->RequestDisconnectedQuestionnaire(&fid, pathname, vp->u.u_pid, vol->GetDisconnectionTime());
}




/*  *****  MLE Linkage  *****  */

/* MUST be called from within transaction! */
void fsobj::AttachMleBinding(binding *b) {
    /* Sanity checks. */
    if (b->bindee != 0) {
	print(logFile);
	b->print(logFile);
	Choke("fsobj::AttachMleBinding: bindee != 0");
    }
    if (LogLevel >= 1000) {
	dprint("fsobj::AttachMleBinding:\n");
	print(logFile);
	b->print(logFile);
    }

    /* Attach ourselves to the binding. */
    if (mle_bindings == 0)
	mle_bindings = new dlist;
    mle_bindings->append(&b->bindee_handle);
    b->bindee = this;

    /* Set our "dirty" flag if this is the first binding. (i.e. this fso has an mle) */
    if (mle_bindings->count() == 1) {
	MakeDirty();
    }
    else {
	FSO_ASSERT(this, DIRTY(this));
    }
}


/* MUST be called from within transaction! */
void fsobj::DetachMleBinding(binding *b) {
    /* Sanity checks. */
    if (b->bindee != this) {
	print(logFile);
	if (b != 0) b->print(logFile);
	Choke("fsobj::DetachMleBinding: bindee != this");
    }
    if (LogLevel >= 1000) {
	dprint("fsobj::DetachMleBinding:\n");
	print(logFile);
	b->print(logFile);
    }
    FSO_ASSERT(this, DIRTY(this));
    FSO_ASSERT(this, mle_bindings != 0);

    /* Detach ourselves from the binding. */
    if (mle_bindings->remove(&b->bindee_handle) != &b->bindee_handle)
	{ print(logFile); b->print(logFile); Choke("fsobj::DetachMleBinding: bindee remove"); }
    b->bindee = 0;

    /* Clear our "dirty" flag if this was the last binding. */
    if (mle_bindings->count() == 0) {
	MakeClean();
    }
}


/* MUST NOT be called from within transaction! */
void fsobj::CancelStores() {
    if (!DIRTY(this))
	{ print(logFile); Choke("fsobj::CancelStores: !DIRTY"); }

    vol->CancelStores(&fid);
}


/*  *****  Data Contents  *****  */

/* MUST be called from within transaction! */
/* Call with object write-locked. */
/* If there are readers of this file, they will have it change from underneath them! */
void fsobj::DiscardData() {
    if (!HAVEDATA(this))
	{ print(logFile); Choke("fsobj::DiscardData: !HAVEDATA"); }
    if (WRITING(this) || EXECUTING(this))
	{ print(logFile); Choke("fsobj::DiscardData: WRITING || EXECUTING"); }

    LOG(10, ("fsobj::DiscardData: (%x.%x.%x)\n",
	      fid.Volume, fid.Vnode, fid.Unique));

    RVMLIB_REC_OBJECT(data);
    switch(stat.VnodeType) {
	case File:
	    {
	    /* 
	     * Return cache-file blocks. Check the cache file length instead of
	     * stat.Length because they may differ legitimately if a cache file 
	     * validation fails or if the file is stored compressed.
	     */
	    FSDB->FreeBlocks((int) NBLOCKS((Simulating ? stat.Length : data.file->Length())));	/* XXX */
	    data.file->Truncate(0);
	    data.file = 0;
	    }
	    break;

	case Directory:
	    {
	    /* Mount points MUST be unmounted before their data can be discarded! */
	    FSO_ASSERT(this, !IsMtPt());

	    /* Return cache-file blocks associated with Unix-format directory. */
	    if (data.dir->udcf) {
		FSO_ASSERT(this, !Simulating);	/* XXX */
		FSDB->FreeBlocks(NBLOCKS(data.dir->udcf->Length()));
		data.dir->udcf->Truncate(0);
		data.dir->udcf = 0;
		data.dir->udcfvalid = 0;
	    }

	    /* Get rid of RVM data. */
	    DH_FreeData(&data.dir->dh);
	    rvmlib_rec_free(data.dir);
	    data.dir = 0;

	    break;
	    }
	case SymbolicLink:
	    {
	    /* Get rid of RVM data. */
	    rvmlib_rec_free(data.symlink);
	    data.symlink = 0;
	    }
	    break;

	case Invalid:
	    Choke("fsobj::DiscardData: bogus VnodeType (%d)", stat.VnodeType);
    }
}


/*  *****  Fake Object Management  *****  */

/* local-repair modification */
/* MUST NOT be called from within transaction! */
/* Transform a fresh fsobj into a fake directory or MTLink. */
/* Call with object write-locked. */
/* returns 0 if successful, ENOENT if the parent cannot
   be found. */
int fsobj::Fakeify() {
    LOG(1, ("fsobj::Fakeify: %s, %x.%x.%x\n",
	     comp, fid.Volume, fid.Vnode, fid.Unique));

    fsobj *pf = 0;
    if (!IsRoot()) {
	/* Laboriously scan database to find our parent! */
	fso_vol_iterator next(NL, vol);
	while (pf = next()) {
	    if (!pf->IsDir() || pf->IsMtPt()) continue;
	    if (!HAVEDATA(pf)) continue;
	    if (!pf->dir_IsParent(&fid)) continue;

	    /* Found! */
	    break;
	}
	if (pf == 0) {
	    LOG(0, ("fsobj::Fakeify: %s, %x.%x.%x, parent not found\n",
		    comp, fid.Volume, fid.Vnode, fid.Unique));
	    return(ENOENT);
	}
    }

    // Either (pf == 0 and this is the volume root) OR (pf != 0 and it isn't)

    Recov_BeginTrans();
    RVMLIB_REC_OBJECT(*this);
    
    flags.fake = 1;
    if (!IsRoot())  // If we're not the root, pf != 0
	    pf->AttachChild(this);

    unsigned long volumehosts[VSG_MEMBERS];
    srvent *s;
    char Name[CODA_MAXNAMLEN];
    int i;
    if (FID_IsFake(&fid)) {		/* Fake MTLink */
	    /* Initialize status. */
	    stat.DataVersion = 1;
	    stat.Mode = 0644;
	    stat.Owner = V_UID;
	    stat.Length = 27;		/* "@XXXXXXXX.YYYYYYYY.ZZZZZZZZ" */
	    stat.Date = Vtime();
	    stat.LinkCount = 1;
	    stat.VnodeType = SymbolicLink;
	    Matriculate();
	    pfid = pf->fid;
	    pfso = pf;

	    /* local-repair modification */
	    if (!strcmp(comp, "local")) {
		/* the first specical case, fake link for a local object */
		LOG(100, ("fsobj::Fakeify: fake link for a local object 0x%x.%x.%x\n",
			  fid.Volume, fid.Vnode, fid.Unique));
		LOG(100, ("fsobj::Fakeify: parent fid for the fake link is 0x%x.%x.%x\n",
			  pfid.Volume, pfid.Vnode, pfid.Unique));
		flags.local = 1;
		ViceFid *LocalFid = LRDB->RFM_LookupLocalRoot(&pfid);
		FSO_ASSERT(this, LocalFid);
		/* Write out the link contents. */
		data.symlink = (char *)rvmlib_rec_malloc((unsigned) stat.Length);
		rvmlib_set_range(data.symlink, stat.Length);
		sprintf(data.symlink, "@%08x.%08x.%08x", LocalFid->Volume, 
			LocalFid->Vnode, LocalFid->Unique);
		LOG(100, ("fsobj::Fakeify: making %x.%x.%x a symlink %s\n",
			  fid.Volume, fid.Vnode, fid.Unique, data.symlink));
	    } else {
		if (!strcmp(comp, "global")) {
		    /* the second specical case, fake link for a global object */
		    LOG(100, ("fsobj::Fakeify: fake link for a global object 0x%x.%x.%x\n",
			      fid.Volume, fid.Vnode, fid.Unique));
		    LOG(100, ("fsobj::Fakeify: parent fid for the fake link is 0x%x.%x.%x\n",
			      pfid.Volume, pfid.Vnode, pfid.Unique));
		    flags.local = 1;
		    ViceFid *GlobalFid = LRDB->RFM_LookupGlobalRoot(&pfid);
		    FSO_ASSERT(this, GlobalFid);
		    /* Write out the link contents. */
		    data.symlink = (char *)rvmlib_rec_malloc((unsigned) stat.Length);
		    rvmlib_set_range(data.symlink, stat.Length);
		    sprintf(data.symlink, "@%08x.%08x.%08x", GlobalFid->Volume,
			    GlobalFid->Vnode, GlobalFid->Unique);
		    LOG(100, ("fsobj::Fakeify: making %x.%x.%x a symlink %s\n",
			      fid.Volume, fid.Vnode, fid.Unique, data.symlink));
		} else {
		    /* the normal fake link */
		    /* get the volumeid corresponding to the server name */
		    /* A big assumption here is that the host order in the
		       server array returned by GetHosts is the
		       same as the volume id order in the vdb.*/
		    vol->vsg->GetHosts(volumehosts);
		    /* find the server first */
		    for (i = 0; i < VSG_MEMBERS; i++) {
			if (volumehosts[i] == 0) continue;
			if ((s = FindServer(volumehosts[i])) &&
			    (s->name)) {
			    if (!strcmp(s->name, comp)) break;
			}
			else {
			    unsigned long vh = atol(comp);
			    if (vh == volumehosts[i]) break;
			}
		    }
		    if (i == VSG_MEMBERS) 
		      // server not found 
		      Choke("fsobj::fakeify couldn't find the server for %s\n", 
			    comp);
		    unsigned long rwvolumeid = vol->u.rep.RWVols[i];
		    
		    /* Write out the link contents. */
		    data.symlink = (char *)rvmlib_rec_malloc((unsigned) stat.Length);
		    rvmlib_set_range(data.symlink, stat.Length);
		    sprintf(data.symlink, "@%08x.%08x.%08x", rwvolumeid, pfid.Vnode,
			    pfid.Unique);
		    LOG(1, ("fsobj::Fakeify: making %x.%x.%x a symlink %s\n",
			    fid.Volume, fid.Vnode, fid.Unique, data.symlink));
		}
	    }
	    UpdateCacheStats(&FSDB->FileDataStats, CREATE, BLOCKS(this));
	}
	else {				/* Fake Directory */
	    /* Initialize status. */
	    stat.DataVersion = 1;
	    stat.Mode = 0444;
	    stat.Owner = V_UID;
	    stat.Length = 0;
	    stat.Date = Vtime();
	    stat.LinkCount = 2;
	    stat.VnodeType = Directory;
	    /* Access rights are not needed! */
	    Matriculate();
	    if (pf != 0) {
		pfid = pf->fid;
		pfso = pf;
	    }

	    /* Create the target directory. */
	    dir_MakeDir();
	    stat.Length = dir_Length();
	    UpdateCacheStats(&FSDB->DirDataStats, CREATE, BLOCKS(this));

	    /* Make entries for each of the rw-replicas. */
	    vol->vsg->GetHosts(volumehosts);
	    for (i = 0; i < VSG_MEMBERS; i++) {
		if (volumehosts[i] == 0) continue;
		srvent *s;
		char Name[CODA_MAXNAMLEN];
		if ((s = FindServer(volumehosts[i])) &&
		    (s->name))
		    sprintf(Name, "%s", s->name);
		else
		    sprintf(Name, "%08x", volumehosts[i]);
		ViceFid FakeFid = vol->GenerateFakeFid();
		LOG(1, ("fsobj::Fakeify: new entry (%s, %x.%x.%x)\n",
			Name, FakeFid.Volume, FakeFid.Vnode, FakeFid.Unique));
		dir_Create(Name, &FakeFid);
	    }
	}

    Reference();
    ComputePriority();
    Recov_EndTrans(CMFP);

    return(0);
}


/*  *****  Local Synchronization  *****  */

void fsobj::Lock(LockLevel level) {
    LOG(1000, ("fsobj::Lock: (%x.%x.%x) level = %s\n",
		fid.Volume, fid.Vnode, fid.Unique, ((level == RD)?"RD":"WR")));

    if (level != RD && level != WR)
	{ print(logFile); Choke("fsobj::Lock: bogus lock level %d", level); }

    FSO_HOLD(this);
    while (level == RD ? (writers > 0) : (writers > 0 || readers > 0)) {
	/* 
	 * if the object is being backfetched for reintegration, make
	 * a shadow and retry.  Note that if it is being backfetched, there  
	 * should be no writers, and there should be at least one reader.
	 */
	if (DIRTY(this) && IsBackFetching()) {
	    ASSERT(readers > 0 && writers == 0);
	    LOG(0, ("WAITING(%x.%x.%x) locked for backfetch, shadowing\n",
		    fid.Volume, fid.Vnode, fid.Unique));
	    START_TIMING();
	    MakeShadow();
	    END_TIMING();
	    LOG(0, ("WAIT OVER, elapsed = %3.1f\n", elapsed));
	}

	if (shadow == 0) { 
	    LOG(0, ("WAITING(%x.%x.%x): level = %s, readers = %d, writers = %d\n",
		    fid.Volume, fid.Vnode, fid.Unique, 
		    lvlstr(level), readers, writers));
	    START_TIMING();
	    VprocWait(&sync);
	    END_TIMING();
	    LOG(0, ("WAIT OVER, elapsed = %3.1f\n", elapsed));
	}
    }
    level == RD ? (readers++) : (writers++);
}


void fsobj::PromoteLock() {
    FSO_HOLD(this);
    UnLock(RD);
    Lock(WR);
    FSO_RELE(this);
}


void fsobj::DemoteLock() {
    FSO_HOLD(this);
    UnLock(WR);
    Lock(RD);
    FSO_RELE(this);
}


void fsobj::UnLock(LockLevel level) {
    LOG(1000, ("fsobj::UnLock: (%x.%x.%x) level = %s\n",
		fid.Volume, fid.Vnode, fid.Unique, ((level == RD)?"RD":"WR")));

    if (level != RD && level != WR)
	{ print(logFile); Choke("fsobj::UnLock: bogus lock level %d", level); }

    if (refcnt <= 0)
	{ print(logFile); Choke("fsobj::UnLock: refcnt <= 0"); }
    (level == RD) ? (readers--) : (writers--);
    if (readers < 0 || writers < 0)
	{ print(logFile); Choke("fsobj::UnLock: readers = %d, writers = %d", readers, writers); }
    if (level == RD ? (readers == 0) : (writers == 0))
	VprocSignal(&sync);
    FSO_RELE(this);
}


/*  *****  Miscellaneous Utility Routines  *****  */

void fsobj::GetVattr(struct coda_vattr *vap) {
    /* Most attributes are derived from the VenusStat structure. */
    vap->va_type = FTTOVT(stat.VnodeType);
    vap->va_mode = stat.Mode ;

    vap->va_uid = (uid_t) stat.Owner;
    vap->va_gid = (vgid_t)V_GID;

    vap->va_fileid = (IsRoot() && u.mtpoint && !IsVenusRoot())
		       ? FidToNodeid(&u.mtpoint->fid)
		       : FidToNodeid(&fid);

    vap->va_nlink = stat.LinkCount;
    vap->va_size = (u_quad_t) stat.Length;
    vap->va_blocksize = V_BLKSIZE;
    vap->va_mtime.tv_sec = (time_t)stat.Date;
    vap->va_mtime.tv_nsec = 0;

    vap->va_atime = vap->va_mtime;
    vap->va_ctime.tv_sec = 0;
    vap->va_ctime.tv_nsec = 0;
    vap->va_flags = 0;
    vap->va_rdev = 1;
    vap->va_bytes = vap->va_size;

    /* If the object is currently open for writing we must physically 
       stat it to get its size and time info. */
    if (!Simulating && WRITING(this)) {
	struct stat tstat;
	cf.Stat(&tstat);

	vap->va_size = tstat.st_size;
	vap->va_mtime.tv_sec = tstat.st_mtime;
	vap->va_mtime.tv_nsec = 0;
	vap->va_atime = vap->va_mtime;
	vap->va_ctime.tv_sec = 0;
	vap->va_ctime.tv_nsec = 0;
    }

    VPROC_printvattr(vap);
}


void fsobj::ReturnEarly() {
    /* Only mutations on replicated objects can return early. */
    if (!flags.replicated) return;

    /* Only makes sense to return early to user requests. */
    vproc *v = VprocSelf();
    if (v->type != VPT_Worker) return;

    /* Oh man is this ugly. Why is this here and not in worker? -- DCS */
    /* Assumption: the opcode and unique fields of the w->msg->msg_ent are already filled in */
    worker *w = (worker *)v;
    switch (w->opcode) {
	union outputArgs *out;
	case CODA_CREATE:
	case CODA_MKDIR:
	    {	/* create and mkdir use exactly the same sized output structure */
	    if (w->msg == 0) Choke("fsobj::ReturnEarly: w->msg == 0");

	    out = (union outputArgs *)w->msg->msg_buf;
	    out->coda_create.oh.result = 0;
	    out->coda_create.VFid = fid;
	    DemoteLock();
	    GetVattr(&out->coda_create.attr);
	    PromoteLock();
	    w->Return(w->msg, sizeof (struct coda_create_out));
	    break;
	    }

	case CODA_CLOSE:
	    {
	    /* Don't return early here if we already did so in a callback handler! */
	    if (!(flags.era && FID_EQ(&w->StoreFid, &NullFid)))
		w->Return(0);
	    break;
	    }

	case CODA_IOCTL:
	    {
	    /* Huh. IOCTL in the kernel thinks there may be return data. Assume not. */
	    out = (union outputArgs *)w->msg->msg_buf;
	    out->coda_ioctl.len = 0; 
	    out->coda_ioctl.oh.result = 0;
	    w->Return(w->msg, sizeof (struct coda_ioctl_out));
	    break;
	    }

	case CODA_LINK:
	case CODA_REMOVE:
	case CODA_RENAME:
	case CODA_RMDIR:
	case CODA_SETATTR:
	case CODA_SYMLINK:
	    w->Return(0);
	    break;

	default:
	    Choke("fsobj::ReturnEarly: bogus opcode (%d)", w->opcode);
    }
}


/* Need not be called from within transaction! */
void fsobj::GetPath(char *buf, int fullpath) {
    if (IsRoot()) {
	if (!fullpath)
	    { strcpy(buf, "."); return; }

	if (fid.Volume == rootfid.Volume)
	    { strcpy(buf, venusRoot); return; }

	if (u.mtpoint == 0)
	    { strcpy(buf, "???"); return; }

	u.mtpoint->GetPath(buf, fullpath);
	return;
    }

    if (pfso == 0 && !FID_EQ(&pfid, &NullFid)) {
	fsobj *pf = FSDB->Find(&pfid);
	if (pf != 0 && HAVESTATUS(pf) && !GCABLE(pf)) {
	    pfso = pf;
	    pfso->AttachChild(this);
	}
    }

    if (pfso != 0)
	pfso->GetPath(buf, fullpath);
    else
	strcpy(buf, "???");
    strcat(buf, "/");
    strcat(buf, comp);
}


/* Virginal state may cause some access checks to be avoided. */
int fsobj::IsVirgin() {
    int virginal = 0;
    int i;

    if (flags.replicated) {
	for (i = 0; i < VSG_MEMBERS; i++)
	    if ((&(stat.VV.Versions.Site0))[i] > 1) break;
	if (i == VSG_MEMBERS) virginal = 1;

	/* If file is dirty, it's really virginal only if there are no stores in the CML! */
	if (virginal && IsFile() && DIRTY(this)) {
	    cml_iterator next(vol->CML, CommitOrder, &fid);
	    cmlent *m;
	    while (m = next())
		if (m->opcode == ViceNewStore_OP)
		    break;
	    if (m != 0) virginal = 0;
	}
    }
    else {
	if (stat.DataVersion == 0) virginal = 1;
    }

    return(virginal);
}


int fsobj::IsBackFetching() {
    cml_iterator next(vol->CML, CommitOrder, &fid);
    cmlent *m;

    while (m = next()) 
	if ((m->opcode == ViceNewStore_OP) && m->IsReintegrating()) 
	    return 1;

    return 0;
}


void fsobj::MakeShadow() {
    /* 
     * since only one reintegration is allowed at a time, there
     * can be only one shadow copy of any object.
     */
    ASSERT(shadow == 0);

    /*
     * Create a shadow, using a name both distinctive and that will
     * be garbage collected at startup.  Move the old cache file to it,
     * to preseve any backfetches in progress.  Then create a new
     * copy of the data for the fso, and unlock.
     */
    shadow = new CacheFile(-ix);
    cf.Move(shadow);
    cf.Copy(shadow);
    
    UnLock(RD);
}


void fsobj::RemoveShadow() {
    shadow->Remove();
    delete shadow;
    shadow = 0;
}


/* Only call this on directory objects (or mount points)! */
/* Locking is irrelevant, but this routine MUST NOT yield! */
void fsobj::CacheReport(int fd, int level) {
    FSO_ASSERT(this, IsDir());

    /* Indirect over mount points. */
    if (IsMtPt()) {
	u.mtpoint->CacheReport(fd, level);
	return;
    }

    /* Report [slots, blocks] for this directory and its children. */
    int slots = 0;
    int blocks = 0;
    if (children != 0) {
	/* N.B. Recursion here could overflow smallish stacks! */
	dlist_iterator next(*children);
	dlink *d;
	while (d = next()) {
	    fsobj *cf = strbase(fsobj, d, child_link);

	    slots++;
	    blocks += NBLOCKS(cf->cf.Length());
	}
    }
    fdprint(fd, "[ %3d  %5d ]      ", slots, blocks);
    for (int i = 0; i < level; i++) fdprint(fd, "   ");
    fdprint(fd, "%s\n", comp);

    /* Report child directories. */
    if (children != 0) {
	/* N.B. Recursion here could overflow smallish stacks! */
	dlist_iterator next(*children);
	dlink *d;
	while (d = next()) {
	    fsobj *cf = strbase(fsobj, d, child_link);

	    if (cf->IsDir())
		cf->CacheReport(fd, level + 1);
	}
    }
}

/* 
 * This is a simple-minded routine that estimates the cost of fetching an
 * object.  It assumes that the fsobj has a reasonable estimate as to the 
 * size of the object stored in stat.Length.
 *
 * The routine takes one argument -- whether the status block (type == 0) 
 * or the actual data (type == 1) is being fetched.  The default is data.
 */
int fsobj::EstimatedFetchCost(int type) {
    LOG(100, ("E fsobj::EstimatedFetchCost(%d)\n", type));

    long bw = UNSET_BW;	/* bandwidth, in bytes/sec */
    vol->vsg->GetBandwidth(&bw);

    if (bw == UNSET_BW)
        return(-1);
    else {    
        LOG(100, ("stat.Length = %d; Bandwidth = %d\n", stat.Length, bw));
	LOG(100, ("EstimatedFetchCost = %d\n", (int)stat.Length/bw));
        return( (int)stat.Length/bw ); 
    }
}

void fsobj::RecordReplacement(int status, int data) {
    char mountpath[MAXPATHLEN];
    char path[MAXPATHLEN];

    LOG(0, ("RecordReplacement(%d,%d)\n", status, data));

    assert(vol != NULL);
    vol->GetMountPath(mountpath, 0);
    GetPath(path, 1);    

    if (data)
      NotifyUserOfReplacement(&fid, path, status, 1);
    else
      NotifyUserOfReplacement(&fid, path, status, 0);

    LOG(0, ("RecordReplacement complete.\n"));
}

/* local-repair modification */
void fsobj::print(int fdes) {
    /* < address, fid, comp, vol > */
    fdprint(fdes, "%#08x : fid = (%x.%x.%x), comp = %s, vol = %x\n",
	     (long)this, fid.Volume, fid.Vnode, fid.Unique, comp, vol);

    /* < FsoState, VenusStat, Replica Control Rights, Access Rights, flags > */
    fdprint(fdes, "\tstate = %s, stat = { %d, %d, %d, %d, %#o, %d, %s }, rc rights = %d\n",
	     PrintFsoState(state), stat.Length, stat.DataVersion,
	     stat.Date, stat.Owner, stat.Mode, stat.LinkCount,
	     PrintVnodeType(stat.VnodeType), RcRights);
    fdprint(fdes, "\tVV = {[");
    for (int i = 0; i < VSG_MEMBERS; i++)
	fdprint(fdes, " %d", (&(stat.VV.Versions.Site0))[i]);
    fdprint(fdes, " ] [ %#x %d ] [ %#x ]}\n",
	     stat.VV.StoreId.Host, stat.VV.StoreId.Uniquifier, stat.VV.Flags);
    if (IsDir()) {
	fdprint(fdes, "\tac rights = { [%x %x%x]",
		AnyUser.rights, AnyUser.inuse, AnyUser.valid);
	for (int i = 0; i < CPSIZE; i++)
	    fdprint(fdes, " [%d %x %x%x]",
		    SpecificUser[i].uid, SpecificUser[i].rights,
		    SpecificUser[i].inuse, SpecificUser[i].valid);
	fdprint(fdes, " }\n");
    }
    fdprint(fdes, "\tvoltype = [%d %d %d %d], ucb = %d, fake = %d, fetching = %d local = %d\n",
	     flags.backup, flags.readonly, flags.replicated, flags.rwreplica,
	     flags.usecallback, flags.fake, flags.fetching, flags.local);
    fdprint(fdes, "\trep = %d, data = %d, owrite = %d, era = %d, dirty = %d, shadow = %d\n",
	     flags.replaceable, data.havedata != 0, flags.owrite, flags.era, flags.dirty,
	     shadow != 0);

    /* < mvstat [rootfid | mtptfid] > */
    fdprint(fdes, "\tmvstat = %s", PrintMvStat(mvstat));
    if (IsMtPt())
	fdprint(fdes, ", root = (%x.%x.%x)",
		u.root->fid.Volume, u.root->fid.Vnode, u.root->fid.Unique);
    if (IsRoot() && u.mtpoint)
	fdprint(fdes, ", mtpoint = (%x.%x.%x)",
		u.mtpoint->fid.Volume, u.mtpoint->fid.Vnode, u.mtpoint->fid.Unique);
    fdprint(fdes, "\n");

    /* < parent_fid, pfso, child count > */
    fdprint(fdes, "\tparent = (%x.%x.%x, %x), children = %d\n",
	     pfid.Volume, pfid.Vnode, pfid.Unique,
	     pfso, (children ? children->count() : 0));

    /* < priority, HoardPri, HoardVuid, hdb_bindings, LastRef > */
    fdprint(fdes, "\tpriority = %d (%d), hoard = [%d, %d, %d], lastref = %d\n",
	     priority, flags.random, HoardPri, HoardVuid,
	     (hdb_bindings ? hdb_bindings->count() : 0), FSDB->LastRef[ix]);
    if (hdb_bindings) {
      dlist_iterator next(*hdb_bindings);
      dlink *d;
      while (d = next()) {
	binding *b = strbase(binding, d, bindee_handle);
	namectxt *nc = (namectxt *)b->binder;
	if (nc != NULL) 
	  nc->print(fdes);
      }

    }

    fdprint(fdes, "\tDisconnectionStatistics:  Used = %d - Unused = %d - SinceLastUse = %d\n",
	    DisconnectionsUsed, DisconnectionsUnused, DisconnectionsSinceUse);

    /* < mle_bindings, CleanStat > */
    fdprint(fdes, "\tmle_bindings = (%x, %d), cleanstat = [%d, %d]\n",
	     mle_bindings, (mle_bindings ? mle_bindings->count() : 0),
	     CleanStat.Length, CleanStat.Date);

    /* < cachefile, [directory | symlink] contents > */
    fdprint(fdes, "\tcachefile = ");
    cf.print(fdes);
    if (IsDir() && !IsMtPt()) {
	if (data.dir == 0) {
	    fdprint(fdes, "\tdirectory = 0\n");
	}
	else {
	    int pagecount;
	    fdprint(fdes, "\tdirectory = %x, udcf = [%x, %d]\n",
		    data.dir, data.dir->udcf, data.dir->udcfvalid);
	    fdprint(fdes, "\tpages = %d, malloc bitmap = [ ", pagecount);
	    fdprint(fdes, "data at %p ", DH_Data(&(data.dir->dh)));
	    fdprint(fdes, "]\n");
	}
    }
    if (IsSymLink() || IsMtPt()) {
	fdprint(fdes, "\tlink contents: %s\n",
		data.symlink ? data.symlink : "N/A");
    }

    /* < references, openers > */
    fdprint(fdes, "\trefs = [%d %d %d], openers = [%d %d %d]",
	     readers, writers, refcnt,
	     (openers - Writers - Execers), Writers, Execers);
    fdprint(fdes, "\tlastresolved = %u\n", lastresolved);
    fdprint(fdes, "\tdiscread = %d\n", flags.discread);
}

void fsobj::ListCache(FILE *fp, int long_format, unsigned int valid)
{
  /* list in long format, if long_format == 1;
     list fsobjs
          such as valid (valid == 1), non-valid (valid == 2) or all (valid == 3) */

  char path[MAXPATHLEN];
  GetPath(path, 0);		/* Get relative pathname. */    

  switch (valid) {
  case 1: /* only valid */
    if ( DATAVALID(this) && STATUSVALID(this) )
      if (!long_format)
	ListCacheShort(fp);
      else
	ListCacheLong(fp);
    break;
  case 2: /* only non-valid */
    if ( !DATAVALID(this) || !STATUSVALID(this) )
      if (!long_format)
	ListCacheShort(fp);
      else
	ListCacheLong(fp);
    break;
  case 3: /* all */
  default:
      if (!long_format)
	ListCacheShort(fp);
      else
	ListCacheLong(fp);
  }
}

void fsobj::ListCacheShort(FILE* fp)
{
  char path[MAXPATHLEN];
  GetPath(path, 0);		/* Get relative pathname. */
  char valid = ((DATAVALID(this) && STATUSVALID(this) && !DIRTY(this)) ? ' ' : '*');

  fprintf(fp, "%c %s\n", valid, path);
  fflush(fp);
}

void fsobj::ListCacheLong(FILE* fp)
{
  char path[MAXPATHLEN];
  GetPath(path, 0);		/* Get relative pathname. */    
  char valid = ((DATAVALID(this) && STATUSVALID(this) && !DIRTY(this)) ? ' ' : '*');
  char type = ( IsDir() ? 'd' : ( IsSymLink() ? 's' : 'f') );
  char linktype = ' ';
  if ( type != 'f' )
    linktype = (IsMtPt() ? 'm' :
		(IsMTLink() ? 'l' :
		 (IsRoot() ? '/' :
		  (IsVenusRoot() ? 'v': ' '))));

  fprintf(fp, "%c %c%c %s  (%x.%x.%x)\n",
	  valid, type, linktype, path, fid.Volume, fid.Vnode, fid.Unique);	
  fflush(fp);
}


/* *****  Iterator  ***** */

fso_iterator::fso_iterator(LockLevel level, ViceFid *key) : rec_ohashtab_iterator(FSDB->htab, key) {
    clevel = level;
    cvol = 0;
}

/* Returns entry locked as specified. */
fsobj *fso_iterator::operator()() {
    for (;;) {
	rec_olink *o = rec_ohashtab_iterator::operator()();
	if (!o) return(0);

	fsobj *f = strbase(fsobj, o, primary_handle);
	if (cvol == 0 || cvol == f->vol) {
	    if (clevel != NL) f->Lock(clevel);
	    return(f);
	}
    }
}

/* for iteration by volume */
fso_vol_iterator::fso_vol_iterator(LockLevel level, volent *vol) : olist_iterator(*vol->fso_list) {
    clevel = level;
}

fsobj *fso_vol_iterator::operator()() {
    olink *o = olist_iterator::operator()();
    if (!o) return(0);

    fsobj *f = strbase(fsobj, o, vol_handle);
    if (clevel != NL) f->Lock(clevel);
    return(f);
}

void fsobj::GetOperationState(int *conn, int *tid)
{
    if (HOARDING(this)) {
	*conn = 1;
	*tid = 0;
	return;
    }
    if (EMULATING(this)) {
	*conn = 0;
	*tid = -1;
	return;
    }

    OBJ_ASSERT(this, LOGGING(this));
    /* 
     * check to see if the object is within the global portion of a subtree
     * that is currently being repaired. (only when a repair session is on)
     */
    *tid = -1;
    int repair_mutation = 0;
    if (LRDB->repair_root_fid != NULL) {
	fsobj *cfo = this;
	while (cfo != NULL) {
	    if (cfo->IsLocalObj())
	      break;
	    if (cfo->IsRoot())
	      cfo = cfo->u.mtpoint;
	    else
	      cfo = cfo->pfso;
	}
	if ((cfo != NULL) && (!bcmp((const void *)&(cfo->pfid), (const void *)LRDB->repair_root_fid, (int)sizeof(ViceFid)) ||
            !bcmp((const void *)&(cfo->pfid), (const void *)LRDB->RFM_FakeRootToParent(LRDB->repair_root_fid), (int)sizeof(ViceFid)))) {
	    repair_mutation = 1;
	}
    }
    if (repair_mutation) {
	*tid = LRDB->repair_session_tid;
	if (LRDB->repair_session_mode == REP_SCRATCH_MODE) {
	    *conn = 0;
	} else {
	    *conn = 1;
	}
    } else {
	*conn = 0;
    }
}
