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
 *    Implementation of the Venus Reintegrate facility.
 *
 *    Reintegration is essentially the merge phase of Davidson's Optimistic Protocol.
 *    Our implementation is highly stylized, however, to account for the fact that our
 *    "database" is a Unix-like file system rather than a conventional database whose
 *    operations and integrity constraints are well specified.
 *
 *    Specific details of our implementation are the following:
 *       1. the unit of logging and reintegration is the volume; this follows from:
 *          - a "transaction" may reference objects in only one volume
 *          - a volume is contained within a single storage group
 *       2. the client, i.e., Venus, is always the coordinator for the merge
 *       3. the server does NOT maintain a log of its partitioned operations;
 *          instead, we rely on the client to supply version information with its
 *          "transactions" sufficient to allow the server to distinguish conflicts
 *       4. "transactions" map one-to-one onto Vice operations;
 *          non-mutating "transactions" are not logged since they can always be 
 *          "serialized" before any conflicting mutations
 *
 */


#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <stdio.h>
#include <struct.h>

#include <errors.h>

#ifdef __cplusplus
}
#endif __cplusplus

/* interfaces */
#include <vice.h>

/* from venus */
#include "local.h"
#include "user.h"
#include "venus.private.h"
#include "venusvol.h"
#include "vproc.h"
#include "advice_daemon.h"


/* must not be called from within a transaction */
void volent::Reintegrate()
{
    userent *u;

    LOG(0, ("volent::Reintegrate\n"));

    /* 
     * this flag keeps multiple reintegrators from interfering with
     * each other in the same volume.  This is necessary even with 
     * the cur_reint_tid field, because the latter is reset between
     * iterations of the loop below.  Without the flag, other threads
     * may slip in between commit or abort of a record.
     */
    if (IsReintegrating())
	return;

    GetUser(&u, CML.owner);
    assert(u != NULL);
    if (AdviceEnabled)
        u->NotifyReintegrationActive(name);

    flags.reintegrating = 1;

    /* enter the volume */
    vproc *v = VprocSelf();
    v->Begin_VFS(vid, VFSOP_REINTEGRATE);
    VOL_ASSERT(this, v->u.u_error == 0);

    /* prevent ASRs from slipping in and leaving records we might reintegrate. */
    DisableASR(V_UID);

    /* lock the CML to prevent a checkpointer from getting confused. */
    ObtainReadLock(&CML_lock);

    /* NOW we're ready.*/
    /* step 1.  scan the log, cancelling stores for open-for-write files. */
    CML.CancelStores();

    /*
     * step 2.  scan the log, gathering records that are ready to 
     * to reintegrate.
     */
    int thisTid = -GetReintId();
    int nrecs = 0;

    CML.GetReintegrateable(thisTid, &nrecs);

    /* step 3.  if we've come up with anything, reintegrate it. */
    int code = 0;
    if (CML.HaveElements(thisTid)) {		
	int startedrecs = CML.count();
    
        code = IncReintegrate(thisTid);

        eprint("Reintegrate: %s, %d/%d records, result = %s", 
		name, nrecs, startedrecs, VenusRetStr(code));

    } else if (CML.GetFatHead(thisTid)) {
	/* 
	 * there a fat store blocking the head of the log.
	 * try to reintegrate a piece of it.
	 */
	code = PartialReintegrate(thisTid);

	eprint("Reintegrate: %s, partial record, result = %s", 
	       name, VenusRetStr(code));
    }

    flags.reintegrating = 0;

    /* 
     * clear CML owner if possible.  If there are still mutators in the volume,
     * the owner must remain set (it is overloaded).  In that case the last
     * mutator will clear the owner on volume exit.
     */
    if (CML.count() == 0  &&  mutator_count == 0)
        CML.owner = UNSET_UID;

    /* trigger a transition if necessary */
    if ((CML.count() == 0 || (CML.count() > 0 && !ContainUnrepairedCML()))
	&& flags.logv == 0 && state == Logging) {
	flags.transition_pending = 1;
    }

    /* if code was non-zero, return EINVAL to End_VFS to force this
       reintegration to inc fail count rather than success count */
    VOL_ASSERT(this, v->u.u_error == 0);
    if (code) v->u.u_error = EINVAL;

    EnableASR(V_UID);	
    ReleaseReadLock(&CML_lock);

    /* Surrender control of the volume. */
    v->End_VFS();

    /* reset it, 'cause we can't leave errors just laying around */
    v->u.u_error = 0;

    /* Let user know reintegration has completed */
    if (AdviceEnabled)
        u->NotifyReintegrationCompleted(name);
}


/*
 *
 *    Reintegration consists of the following phases:
 *       1. (late) prelude
 *       2. interlude
 *       3. postlude
 *
 */

/* must not be called from within a transaction */
int volent::IncReintegrate(int tid) {
    LOG(0, ("volent::IncReintegrate: (%s, %d) vuid = %d\n",
	    name, tid, CML.owner));
    /* check if transaction "tid" has any cmlent objects */
    if (!CML.HaveElements(tid)) {
	LOG(0, ("volent::IncReintegrate: transaction %d does not have any elements\n", 
		tid));
	return 0;
    }

    /* check if volume state is Logging or not */
    if (state != Logging) return ETIMEDOUT;	/* it must be Emulating */

    int code = 0;

    /* Get the current CML stats for reporting diffs */
    int StartCancelled = RecordsCancelled;
    int StartCommitted = RecordsCommitted;
    int StartAborted = RecordsAborted;
    int StartRealloced = FidsRealloced;
    long StartBackFetched = BytesBackFetched;

    /* Get the NEW CML stats. */
    cmlstats current;
    cmlstats cancelled;
    CML.IncGetStats(current, cancelled, tid);	/* get incremental stats */

    int pre_vm_usage = VMUsage();
    START_TIMING();
    float pre_elapsed = 0.0, inter_elapsed = 0.0, post_elapsed = 0.0;
    for (;;) {
	char *buf = 0;
	int bufsize = 0;
	int locked = 0;
	ViceVersionVector UpdateSet;
	code = 0;
	cur_reint_tid = tid; 

	/* Steps 1-3 constitute the ``late prelude.'' */
	{
	    START_TIMING();
	    /* 
	     * Step 1 is to reallocate real fids for new client-log 
	     * objects that were created with "local" fids.
	     */
	    code = CML.IncReallocFids(tid);
	    if (code != 0) goto CheckResult;

	    /*
	     * Step 2 is to reference count all objects, and lock 
	     * operands of stores.  These will be backfetched.
	     */
	    CML.LockObjs(tid);
	    locked = 1;

	    /* 
	     * Step 3 is to "thread" the log and pack it into a buffer 
	     * (buffer is allocated by pack routine).
	     */
	    CML.IncThread(tid);
	    CML.IncPack(&buf, &bufsize, tid);

	    END_TIMING();
	    pre_elapsed = elapsed;
	}

	/* 
	 * Step 4 is to have the server(s) replay the client modify log 
	 * via a Reintegrate RPC. 
	 */
	{
	    START_TIMING();

	    code = CML.COP1(buf, bufsize, &UpdateSet);

	    END_TIMING();
	    inter_elapsed = elapsed;
	}

	{
CheckResult:
	    START_TIMING();
	    if (buf != 0) delete buf;

	    if (locked) CML.UnLockObjs(tid);

	    switch(code) {
	    case ESUCCESS:
	    case EALREADY:
		/* Commit logged mutations upon successful replay at server. */
		CML.IncCommit(&UpdateSet, tid);
		LOG(0, ("volent::IncReintegrate: committed\n"));

		CML.ClearPending();
		break;

	    case ETIMEDOUT:
		/*
		 * We cannot cancel pending records because we do not know if 
		 * the reintegration actually occurred at the server.  If the 
		 * RPC reply was lost it is possible that it succeeded.  Note
		 * the next attempt may involve a different set of records.
		 */
		break;

	    case ERETRY:
	    case EWOULDBLOCK:
		/* 
		 * if any cmlents we were working on are still around and 
		 * should now be cancelled, do it.
		 */
		CML.CancelPending();

		/* 
		 * We do our own retrying here, because the code in 
		 * vproc::End_VFS() causes an entirely new vproc to start 
		 * up for each transition into reintegrating state (and 
		 * thus it has no knowledge of how many "waits" have already 
		 * been done).  Unfortunately, that means we end up 
		 * duplicating some code here.  Finally, if a transition is 
		 * pending, we'd better take it. 
		 */
		if (flags.transition_pending) {
		    break;
		}
		else {
		    vproc *v = VprocSelf();

		    if (code == ERETRY) {
			v->u.u_retrycnt++;
extern int VprocRetryN;
			if (v->u.u_retrycnt <= VprocRetryN) {
extern struct timeval *VprocRetryBeta;
			    VprocSleep(&VprocRetryBeta[v->u.u_retrycnt]);
			    continue;
                        }
		    }
		    if (code == EWOULDBLOCK) {
			v->u.u_wdblkcnt++;
			if (v->u.u_wdblkcnt <= 20) {	/* XXX */
			    eprint("Volume %s busy, waiting", name);
			    struct timeval delay;
			    delay.tv_sec = 20;		/* XXX */
			    delay.tv_usec = 0;
			    VprocSleep(&delay);
			    continue;
			}
		    }
		    /* Fall through if retry/wdblk count was exceeded ... */
		}

	    case EINCOMPATIBLE:	
	    default:
		/* non-retryable failures */

		LOG(0, ("volent::IncReintegrate: fail code = %d\n", code));
		CML.print(logFile);
		/* 
		 * checkpoint the log before localizing or aborting.
		 * release read lock; it will be boosted in CML.Checkpoint.
		 * Note that we may have to wait until other mutators finish 
		 * mucking with the volume/log, which means the state we checkpoint 
		 * might not be the state we had.  Note that we MUST unlock objects 
		 * before boosting this lock to prevent deadlock with mutator threads.
		 */
		ReleaseReadLock(&CML_lock);
		CML.CheckPoint(0);
		ObtainReadLock(&CML_lock);

		CML.CancelPending();       
		/*
		 * localize or abort the record if this is not a local repair
		 * session.  Local repair does its own error handling.
		 */
		if (tid != LRDB->GetRepairSessionTid())
		    CML.HandleFailedMLE();

		break;
	    }

	    END_TIMING();
	    post_elapsed = elapsed;
	    break;
	}
    }

    cur_reint_tid = UNSET_TID;
    END_TIMING();
    int post_vm_usage = VMUsage();
    LOG(0, ("IncReintegrate: (%s,%d) result = %s, elapsed = %3.1f (%3.1f, %3.1f, %3.1f)\n", 
	    name, tid, VenusRetStr(code), elapsed, pre_elapsed, inter_elapsed, post_elapsed));
    LOG(100, ("\tdelta_vm = %x, old stats = [%d, %d, %d, %d, %d]\n",
	   post_vm_usage - pre_vm_usage,
	   RecordsCancelled - StartCancelled, 
	   RecordsCommitted - StartCommitted, 
	   RecordsAborted - StartAborted,
	   FidsRealloced - StartRealloced,
	   BytesBackFetched - StartBackFetched));
    LOG(0, ("\tnew stats = [%4d, %5.1f, %7.1f, %4d, %5.1f], [%4d, %5.1f, %7.1f, %4d, %5.1f]\n",
	   current.store_count, current.store_size / 1024.0, 
	   current.store_contents_size / 1024.0,
	   current.other_count, current.other_size / 1024.0,
	   cancelled.store_count, cancelled.store_size / 1024.0, 
	   cancelled.store_contents_size / 1024.0,
	   cancelled.other_count, cancelled.other_size / 1024.0));

    CML.cancellations.store_count = 0;
    CML.cancellations.store_size = 0.0;
    CML.cancellations.store_contents_size = 0.0;
    CML.cancellations.other_count = 0;
    CML.cancellations.other_size = 0.0;

    return (code);
}


/*
 * Reintegrate some portion of the store record at the head
 * of the log.
 */
int volent::PartialReintegrate(int tid) {
    LOG(0, ("volent::PartialReintegrate: (%s, %d) vuid = %d\n",
	    name, tid, CML.owner));

    cmlent *m = CML.GetFatHead(tid);
    ASSERT(m && m->opcode == ViceNewStore_OP);

    int locked = 0;
    int code = 0;
    cur_reint_tid = tid; 
    ViceVersionVector UpdateSet;

    /* perform some late prelude functions. */
    {
	code = m->realloc();
	if (code != 0) goto CheckResult;

	/* lock object, for survival of mle as well */
	m->LockObj();
	locked = 1;
    }

    /* 
     * If this is a new transfer, get a handle from the server.
     * Otherwise, check the status of the handle.
     */
    {
	if (m->HaveReintegrationHandle()) 
	    code = m->ValidateReintegrationHandle();
	else 
	    code = m->GetReintegrationHandle();

	if (code != 0) goto CheckResult;
    }

    /* send some file data to the server */
    {
	while (!m->DoneSending() && (code == 0)) 
	    code = m->WriteReintegrationHandle();

	if (code != 0) goto CheckResult;
    }

    /* reintegrate the changes if all of the data is there */
    if (m->DoneSending()) {
	char *buf = 0;
	int bufsize = 0;

	CML.IncThread(tid);
	CML.IncPack(&buf, &bufsize, tid);

	code = m->CloseReintegrationHandle(buf, bufsize, &UpdateSet);
	if (buf != 0) delete buf;

	if (code != 0) goto CheckResult;
    }

CheckResult:
    if (locked) m->UnLockObj();

    /* allow log optimizations to go through. */
    cur_reint_tid = UNSET_TID;

    /*
     * In the following, calls to CancelPending may cancel the record
     * out from under us.
     */
    switch (code) {
    case ESUCCESS:
    case EALREADY:
	if (m->DoneSending()) {
	    /* Commit logged mutations upon successful replay at server. */
	    CML.IncCommit(&UpdateSet, tid);
	    LOG(0, ("volent::PartialReintegrate: committed\n"));

	    CML.ClearPending();
	} else {
	    /* allow an incompletely sent record to be cancelled. */
	    CML.CancelPending();
	}
	break;

    case ETIMEDOUT: 
    case EWOULDBLOCK:
    case ERETRY:
	CML.CancelPending();
	break;

    case EBADF:		/* bad handle */
	m->ClearReintegrationHandle();
	CML.CancelPending();
	code = ERETRY;	/* try again later */
	break;

    case EINCOMPATIBLE:
    default:
	/* non-retryable failures -- see IncReintegrate for comments */

	LOG(0, ("volent::PartialReintegrate: fail code = %d\n", code));
	CML.print(logFile);

	/* checkpoint the log */
	ReleaseReadLock(&CML_lock);
	CML.CheckPoint(0);
	ObtainReadLock(&CML_lock);

	/* cancel, localize, or abort the offending record */
	CML.CancelPending();       
	CML.HandleFailedMLE();

	break;
    }

    return(code);
}


/* 
 * determine if a volume has updates that may be reintegrated,
 * and return the number. humongous predicate check here.  
 */
int volent::ReadyToReintegrate() {
    int ready = 0;
    userent *u = 0;

    GetUser(&u, CML.owner);	/* if the CML is non-empty, u != 0 */
    /* 
     * we're a bit draconian about ASRs.  We want to avoid reintegrating
     * while an ASR is in progress, because the ASR uses the write
     * disconnected state in place of transactional support -- it requires
     * control of when its updates are sent back.  The ASRinProgress flag 
     * is Venus-wide, so the check is correct but more conservative than 
     * we would like.
     */
    if (IsWriteDisconnected() && 
	(CML.count() > 0) && u->TokensValid() &&
	!ASRinProgress && !IsReintegrating()) {
	    cmlent *m;
	    cml_iterator next(CML, CommitOrder);

	    while (m = next()) 
		ready += m->ReintReady();
    }

    PutUser(&u);
    return(ready);
}


/* need not be called from within a transaction */
int cmlent::ReintReady()
{
    /* check volume state */
    volent *vol = strbase(volent, log, CML);
    if (!(vol->IsWriteDisconnected())) {
	LOG(100, ("cmlent::ReintReady: not write-disconnected\n"));
	return 0;
    }

    if (vol->flags.transition_pending) {
	LOG(100, ("cmlent::ReintReady: transition_pending\n"));
	return 0;
    }

    /* check if its repair flag is set */
    if (flags.to_be_repaired || flags.repair_mutation) {
	LOG(100, ("cmlent::ReintReady: this is a repair related cmlent\n"));
	return 0;
    }

    if (ContainLocalFid()) {
	LOG(100, ("cmlent::ReintReady: contains local fid\n"));
	/* set its to_be_repaired flag */
	SetRepairFlag();
	return 0;
    }

    /* check if this record is part of a transaction (IOT, etc.) */
    if (tid > 0) {
	LOG(100, ("cmlent::ReintReady: transactional cmlent\n"));
	return 0; 
    }

    /* if vol staying write disconnected, check age. does not apply to ASRs */
    if (!ASRinProgress && !Aged() && vol->flags.logv) {
	LOG(100, ("cmlent::ReintReady: record too young\n"));
	return 0; 
    }

    return 1;
}


/* *****  Reintegrator  ***** */

PRIVATE const int ReintegratorStackSize = 65536;
PRIVATE const int MaxFreeReintegrators = 2;
PRIVATE const int ReintegratorPriority = LWP_NORMAL_PRIORITY-2;

/* local-repair modification */
class reintegrator : public vproc {
  friend void Reintegrate(volent *);

    static olist freelist;
    olink handle;
    struct Lock run_lock;

    reintegrator();
    reintegrator(reintegrator&);			/* not supported! */
    operator=(reintegrator&) { abort();	return(0); }	/* not supported! */
    ~reintegrator();

  public:
    void main(void *);
};

olist reintegrator::freelist;


/* This is the entry point for reintegration. */
/* It finds a free reintegrator (or creates a new one), 
   sets up its context, and gives it a poke. */
void Reintegrate(volent *v) {
    LOG(0, ("Reintegrate\n"));
    /* Get a free reintegrator. */
    reintegrator *r;
    olink *o = reintegrator::freelist.get();
    r = (o == 0)
      ? new reintegrator
      : strbase(reintegrator, o, handle);
    ASSERT(r->idle);

    /* Set up context for reintegrator. */
    r->u.Init();
#ifdef __MACH__
    r->u.u_cred.cr_ruid = v->CML.Owner();
#endif /* __MACH__ */
#ifdef __BSD44__
    r->u.u_cred.cr_uid = v->CML.Owner();
#endif /* __BSD44__ */
    r->u.u_vol = v;
    v->hold();		    /* vproc::End_VFS() will do release */

    /* Set it going. */
    r->idle = 0;
    VprocSignal((char *)r);	/* ignored for new reintegrators */
}


reintegrator::reintegrator() :
	vproc("Reintegrator", (PROCBODY) &reintegrator::main,
		VPT_Reintegrator, ReintegratorStackSize, ReintegratorPriority) {
    LOG(100, ("reintegrator::reintegrator(%#x): %-16s : lwpid = %d\n",
	       this, name, lwpid));

    idle = 1;
}


reintegrator::reintegrator(reintegrator& r) : vproc((vproc)r) {
    abort();
}


reintegrator::~reintegrator() {
    LOG(100, ("reintegrator::~reintegrator: %-16s : lwpid = %d\n", name, lwpid));

}


/* 
 * N.B. Vproc synchronization is not done in the usual way, with
 * the constructor signalling, and the new vproc waiting in its
 * main procedure for the constructor to poke it.  This handshake 
 * assumes that the new vproc has a thread priority greater than or
 * equal to its creator; only then does the a thread run when 
 * LWP_CreateProcess is called.  If the creator has a higher 
 * priority, the new reintegrator runs only when the creator is
 * suspended.  Otherwise, the reintegrator will run when created.
 * This can happen if a reintegrator creates another reintegrator.
 * In this case, the yield below allows the creator to fill in
 * the context.
 */
void reintegrator::main(void *parm) {
    /* Hack!  Vproc must yield before data members become valid! */
    VprocYield();

    for (;;) {
	if (idle) Choke("reintegrator::main: signalled but not dispatched!");
	if (!u.u_vol) Choke("reintegrator::main: no volume!");

	/* Do the reintegration. */
	u.u_vol->Reintegrate();
	
	seq++;
	idle = 1;

	/* Commit suicide if we already have enough free reintegrators. */
	if (freelist.count() == MaxFreeReintegrators) 
	    delete VprocSelf();

	/* Else put ourselves on free list. */
	freelist.append(&handle);

	/* Wait for new request. */
	VprocWait((char *)this);
    }
}
