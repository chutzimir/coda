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
 *    CFS calls1.
 *
 *    ToDo:
 *       1. All mutating Vice calls should have the following IN arguments:
 *            NewSid, NewMutator (implicit from connection), NewMtime, 
 *            OldVV and DataVersion (for each object), NewStatus (for each object)
 */


#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <stdio.h>
#include <sys/types.h>
#include <sys/file.h>

#include <rpc2.h>

#ifdef __cplusplus
}
#endif __cplusplus

/* interfaces */
#include <vice.h>

#include "comm.h"
#include "fso.h"
#include "mariner.h"
#include "venuscb.h"
#include "venus.private.h"
#include "venusrecov.h"
#include "venusvol.h"
#include "vproc.h"
#include "worker.h"


/*  *****  Remove  *****  */

/* MUST be called from within transaction! */
void fsobj::LocalRemove(Date_t Mtime, char *name, fsobj *target_fso) {
    /* Update parent status. */
    {
	/* Delete the target name from the directory. */
	dir_Delete(name);

	/* Update the status to reflect the delete. */
	RVMLIB_REC_OBJECT(stat);
	stat.DataVersion++;
	stat.Length = dir_Length();
	stat.Date = Mtime;
    }

    /* Update the target status. */
    {
	RVMLIB_REC_OBJECT(target_fso->stat);
	target_fso->stat.LinkCount--;
	if (target_fso->stat.LinkCount == 0) {
	    UpdateCacheStats(&FSDB->FileAttrStats, REMOVE,
			     NBLOCKS(sizeof(fsobj)));
	    UpdateCacheStats(&FSDB->FileDataStats, REMOVE,
			     BLOCKS(target_fso));
	    target_fso->Kill();
	}
	else {
	    target_fso->stat.DataVersion++;
	    target_fso->DetachHdbBindings();
	}
    }
}


int fsobj::ConnectedRemove(Date_t Mtime, vuid_t vuid, char *name, fsobj *target_fso) {
    FSO_ASSERT(this, HOARDING(this));

    int code = 0;

    /* Status parameters. */
    ViceStatus parent_status;
    VenusToViceStatus(&stat, &parent_status);
    ViceStatus target_status;
    VenusToViceStatus(&target_fso->stat, &target_status);
    {
	/* Temporary!  Until RPC interface is fixed!  -JJK */
	parent_status.Date = Mtime;
    }

    /* COP2 Piggybacking. */
    char PiggyData[COP2SIZE];
    RPC2_CountedBS PiggyBS;
    PiggyBS.SeqLen = 0;
    PiggyBS.SeqBody = (RPC2_ByteSeq)PiggyData;

    /* VCB Arguments */
    RPC2_Integer VS = 0;
    CallBackStatus VCBStatus = NoCallBack;
    RPC2_CountedBS OldVS; 
    OldVS.SeqLen = 0;
    OldVS.SeqBody = 0;

    if (flags.replicated) {
	ViceStoreId sid;
	mgrpent *m = 0;
	int asy_resolve = 0;

	/* Acquire an Mgroup. */
	code = vol->GetMgrp(&m, vuid, (PIGGYCOP2 ? &PiggyBS : 0));
	if (code != 0) goto RepExit;

	/* The COP1 call. */
	long cbtemp; cbtemp = cbbreaks;
	vv_t UpdateSet;
	sid = vol->GenerateStoreId();
	{
	    /* Make multiple copies of the IN/OUT and OUT parameters. */
	    int ph_ix; unsigned long ph; ph = m->GetPrimaryHost(&ph_ix);
	    vol->PackVS(m->nhosts, &OldVS);

	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, parent_statusvar, parent_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, target_statusvar, target_status, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, RPC2_Integer, VSvar, VS, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, CallBackStatus, VCBStatusvar, VCBStatus, VSG_MEMBERS);

	    /* Make the RPC call. */
	    CFSOP_PRELUDE("store::Remove %-30s\n", name, target_fso->fid);
	    MULTI_START_MESSAGE(ViceVRemove_OP);
	    code = (int) MRPC_MakeMulti(ViceVRemove_OP, ViceVRemove_PTR,
				  VSG_MEMBERS, m->rocc.handles,
				  m->rocc.retcodes, m->rocc.MIp, 0, 0,
				  &fid, name, parent_statusvar_ptrs,
				  target_statusvar_ptrs, ph, &sid,
				  &OldVS, VSvar_ptrs, VCBStatusvar_ptrs,
				  &PiggyBS);
	    MULTI_END_MESSAGE(ViceVRemove_OP);
	    CFSOP_POSTLUDE("store::remove done\n");

	    free(OldVS.SeqBody);
	    /* Collate responses from individual servers and decide what to do next. */
	    code = vol->Collate_COP1(m, code, &UpdateSet);
	    MULTI_RECORD_STATS(ViceVRemove_OP);
	    if (code == EASYRESOLVE) { asy_resolve = 1; code = 0; }
	    if (code != 0) goto RepExit;

	    /* Collate volume callback information */
	    if (cbtemp == cbbreaks)
		vol->CollateVCB(m, VSvar_bufs, VCBStatusvar_bufs);

	    /* Finalize COP2 Piggybacking. */
	    if (PIGGYCOP2)
		vol->ClearCOP2(&PiggyBS);

	    /* Manually compute the OUT parameters from the mgrpent::Remove() call! -JJK */
	    int dh_ix; dh_ix = -1;
	    (void)m->DHCheck(0, ph_ix, &dh_ix);
	    ARG_UNMARSHALL(parent_statusvar, parent_status, dh_ix);
	    ARG_UNMARSHALL(target_statusvar, target_status, dh_ix);
	}

	/* Do Remove locally. */
	ATOMIC(
	    LocalRemove(Mtime, name, target_fso);
	    UpdateStatus(&parent_status, &UpdateSet, vuid);
	    target_fso->UpdateStatus(&target_status, &UpdateSet, vuid);
       , CMFP)
	if (ASYNCCOP2) ReturnEarly();

	/* Send the COP2 message or add an entry for piggybacking. */
	if (PIGGYCOP2)
	    vol->AddCOP2(&sid, &UpdateSet);
	else
	    (void)vol->COP2(m, &sid, &UpdateSet);

RepExit:
	PutMgrp(&m);
	switch(code) {
	    case 0:
		if (asy_resolve) {
		    vol->ResSubmit(0, &fid);
		    vol->ResSubmit(0, &target_fso->fid);
		}
		break;

	    case ETIMEDOUT:
	    case ESYNRESOLVE:
	    case EINCONS:
		code = ERETRY;
		break;

	    default:
		break;
	}
    }
    else {
	/* Acquire a Connection. */
	connent *c;
	ViceStoreId Dummy;                   /* Need an address for ViceRemove */
	code = vol->GetConn(&c, vuid);
	if (code != 0) goto NonRepExit;

	/* Make the RPC call. */
	CFSOP_PRELUDE("store::Remove %-30s\n", name, target_fso->fid);
	UNI_START_MESSAGE(ViceVRemove_OP);
	code = (int) ViceVRemove(c->connid, &fid, (RPC2_String)name, 
				 &parent_status, &target_status, 0, &Dummy, 
				 &OldVS, &VS, &VCBStatus, &PiggyBS);
	UNI_END_MESSAGE(ViceVRemove_OP);
	CFSOP_POSTLUDE("store::remove done\n");

	/* Examine the return code to decide what to do next. */
	code = vol->Collate(c, code);
	UNI_RECORD_STATS(ViceVRemove_OP);
	if (code != 0) goto NonRepExit;

	/* Do Remove locally. */
	ATOMIC(
	    LocalRemove(Mtime, name, target_fso);
	    UpdateStatus(&parent_status, 0, vuid);
	    target_fso->UpdateStatus(&target_status, 0, vuid);
       , CMFP)

NonRepExit:
	PutConn(&c);
    }

    return(code);
}


/* local-repair modification */
int fsobj::DisconnectedRemove(Date_t Mtime, vuid_t vuid, char *name, fsobj *target_fso, int Tid) {
    FSO_ASSERT(this, (EMULATING(this) || LOGGING(this)));

    int code = 0;

    if (!flags.replicated) {
	code = ETIMEDOUT;
	goto Exit;
    }

    ATOMIC(
	code = vol->LogRemove(Mtime, vuid, &fid, name, &target_fso->fid, 
			      target_fso->stat.LinkCount, Tid);

	if (code == 0)
	    /* This MUST update second-class state! */
	    LocalRemove(Mtime, name, target_fso);
    , DMFP)

Exit:
    return(code);
}


/* local-repair modification */
int fsobj::Remove(char *name, fsobj *target_fso, vuid_t vuid) {
    LOG(10, ("fsobj::Remove: (%s, %s), uid = %d\n",
	      comp, name, vuid));

    int code = 0;
    Date_t Mtime = Vtime();

    int conn, tid;
    GetOperationState(&conn, &tid);
    if (conn == 0) {
	code = DisconnectedRemove(Mtime, vuid, name, target_fso, tid);
    }
    else {
	code = ConnectedRemove(Mtime, vuid, name, target_fso);
    }

    if (code != 0) {
	Demote();
	target_fso->Demote();
    }
    return(code);
}


/*  *****  Link  *****  */

/* MUST be called from within transaction! */
void fsobj::LocalLink(Date_t Mtime, char *name, fsobj *source_fso) {
    /* Update parent status. */
    {
	/* Add the new <name, fid> to the directory. */
	dir_Create(name, &source_fso->fid);

	/* Update the status to reflect the create. */
	RVMLIB_REC_OBJECT(stat);
	stat.DataVersion++;
	stat.Length = dir_Length();
	stat.Date = Mtime;
	if (source_fso->IsDir())
	    stat.LinkCount++;
	DemoteHdbBindings();	    /* in case an expansion would now be satisfied! */
    }

    /* Update source status. */
    {
	RVMLIB_REC_OBJECT(source_fso->stat);
/*    source_fso->stat.DataVersion++;*/
	source_fso->stat.LinkCount++;
    }
}


int fsobj::ConnectedLink(Date_t Mtime, vuid_t vuid, char *name, fsobj *source_fso) {
    FSO_ASSERT(this, HOARDING(this));

    int code = 0;

    /* Status parameters. */
    ViceStatus parent_status;
    VenusToViceStatus(&stat, &parent_status);
    ViceStatus source_status;
    VenusToViceStatus(&source_fso->stat, &source_status);
    {
	/* Temporary!  Until RPC interface is fixed!  -JJK */
	parent_status.Date = Mtime;
    }

    /* COP2 Piggybacking. */
    char PiggyData[COP2SIZE];
    RPC2_CountedBS PiggyBS;
    PiggyBS.SeqLen = 0;
    PiggyBS.SeqBody = (RPC2_ByteSeq)PiggyData;

    /* VCB Arguments */
    RPC2_Integer VS = 0;
    CallBackStatus VCBStatus = NoCallBack;
    RPC2_CountedBS OldVS; 
    OldVS.SeqLen = 0;
    OldVS.SeqBody = 0;

    if (flags.replicated) {
	ViceStoreId sid;
	mgrpent *m = 0;
	int asy_resolve = 0;

	/* Acquire an Mgroup. */
	code = vol->GetMgrp(&m, vuid, (PIGGYCOP2 ? &PiggyBS : 0));
	if (code != 0) goto RepExit;

	/* The COP1 call. */
	long cbtemp; cbtemp = cbbreaks;
	vv_t UpdateSet;
	sid = vol->GenerateStoreId();
	{
	    /* Make multiple copies of the IN/OUT and OUT parameters. */
	    int ph_ix; unsigned long ph; ph = m->GetPrimaryHost(&ph_ix);
 	    vol->PackVS(m->nhosts, &OldVS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, source_statusvar, source_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, parent_statusvar, parent_status, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, RPC2_Integer, VSvar, VS, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, CallBackStatus, VCBStatusvar, VCBStatus, VSG_MEMBERS);

	    /* Make the RPC call. */
	    CFSOP_PRELUDE("store::Link %-30s\n", name, source_fso->fid);
	    MULTI_START_MESSAGE(ViceVLink_OP);
	    code = (int) MRPC_MakeMulti(ViceVLink_OP, ViceVLink_PTR,
				  VSG_MEMBERS, m->rocc.handles,
				  m->rocc.retcodes, m->rocc.MIp, 0, 0,
				  &fid, name, &source_fso->fid,
				  source_statusvar_ptrs, parent_statusvar_ptrs,
				  ph, &sid, &OldVS, VSvar_ptrs, VCBStatusvar_ptrs,
				  &PiggyBS);
	    MULTI_END_MESSAGE(ViceVLink_OP);
	    CFSOP_POSTLUDE("store::link done\n");

	    free(OldVS.SeqBody);
	    /* Collate responses from individual servers and decide what to do next. */
	    code = vol->Collate_COP1(m, code, &UpdateSet);
	    MULTI_RECORD_STATS(ViceVLink_OP);
	    if (code == EASYRESOLVE) { asy_resolve = 1; code = 0; }
	    if (code != 0) goto RepExit;

	    /* Collate volume callback information */
	    if (cbtemp == cbbreaks)
		vol->CollateVCB(m, VSvar_bufs, VCBStatusvar_bufs);

	    /* Finalize COP2 Piggybacking. */
	    if (PIGGYCOP2)
		vol->ClearCOP2(&PiggyBS);

	    /* Manually compute the OUT parameters from the mgrpent::Link() call! -JJK */
	    int dh_ix; dh_ix = -1;
	    (void)m->DHCheck(0, ph_ix, &dh_ix);
	    ARG_UNMARSHALL(source_statusvar, source_status, dh_ix);
	    ARG_UNMARSHALL(parent_statusvar, parent_status, dh_ix);
	}

	/* Do Link locally. */
	ATOMIC(
	    LocalLink(Mtime, name, source_fso);
	    UpdateStatus(&parent_status, &UpdateSet, vuid);
	    source_fso->UpdateStatus(&source_status, &UpdateSet, vuid);
	, CMFP)
	if (ASYNCCOP2) ReturnEarly();

	/* Send the COP2 message or add an entry for piggybacking. */
	if (PIGGYCOP2)
	    vol->AddCOP2(&sid, &UpdateSet);
	else
	    (void)vol->COP2(m, &sid, &UpdateSet);

RepExit:
	PutMgrp(&m);
	switch(code) {
	    case 0:
		if (asy_resolve) {
		    vol->ResSubmit(0, &fid);
		    vol->ResSubmit(0, &source_fso->fid);
		}
		break;

	    case ETIMEDOUT:
	    case ESYNRESOLVE:
	    case EINCONS:
		code = ERETRY;
		break;

	    default:
		break;
	}
    }
    else {
	/* Acquire a Connection. */
	connent *c;
	ViceStoreId Dummy;                   /* Need an address for ViceLink */
	code = vol->GetConn(&c, vuid);
	if (code != 0) goto NonRepExit;

	/* Make the RPC call. */
	CFSOP_PRELUDE("store::Link %-30s\n", name, source_fso->fid);
	UNI_START_MESSAGE(ViceVLink_OP);
	code = (int) ViceVLink(c->connid, &fid, (RPC2_String)name,
			      &source_fso->fid, &source_status,
			      &parent_status, 0, &Dummy, 
			      &OldVS, &VS, &VCBStatus, &PiggyBS);
	UNI_END_MESSAGE(ViceVLink_OP);
	CFSOP_POSTLUDE("store::link done\n");

	/* Examine the return code to decide what to do next. */
	code = vol->Collate(c, code);
	UNI_RECORD_STATS(ViceVLink_OP);
	if (code != 0) goto NonRepExit;

	/* Do Link locally. */
	ATOMIC(
	    LocalLink(Mtime, name, source_fso);
	    UpdateStatus(&parent_status, 0, vuid);
	    source_fso->UpdateStatus(&source_status, 0, vuid);
	, CMFP)

NonRepExit:
	PutConn(&c);
    }

    return(code);
}


/* local-repair modification */
int fsobj::DisconnectedLink(Date_t Mtime, vuid_t vuid, char *name, fsobj *source_fso, int Tid) {
    FSO_ASSERT(this, (EMULATING(this) || LOGGING(this)));

    int code = 0;

    if (!flags.replicated) {
	code = ETIMEDOUT;
	goto Exit;
    }

    ATOMIC(
	code = vol->LogLink(Mtime, vuid, &fid, name, &source_fso->fid, Tid);

	if (code == 0)
	    /* This MUST update second-class state! */
	    LocalLink(Mtime, name, source_fso);
    , DMFP)

Exit:
    return(code);
}


/* local-repair modification */
int fsobj::Link(char *name, fsobj *source_fso, vuid_t vuid) {
    LOG(10, ("fsobj::Link: (%s/%s, %s), uid = %d\n",
	      comp, source_fso->comp, name, vuid));

    int code = 0;
    Date_t Mtime = Vtime();

    int conn, tid;
    GetOperationState(&conn, &tid);
    if (conn == 0) {
	code = DisconnectedLink(Mtime, vuid, name, source_fso, tid);
    }
    else {
	code = ConnectedLink(Mtime, vuid, name, source_fso);
    }

    if (code != 0) {
	Demote();
	source_fso->Demote();
    }
    return(code);
}


/*  *****  Rename  *****  */

/* MUST be called from within transaction! */
void fsobj::LocalRename(Date_t Mtime, fsobj *s_parent_fso, char *s_name,
			 fsobj *s_fso, char *t_name, fsobj *t_fso) {
    int SameParent = (s_parent_fso == this);
    int TargetExists = (t_fso != 0);

    /* Update local status. */
    {
	RVMLIB_REC_OBJECT(stat);
	if (!SameParent)
	    RVMLIB_REC_OBJECT(s_parent_fso->stat);
	RVMLIB_REC_OBJECT(s_fso->stat);
	if (TargetExists)
	    RVMLIB_REC_OBJECT(t_fso->stat);

	/*Remove the source <name, fid> from its directory. */
	s_parent_fso->dir_Delete(s_name);

	/* Remove the target <name, fid> from its directory (if it exists). */
	if (TargetExists) {
	    dir_Delete(t_name);

	    t_fso->DetachHdbBindings();
	    if (t_fso->IsDir()) {
		stat.LinkCount--;

		/* Delete the target object. */
		UpdateCacheStats(&FSDB->DirAttrStats, REMOVE,
				 NBLOCKS(sizeof(fsobj)));
		UpdateCacheStats(&FSDB->DirDataStats, REMOVE,
				 BLOCKS(t_fso));
		t_fso->Kill();
	    }
	    else {
		/* Update the target status. */
		t_fso->stat.LinkCount--;
		if (t_fso->stat.LinkCount == 0) {
		    UpdateCacheStats(&FSDB->FileAttrStats, REMOVE,
				     NBLOCKS(sizeof(fsobj)));
		    UpdateCacheStats(&FSDB->FileDataStats, REMOVE,
				     BLOCKS(t_fso));
		    t_fso->Kill();
		}
		else {
		    t_fso->stat.DataVersion++;
		}
	    }
	}

	/* Create the target <name, fid> in the target directory. */
	dir_Create(t_name, &s_fso->fid);

	/* Alter ".." entry in source if necessary. */
	if (!SameParent && s_fso->IsDir()) {
	    s_fso->dir_Delete("..");
	    s_parent_fso->stat.LinkCount--;
	    s_fso->dir_Create("..", &fid);
	    stat.LinkCount++;
	}

	/* Update parents' status to reflect the create(s) and delete(s). */
	stat.DataVersion++;
	stat.Length = dir_Length();
	stat.Date = Mtime;
	if (SameParent) {
	    DemoteHdbBindings();	    /* in case an expansion would now be satisfied! */
	}
	else {
	    s_parent_fso->stat.DataVersion++;
	    s_parent_fso->stat.Length = s_parent_fso->dir_Length();
	    s_parent_fso->stat.Date = Mtime;
	}

	/* Update the source status to reflect the rename and possible create/delete. */
	if (!STREQ(s_fso->comp, t_name)) {
	    RVMLIB_REC_OBJECT(s_fso->comp);
	    RVMLIB_REC_FREE(s_fso->comp);
	    int len = (int) strlen(t_name) + 1;
	    s_fso->comp = (char *)RVMLIB_REC_MALLOC(len);
	    RVMLIB_SET_RANGE(s_fso->comp, len);
	    strcpy(s_fso->comp, t_name);
	}
	s_fso->DetachHdbBindings();
/*    s_fso->stat.DataVersion++;*/
	if (!SameParent)
	    s_fso->SetParent(fid.Vnode, fid.Unique);
    }
}


int fsobj::ConnectedRename(Date_t Mtime, vuid_t vuid, fsobj *s_parent_fso,
			    char *s_name, fsobj *s_fso, char *t_name, fsobj *t_fso) {
    FSO_ASSERT(this, HOARDING(this));

    int code = 0;
    int SameParent = (s_parent_fso == this);
    int TargetExists = (t_fso != 0);

    /* Status parameters. */
    ViceStatus t_parent_status;
    VenusToViceStatus(&stat, &t_parent_status);
    ViceStatus s_parent_status;
    VenusToViceStatus(&s_parent_fso->stat, &s_parent_status);
    ViceStatus source_status;
    VenusToViceStatus(&s_fso->stat, &source_status);
    ViceStatus target_status;
    if (TargetExists) VenusToViceStatus(&t_fso->stat, &target_status);
    {
	/* Temporary!  Until RPC interface is fixed!  -JJK */
	s_parent_status.Date = Mtime;
    }

    /* COP2 Piggybacking. */
    char PiggyData[COP2SIZE];
    RPC2_CountedBS PiggyBS;
    PiggyBS.SeqLen = 0;
    PiggyBS.SeqBody = (RPC2_ByteSeq)PiggyData;

    /* VCB Arguments */
    RPC2_Integer VS = 0;
    CallBackStatus VCBStatus = NoCallBack;
    RPC2_CountedBS OldVS; 
    OldVS.SeqLen = 0;
    OldVS.SeqBody = 0;

    if (flags.replicated) {
	ViceStoreId sid;
	mgrpent *m = 0;
	int asy_resolve = 0;

	/* Acquire an Mgroup. */
	code = vol->GetMgrp(&m, vuid, (PIGGYCOP2 ? &PiggyBS : 0));
	if (code != 0) goto RepExit;

	/* The COP1 call. */
	long cbtemp; cbtemp = cbbreaks;
	vv_t UpdateSet;
	sid = vol->GenerateStoreId();
	{
	    /* Make multiple copies of the IN/OUT and OUT parameters. */
	    int ph_ix; unsigned long ph; ph = m->GetPrimaryHost(&ph_ix);
 	    vol->PackVS(m->nhosts, &OldVS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, t_parent_statusvar, t_parent_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, s_parent_statusvar, s_parent_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, source_statusvar, source_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, target_statusvar, target_status, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, RPC2_Integer, VSvar, VS, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, CallBackStatus, VCBStatusvar, VCBStatus, VSG_MEMBERS);

	    /* Make the RPC call. */
	    CFSOP_PRELUDE("store::Rename %-30s\n", s_name, s_fso->fid);
	    MULTI_START_MESSAGE(ViceVRename_OP);
	    code = (int) MRPC_MakeMulti(ViceVRename_OP, ViceVRename_PTR,
				  VSG_MEMBERS, m->rocc.handles,
				  m->rocc.retcodes, m->rocc.MIp, 0, 0,
				  &s_parent_fso->fid, s_name, &fid, t_name,
				  s_parent_statusvar_ptrs, t_parent_statusvar_ptrs,
				  source_statusvar_ptrs, target_statusvar_ptrs,
				  ph, &sid, &OldVS, VSvar_ptrs, VCBStatusvar_ptrs,
				  &PiggyBS);
	    MULTI_END_MESSAGE(ViceVRename_OP);
	    CFSOP_POSTLUDE("store::rename done\n");

	    free(OldVS.SeqBody);
	    /* Collate responses from individual servers and decide what to do next. */
	    code = vol->Collate_COP1(m, code, &UpdateSet);
	    MULTI_RECORD_STATS(ViceVRename_OP);
	    if (code == EASYRESOLVE) { asy_resolve = 1; code = 0; }
	    if (code != 0) goto RepExit;

	    /* Collate volume callback information */
	    if (cbtemp == cbbreaks)
		vol->CollateVCB(m, VSvar_bufs, VCBStatusvar_bufs);

	    /* Finalize COP2 Piggybacking. */
	    if (PIGGYCOP2)
		vol->ClearCOP2(&PiggyBS);

	    /* Manually compute the OUT parameters from the mgrpent::Rename() call! -JJK */
	    int dh_ix; dh_ix = -1;
	    (void)m->DHCheck(0, ph_ix, &dh_ix);
	    ARG_UNMARSHALL(t_parent_statusvar, t_parent_status, dh_ix);
	    ARG_UNMARSHALL(s_parent_statusvar, s_parent_status, dh_ix);
	    ARG_UNMARSHALL(source_statusvar, source_status, dh_ix);
	    ARG_UNMARSHALL(target_statusvar, target_status, dh_ix);
	}

	/* Do Rename locally. */
	ATOMIC(
	    LocalRename(Mtime, s_parent_fso, s_name, s_fso, t_name, t_fso);
	    UpdateStatus(&t_parent_status, &UpdateSet, vuid);
	    if (!SameParent)
		s_parent_fso->UpdateStatus(&s_parent_status, &UpdateSet, vuid);
	    s_fso->UpdateStatus(&source_status, &UpdateSet, vuid);
	    if (TargetExists)
		t_fso->UpdateStatus(&target_status, &UpdateSet, vuid);
	, CMFP)
	if (ASYNCCOP2) ReturnEarly();

	/* Send the COP2 message or add an entry for piggybacking. */
	if (PIGGYCOP2)
	    vol->AddCOP2(&sid, &UpdateSet);
	else
	    (void)vol->COP2(m, &sid, &UpdateSet);

RepExit:
	PutMgrp(&m);
	switch(code) {
	    case 0:
		if (asy_resolve) {
		    vol->ResSubmit(0, &fid);
		    if (!SameParent)
			vol->ResSubmit(0, &s_parent_fso->fid);
		    vol->ResSubmit(0, &s_fso->fid);
		    if (TargetExists)
			vol->ResSubmit(0, &t_fso->fid);
		}
		break;

	    case ETIMEDOUT:
	    case ESYNRESOLVE:
	    case EINCONS:
		code = ERETRY;
		break;

	    default:
		break;
	}
    }
    else {
	/* Acquire a Connection. */
	connent *c;
	ViceStoreId Dummy;                   /* Need an address for ViceRename */
	code = vol->GetConn(&c, vuid);
	if (code != 0) goto NonRepExit;

	/* Make the RPC call. */
	CFSOP_PRELUDE("store::Rename %-30s\n", s_name, s_fso->fid);
	UNI_START_MESSAGE(ViceVRename_OP);
	code = (int) ViceVRename(c->connid, &s_parent_fso->fid, 
				(RPC2_String)s_name, &fid, (RPC2_String)t_name,
				&s_parent_status, &t_parent_status,
				&source_status, &target_status, 0, &Dummy,
				&OldVS, &VS, &VCBStatus, &PiggyBS);
	UNI_END_MESSAGE(ViceVRename_OP);
	CFSOP_POSTLUDE("store::rename done\n");

	/* Examine the return code to decide what to do next. */
	code = vol->Collate(c, code);
	UNI_RECORD_STATS(ViceVRename_OP);
	if (code != 0) goto NonRepExit;

	/* Release the Connection. */
	PutConn(&c);

	/* Do Rename locally. */
	ATOMIC(
	    LocalRename(Mtime, s_parent_fso, s_name, s_fso, t_name, t_fso);
	    UpdateStatus(&t_parent_status, 0, vuid);
	    if (!SameParent)
		s_parent_fso->UpdateStatus(&s_parent_status, 0, vuid);
	    s_fso->UpdateStatus(&source_status, 0, vuid);
	    if (TargetExists)
		t_fso->UpdateStatus(&target_status, 0, vuid);
	, CMFP)

NonRepExit:
	PutConn(&c);
    }

    return(code);
}


/* local-repair modification */
int fsobj::DisconnectedRename(Date_t Mtime, vuid_t vuid, fsobj *s_parent_fso, char *s_name, 
			      fsobj *s_fso, char *t_name, fsobj *t_fso, int Tid) {
    FSO_ASSERT(this, (EMULATING(this) || LOGGING(this)));

    int code = 0;
    int SameParent = (s_parent_fso == this);
    int TargetExists = (t_fso != 0);

    if (!flags.replicated) {
	code = ETIMEDOUT;
	goto Exit;
    }

    ATOMIC(
	code = vol->LogRename(Mtime, vuid, &s_parent_fso->fid, s_name,
			      &fid, t_name, &s_fso->fid,
			      (TargetExists ? &t_fso->fid : &NullFid),
			      (TargetExists ? t_fso->stat.LinkCount : 0), Tid);

	if (code == 0)
	    /* This MUST update second-class state! */
	    LocalRename(Mtime, s_parent_fso, s_name, s_fso, t_name, t_fso);
    , DMFP)

Exit:
    return(code);
}


/* local-repair modification */
int fsobj::Rename(fsobj *s_parent_fso, char *s_name, fsobj *s_fso,
		   char *t_name, fsobj *t_fso, vuid_t vuid) {
    LOG(10, ("fsobj::Rename : (%s/%s, %s/%s), uid = %d\n",
	      (s_parent_fso ? s_parent_fso->comp : comp),
	      s_name, comp, t_name, vuid));

    int code = 0;
    Date_t Mtime = Vtime();
    int SameParent = (s_parent_fso == 0);
    if (SameParent) s_parent_fso = this;
    int TargetExists = (t_fso != 0);

    int conn, tid;
    GetOperationState(&conn, &tid);
    if (conn == 0) {
	code = DisconnectedRename(Mtime, vuid, s_parent_fso, s_name, s_fso, 
				  t_name, t_fso, tid);
    }
    else {
	code = ConnectedRename(Mtime, vuid, s_parent_fso,
			       s_name, s_fso, t_name, t_fso);
    }

    if (code != 0) {
	Demote();
	if (!SameParent) s_parent_fso->Demote();
	s_fso->Demote();
	if (TargetExists) t_fso->Demote();
    }
    return(code);
}


/*  *****  Mkdir  *****  */

/* MUST be called from within transaction! */
void fsobj::LocalMkdir(Date_t Mtime, fsobj *target_fso, char *name,
			vuid_t Owner, unsigned short Mode) {
    /* Update parent status. */
    {
	/* Add the new <name, fid> to the directory. */
	dir_Create(name, &target_fso->fid);

	/* Update the status to reflect the create. */
	RVMLIB_REC_OBJECT(stat);
	stat.DataVersion++;
	stat.Length = dir_Length();
	stat.Date = Mtime;
	stat.LinkCount++;
    }

    /* Set target status and data. */
    {
	/* What about ACL? -JJK */
	RVMLIB_REC_OBJECT(*target_fso);
	target_fso->stat.VnodeType = Directory;
	target_fso->stat.LinkCount = 2;
	target_fso->stat.Length = 0;
	target_fso->stat.DataVersion = 1;
	target_fso->stat.Date = Mtime;
	target_fso->stat.Owner = Owner;
	target_fso->stat.Mode = Mode;
	target_fso->AnyUser = AnyUser;
	bcopy((const void *)SpecificUser, (void *)target_fso->SpecificUser, (int)(CPSIZE * sizeof(AcRights)));
	target_fso->flags.created = 1;
	target_fso->Matriculate();
	target_fso->SetParent(fid.Vnode, fid.Unique);

	/* Create the target directory. */
	target_fso->dir_MakeDir();
	target_fso->stat.Length = target_fso->dir_Length();
	UpdateCacheStats(&FSDB->DirDataStats, CREATE,
			 BLOCKS(target_fso));

	target_fso->Reference();
	target_fso->ComputePriority();
    }
}


int fsobj::ConnectedMkdir(Date_t Mtime, vuid_t vuid, fsobj **t_fso_addr,
			   char *name, unsigned short Mode, int target_pri) {
    FSO_ASSERT(this, HOARDING(this));

    int code = 0;
    fsobj *target_fso = 0;
    ViceFid target_fid;
    RPC2_Unsigned AllocHost = 0;

    /* Status parameters. */
    ViceStatus parent_status;
    VenusToViceStatus(&stat, &parent_status);
    ViceStatus target_status;
    target_status.Mode = Mode;
    {
	/* Temporary!  Until RPC interface is fixed!  -JJK */
	parent_status.Date = Mtime;
	target_status.DataVersion = 1;
	target_status.VV = NullVV;
    }

    /* COP2 Piggybacking. */
    char PiggyData[COP2SIZE];
    RPC2_CountedBS PiggyBS;
    PiggyBS.SeqLen = 0;
    PiggyBS.SeqBody = (RPC2_ByteSeq)PiggyData;

    /* VCB Arguments */
    RPC2_Integer VS = 0;
    CallBackStatus VCBStatus = NoCallBack;
    RPC2_CountedBS OldVS; 
    OldVS.SeqLen = 0;
    OldVS.SeqBody = 0;

    if (flags.replicated) {
	ViceStoreId sid;
	mgrpent *m = 0;
	int asy_resolve = 0;

	/* Allocate a fid for the new object. */
	code = vol->AllocFid(Directory, &target_fid, &AllocHost, vuid);
	if (code != 0) goto RepExit;

	/* Allocate the fsobj. */
	target_fso = FSDB->Create(&target_fid, WR, target_pri, name);
	if (target_fso == 0) {
	    UpdateCacheStats(&FSDB->DirAttrStats, NOSPACE,
			     NBLOCKS(sizeof(fsobj)));
	    code = ENOSPC;
	    goto RepExit;
	}
	UpdateCacheStats(&FSDB->DirAttrStats, CREATE,
			 NBLOCKS(sizeof(fsobj)));

	/* Acquire an Mgroup. */
	code = vol->GetMgrp(&m, vuid, (PIGGYCOP2 ? &PiggyBS : 0));
	if (code != 0) goto RepExit;

	/* The COP1 call. */
	long cbtemp; cbtemp = cbbreaks;
	vv_t UpdateSet;
	sid = vol->GenerateStoreId();
	{
	    /* Make multiple copies of the IN/OUT and OUT parameters. */
 	    vol->PackVS(m->nhosts, &OldVS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, target_statusvar, target_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceFid, target_fidvar, target_fid, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, parent_statusvar, parent_status, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, RPC2_Integer, VSvar, VS, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, CallBackStatus, VCBStatusvar, VCBStatus, VSG_MEMBERS);

	    /* Make the RPC call. */
	    CFSOP_PRELUDE("store::Mkdir %-30s\n", name, NullFid);
	    MULTI_START_MESSAGE(ViceVMakeDir_OP);
	    code = (int) MRPC_MakeMulti(ViceVMakeDir_OP, ViceVMakeDir_PTR,
					VSG_MEMBERS, m->rocc.handles,
					m->rocc.retcodes, m->rocc.MIp, 0, 0,
					&fid, name, target_statusvar_ptrs,
					target_fidvar_ptrs, parent_statusvar_ptrs,
					AllocHost, &sid, &OldVS, VSvar_ptrs, 
					VCBStatusvar_ptrs, &PiggyBS);
	    MULTI_END_MESSAGE(ViceVMakeDir_OP);
	    CFSOP_POSTLUDE("store::mkdir done\n");

	    free(OldVS.SeqBody);
	    /* Collate responses from individual servers and decide what to do next. */
	    code = vol->Collate_COP1(m, code, &UpdateSet);
	    MULTI_RECORD_STATS(ViceVMakeDir_OP);
	    if (code == EASYRESOLVE) { asy_resolve = 1; code = 0; }
	    if (code != 0) goto RepExit;

	    /* Collate volume callback information */
	    if (cbtemp == cbbreaks)
		vol->CollateVCB(m, VSvar_bufs, VCBStatusvar_bufs);

	    /* Finalize COP2 Piggybacking. */
	    if (PIGGYCOP2)
		vol->ClearCOP2(&PiggyBS);

	    /* Manually compute the OUT parameters from the mgrpent::Mkdir() call! -JJK */
	    int dh_ix; dh_ix = -1;
	    (void)m->DHCheck(0, -1, &dh_ix);
	    ARG_UNMARSHALL(target_statusvar, target_status, dh_ix);
	    ARG_UNMARSHALL(target_fidvar, target_fid, dh_ix);
	    ARG_UNMARSHALL(parent_statusvar, parent_status, dh_ix);
	}

	/* Do Mkdir locally. */
	ATOMIC(
	    LocalMkdir(Mtime, target_fso, name, vuid, Mode);
	    UpdateStatus(&parent_status, &UpdateSet, vuid);
	    target_fso->UpdateStatus(&target_status, &UpdateSet, vuid);
	, CMFP)
	if (target_fso->flags.usecallback &&
	    target_status.CallBack == CallBackSet &&
	    cbtemp == cbbreaks)
	    target_fso->SetRcRights(RC_STATUS | RC_DATA);
	if (ASYNCCOP2) target_fso->ReturnEarly();

	/* Send the COP2 message or add an entry for piggybacking. */
	if (PIGGYCOP2)
	    vol->AddCOP2(&sid, &UpdateSet);
	else
	    (void)vol->COP2(m, &sid, &UpdateSet);

RepExit:
	PutMgrp(&m);
	switch(code) {
	    case 0:
		if (asy_resolve) {
		    vol->ResSubmit(0, &fid);
		    if (target_fso != 0)
			vol->ResSubmit(0, &target_fso->fid);
		}
		break;

	    case ETIMEDOUT:
	    case ESYNRESOLVE:
	    case EINCONS:
		code = ERETRY;
		break;

	    default:
		break;
	}
    }
    else {
	/* Acquire a Connection. */
	connent *c;
	ViceStoreId Dummy;                   /* Need an address for ViceMakeDir */
	code = vol->GetConn(&c, vuid);
	if (code != 0) goto NonRepExit;

	/* Make the RPC call. */
	long cbtemp; cbtemp = cbbreaks;
	CFSOP_PRELUDE("store::Mkdir %-30s\n", name, NullFid);
	UNI_START_MESSAGE(ViceVMakeDir_OP);
	code = (int) ViceVMakeDir(c->connid, &fid, (RPC2_String) name,
				  &target_status, &target_fid,
				  &parent_status, 0, &Dummy, 
				  &OldVS, &VS, &VCBStatus, &PiggyBS);
	UNI_END_MESSAGE(ViceVMakeDir_OP);
	CFSOP_POSTLUDE("store::mkdir done\n");

	/* Examine the return code to decide what to do next. */
	code = vol->Collate(c, code);
	UNI_RECORD_STATS(ViceVMakeDir_OP);
	if (code != 0) goto NonRepExit;

	/* Allocate the fsobj. */
	target_fso = FSDB->Create(&target_fid, WR, target_pri, name);
	if (target_fso == 0) {
	    UpdateCacheStats(&FSDB->DirAttrStats, NOSPACE,
			     NBLOCKS(sizeof(fsobj)));
	    code = ENOSPC;
	    goto NonRepExit;
	}
	UpdateCacheStats(&FSDB->DirAttrStats, CREATE,
			 NBLOCKS(sizeof(fsobj)));

	/* Do Mkdir locally. */
	ATOMIC(
	    LocalMkdir(Mtime, target_fso, name, vuid, Mode);
	    UpdateStatus(&parent_status, 0, vuid);
	    target_fso->UpdateStatus(&target_status, 0, vuid);
	, CMFP)
	if (target_fso->flags.usecallback &&
	    target_status.CallBack == CallBackSet &&
	    cbtemp == cbbreaks)
	    target_fso->SetRcRights(RC_STATUS | RC_DATA);

NonRepExit:
	PutConn(&c);
    }

    if (code == 0) {
	*t_fso_addr = target_fso;
    }
    else {
	if (target_fso != 0) {
	    FSO_ASSERT(target_fso, !HAVESTATUS(target_fso));
	    ATOMIC(
		target_fso->Kill();
	    , DMFP);
	    FSDB->Put(&target_fso);
	}
    }
    return(code);
}


/* local-repair modification */
int fsobj::DisconnectedMkdir(Date_t Mtime, vuid_t vuid, fsobj **t_fso_addr, char *name, 
			     unsigned short Mode, int target_pri, int Tid) {
    FSO_ASSERT(this, (EMULATING(this) || LOGGING(this)));

    int code = 0;
    fsobj *target_fso = 0;
    ViceFid target_fid;
    RPC2_Unsigned AllocHost = 0;

    if (!flags.replicated) {
	code = ETIMEDOUT;
	goto Exit;
    }

    /* Allocate a fid for the new object. */
    /* if we time out, return so we will try again with a local fid. */
    code = vol->AllocFid(Directory, &target_fid, &AllocHost, vuid);
    if (code != 0) goto Exit;

    /* Allocate the fsobj. */
    target_fso = FSDB->Create(&target_fid, WR, target_pri, name);
    if (target_fso == 0) {
	UpdateCacheStats(&FSDB->DirAttrStats, NOSPACE,
			 NBLOCKS(sizeof(fsobj)));
	code = ENOSPC;
	goto Exit;
    }
    UpdateCacheStats(&FSDB->DirAttrStats, CREATE,
		      NBLOCKS(sizeof(fsobj)));

    ATOMIC(
	code = vol->LogMkdir(Mtime, vuid, &fid, name, &target_fso->fid, Mode, Tid);

	   if (code == 0) {
	    /* This MUST update second-class state! */
	    LocalMkdir(Mtime, target_fso, name, vuid, Mode);

	    /* target_fso->stat is not initialized until LocalMkdir */
	    RVMLIB_REC_OBJECT(target_fso->CleanStat);
	    target_fso->CleanStat.Length = target_fso->stat.Length;
	    target_fso->CleanStat.Date = target_fso->stat.Date;
	   }
    , DMFP)

Exit:
    if (code == 0) {
	*t_fso_addr = target_fso;
    }
    else {
	if (target_fso != 0) {
	    FSO_ASSERT(target_fso, !HAVESTATUS(target_fso));
	    ATOMIC(
		target_fso->Kill();
	    , DMFP);
	    FSDB->Put(&target_fso);
	}
    }
    return(code);
}


/* local-repair modification */
/* Returns target object write-locked (on success). */
int fsobj::Mkdir(char *name, fsobj **target_fso_addr,
		  vuid_t vuid, unsigned short Mode, int target_pri) {
    LOG(10, ("fsobj::Mkdir: (%s, %s, %d), uid = %d\n",
	      comp, name, target_pri, vuid));

    int code = 0;
    Date_t Mtime = Vtime();
    *target_fso_addr = 0;

    int conn, tid;
    GetOperationState(&conn, &tid);
    if (conn == 0) {
	code = DisconnectedMkdir(Mtime, vuid, target_fso_addr,
				 name, Mode, target_pri, tid);
    }
    else {
	code = ConnectedMkdir(Mtime, vuid, target_fso_addr,
			      name, Mode, target_pri);
    }

    if (code != 0) {
	Demote();
    }
    return(code);
}


/*  *****  Rmdir  *****  */

/* MUST be called from within transaction! */
void fsobj::LocalRmdir(Date_t Mtime, char *name, fsobj *target_fso) {
    /* Update parent status. */
    {
	/* Delete the target name from the directory.. */
	dir_Delete(name);

	/* Update the status to reflect the delete. */
	RVMLIB_REC_OBJECT(stat);
	stat.DataVersion++;
	stat.Length = dir_Length();
	stat.Date = Mtime;
	stat.LinkCount--;
    }

    /* Update target status. */
    {
	/* Delete the target object. */
	RVMLIB_REC_OBJECT(target_fso->stat);
	target_fso->stat.LinkCount--;
	target_fso->DetachHdbBindings();
	UpdateCacheStats(&FSDB->DirAttrStats, REMOVE,
			 NBLOCKS(sizeof(fsobj)));
	UpdateCacheStats(&FSDB->DirDataStats, REMOVE,
			 BLOCKS(target_fso));
	target_fso->Kill();
    }
}


int fsobj::ConnectedRmdir(Date_t Mtime, vuid_t vuid, char *name, fsobj *target_fso) {
    FSO_ASSERT(this, HOARDING(this));

    int code = 0;

    /* Status parameters. */
    ViceStatus parent_status;
    VenusToViceStatus(&stat, &parent_status);
    ViceStatus target_status;
    VenusToViceStatus(&target_fso->stat, &target_status);
    {
	/* Temporary!  Until RPC interface is fixed!  -JJK */
	parent_status.Date = Mtime;
    }

    /* COP2 Piggybacking. */
    char PiggyData[COP2SIZE];
    RPC2_CountedBS PiggyBS;
    PiggyBS.SeqLen = 0;
    PiggyBS.SeqBody = (RPC2_ByteSeq)PiggyData;

    /* VCB Arguments */
    RPC2_Integer VS = 0;
    CallBackStatus VCBStatus = NoCallBack;
    RPC2_CountedBS OldVS; 
    OldVS.SeqLen = 0;
    OldVS.SeqBody = 0;

    if (flags.replicated) {
	ViceStoreId sid;
	mgrpent *m = 0;
	int asy_resolve = 0;

	/* Acquire an Mgroup. */
	code = vol->GetMgrp(&m, vuid, (PIGGYCOP2 ? &PiggyBS : 0));
	if (code != 0) goto RepExit;

	/* The COP1 call. */
	long cbtemp; cbtemp = cbbreaks;
	vv_t UpdateSet;
	sid = vol->GenerateStoreId();
	{
	    /* Make multiple copies of the IN/OUT and OUT parameters. */
	    int ph_ix; unsigned long ph; ph = m->GetPrimaryHost(&ph_ix);
 	    vol->PackVS(m->nhosts, &OldVS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, parent_statusvar, parent_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, target_statusvar, target_status, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, RPC2_Integer, VSvar, VS, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, CallBackStatus, VCBStatusvar, VCBStatus, VSG_MEMBERS);

	    /* Make the RPC call. */
	    CFSOP_PRELUDE("store::Rmdir %-30s\n", name, target_fso->fid);
	    MULTI_START_MESSAGE(ViceVRemoveDir_OP);
	    code = (int) MRPC_MakeMulti(ViceVRemoveDir_OP, ViceVRemoveDir_PTR,
				  VSG_MEMBERS, m->rocc.handles,
				  m->rocc.retcodes, m->rocc.MIp, 0, 0,
				  &fid, name, parent_statusvar_ptrs,
				  target_statusvar_ptrs, ph, &sid, 
				  &OldVS, VSvar_ptrs, VCBStatusvar_ptrs, &PiggyBS);
	    MULTI_END_MESSAGE(ViceVRemoveDir_OP);
	    CFSOP_POSTLUDE("store::rmdir done\n");

	    free(OldVS.SeqBody);
	    /* Collate responses from individual servers and decide what to do next. */
	    code = vol->Collate_COP1(m, code, &UpdateSet);
	    MULTI_RECORD_STATS(ViceVRemoveDir_OP);
	    if (code == EASYRESOLVE) { asy_resolve = 1; code = 0; }
	    if (code != 0) goto RepExit;

	    /* Collate volume callback information */
	    if (cbtemp == cbbreaks)
		vol->CollateVCB(m, VSvar_bufs, VCBStatusvar_bufs);

	    /* Finalize COP2 Piggybacking. */
	    if (PIGGYCOP2)
		vol->ClearCOP2(&PiggyBS);

	    /* Manually compute the OUT parameters from the mgrpent::Rmdir() call! -JJK */
	    int dh_ix; dh_ix = -1;
	    (void)m->DHCheck(0, ph_ix, &dh_ix);
	    ARG_UNMARSHALL(parent_statusvar, parent_status, dh_ix);
	    ARG_UNMARSHALL(target_statusvar, target_status, dh_ix);
	}

	/* Do Rmdir locally. */
	ATOMIC(
	    LocalRmdir(Mtime, name, target_fso);
	    UpdateStatus(&parent_status, &UpdateSet, vuid);
	    target_fso->UpdateStatus(&target_status, &UpdateSet, vuid);
	, CMFP)
	if (ASYNCCOP2) ReturnEarly();

	/* Send the COP2 message or add an entry for piggybacking. */
	if (PIGGYCOP2)
	    vol->AddCOP2(&sid, &UpdateSet);
	else
	    (void)vol->COP2(m, &sid, &UpdateSet);

RepExit:
	PutMgrp(&m);
	switch(code) {
	    case 0:
		if (asy_resolve) {
		    vol->ResSubmit(0, &fid);
		    vol->ResSubmit(0, &target_fso->fid);
		}
		break;

	    case ETIMEDOUT:
	    case ESYNRESOLVE:
	    case EINCONS:
		code = ERETRY;
		break;

	    default:
		break;
	}
    }
    else {
	/* Acquire a Connection. */
	connent *c;
	ViceStoreId Dummy;                   /* Need an address for ViceRemoveDir */
	code = vol->GetConn(&c, vuid);
	if (code != 0) goto NonRepExit;

	/* Make the RPC call. */
	CFSOP_PRELUDE("store::Rmdir %-30s\n", name, target_fso->fid);
	UNI_START_MESSAGE(ViceVRemoveDir_OP);
	code = (int) ViceVRemoveDir(c->connid, &fid, (RPC2_String)name,
				   &parent_status, &target_status, 0, &Dummy,
				   &OldVS, &VS, &VCBStatus, &PiggyBS);
	UNI_END_MESSAGE(ViceVRemoveDir_OP);
	CFSOP_POSTLUDE("store::rmdir done\n");

	/* Examine the return code to decide what to do next. */
	code = vol->Collate(c, code);
	UNI_RECORD_STATS(ViceVRemoveDir_OP);
	if (code != 0) goto NonRepExit;

	/* Do Rmdir locally. */
	ATOMIC(
	    LocalRmdir(Mtime, name, target_fso);
	    UpdateStatus(&parent_status, 0, vuid);
	    target_fso->UpdateStatus(&target_status, 0, vuid);
	, CMFP)

NonRepExit:
	PutConn(&c);
    }

    return(code);
}


/* local-repair modification */
int fsobj::DisconnectedRmdir(Date_t Mtime, vuid_t vuid, char *name, fsobj *target_fso, int Tid) {
    FSO_ASSERT(this, (EMULATING(this) || LOGGING(this)));

    int code = 0;

    if (!flags.replicated) {
	code = ETIMEDOUT;
	goto Exit;
    }

    ATOMIC(
	code = vol->LogRmdir(Mtime, vuid, &fid, name, &target_fso->fid, Tid);

	if (code == 0)
	    /* This MUST update second-class state! */
	    LocalRmdir(Mtime, name, target_fso);
    , DMFP)

Exit:
    return(code);
}


/* local-repair modification */
int fsobj::Rmdir(char *name, fsobj *target_fso, vuid_t vuid) {
    LOG(10, ("fsobj::Rmdir: (%s, %s), uid = %d\n",
	      comp, name, vuid));

    int code = 0;
    Date_t Mtime = Vtime();

    int conn, tid;
    GetOperationState(&conn, &tid);
    if (conn == 0) {
	code = DisconnectedRmdir(Mtime, vuid, name, target_fso, tid);
    }
    else {
	code = ConnectedRmdir(Mtime, vuid, name, target_fso);
    }

    if (code != 0) {
	Demote();
	target_fso->Demote();
    }
    return(code);
}


/*  *****  Symlink  *****  */

/* MUST be called from within transaction! */
void fsobj::LocalSymlink(Date_t Mtime, fsobj *target_fso, char *name,
			  char *contents, vuid_t Owner, unsigned short Mode) {
    /* Update parent status. */
    {
	/* Add the new <name, fid> to the directory. */
	dir_Create(name, &target_fso->fid);

	/* Update the status to reflect the create. */
	RVMLIB_REC_OBJECT(stat);
	stat.DataVersion++;
	stat.Length = dir_Length();
	stat.Date = Mtime;
    }

    /* Set target status. */
    {
	/* Initialize the target fsobj. */
	RVMLIB_REC_OBJECT(*target_fso);
	target_fso->stat.VnodeType = SymbolicLink;
	target_fso->stat.LinkCount = 1;
	target_fso->stat.Length = 0;
	target_fso->stat.DataVersion = 1;
	target_fso->stat.Date = Mtime;
	target_fso->stat.Owner = Owner;
	target_fso->stat.Mode = Mode;
	target_fso->flags.created = 1;
	target_fso->Matriculate();
	target_fso->SetParent(fid.Vnode, fid.Unique);

	/* Write out the link contents. */
	int linklen = (int) strlen(contents);
	target_fso->stat.Length = linklen;
	target_fso->data.symlink = (char *)RVMLIB_REC_MALLOC(linklen + 1);
	RVMLIB_SET_RANGE(target_fso->data.symlink, linklen);
	bcopy(contents, target_fso->data.symlink, linklen);
	UpdateCacheStats(&FSDB->FileDataStats, CREATE,
			 NBLOCKS(linklen));

	target_fso->Reference();
	target_fso->ComputePriority();
    }
}


int fsobj::ConnectedSymlink(Date_t Mtime, vuid_t vuid, fsobj **t_fso_addr,
			     char *name, char *contents,
			     unsigned short Mode, int target_pri) {
    FSO_ASSERT(this, HOARDING(this));

    int code = 0;
    fsobj *target_fso = 0;
    ViceFid target_fid = NullFid;
    RPC2_Unsigned AllocHost = 0;

    /* Status parameters. */
    ViceStatus parent_status;
    VenusToViceStatus(&stat, &parent_status);
    ViceStatus target_status;
    target_status.Mode = Mode;
    {
	/* Temporary!  Until RPC interface is fixed!  -JJK */
	parent_status.Date = Mtime;
	target_status.DataVersion = 1;
	target_status.VV = NullVV;
    }

    /* COP2 Piggybacking. */
    char PiggyData[COP2SIZE];
    RPC2_CountedBS PiggyBS;
    PiggyBS.SeqLen = 0;
    PiggyBS.SeqBody = (RPC2_ByteSeq)PiggyData;

    /* VCB Arguments */
    RPC2_Integer VS = 0;
    CallBackStatus VCBStatus = NoCallBack;
    RPC2_CountedBS OldVS; 
    OldVS.SeqLen = 0;
    OldVS.SeqBody = 0;

    if (flags.replicated) {
	ViceStoreId sid;
	mgrpent *m = 0;
	int asy_resolve = 0;

	/* Allocate a fid for the new object. */
	code = vol->AllocFid(SymbolicLink, &target_fid, &AllocHost, vuid);
	if (code != 0) goto RepExit;

	/* Allocate the fsobj. */
	target_fso = FSDB->Create(&target_fid, WR, target_pri, name);
	if (target_fso == 0) {
	    UpdateCacheStats(&FSDB->FileAttrStats, NOSPACE,
			     NBLOCKS(sizeof(fsobj)));
	    code = ENOSPC;
	    goto RepExit;
	}
	UpdateCacheStats(&FSDB->FileAttrStats, CREATE,
			 NBLOCKS(sizeof(fsobj)));

	/* Acquire an Mgroup. */
	code = vol->GetMgrp(&m, vuid, (PIGGYCOP2 ? &PiggyBS : 0));
	if (code != 0) goto RepExit;

	/* The COP1 call. */
	long cbtemp; cbtemp = cbbreaks;
	vv_t UpdateSet;
	sid = vol->GenerateStoreId();
	{
	    /* Make multiple copies of the IN/OUT and OUT parameters. */
 	    vol->PackVS(m->nhosts, &OldVS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceFid, target_fidvar, target_fid, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, target_statusvar, target_status, VSG_MEMBERS);
	    ARG_MARSHALL(IN_OUT_MODE, ViceStatus, parent_statusvar, parent_status, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, RPC2_Integer, VSvar, VS, VSG_MEMBERS);
	    ARG_MARSHALL(OUT_MODE, CallBackStatus, VCBStatusvar, VCBStatus, VSG_MEMBERS);

	    /* Make the RPC call. */
	    CFSOP_PRELUDE("store::Symlink %-30s\n", contents, NullFid);
	    MULTI_START_MESSAGE(ViceVSymLink_OP);
	    code = (int) MRPC_MakeMulti(ViceVSymLink_OP, ViceVSymLink_PTR,
				  VSG_MEMBERS, m->rocc.handles,
				  m->rocc.retcodes, m->rocc.MIp, 0, 0,
				  &fid, name, contents,
				  target_fidvar_ptrs, target_statusvar_ptrs,
				  parent_statusvar_ptrs, AllocHost, &sid,
				  &OldVS, VSvar_ptrs, VCBStatusvar_ptrs, &PiggyBS);
	    MULTI_END_MESSAGE(ViceVSymLink_OP);
	    CFSOP_POSTLUDE("store::symlink done\n");

	    free(OldVS.SeqBody);
	    /* Collate responses from individual servers and decide what to do next. */
	    code = vol->Collate_COP1(m, code, &UpdateSet);
	    MULTI_RECORD_STATS(ViceVSymLink_OP);
	    if (code == EASYRESOLVE) { asy_resolve = 1; code = 0; }
	    if (code != 0) goto RepExit;

	    /* Collate volume callback information */
	    if (cbtemp == cbbreaks)
		vol->CollateVCB(m, VSvar_bufs, VCBStatusvar_bufs);

	    /* Finalize COP2 Piggybacking. */
	    if (PIGGYCOP2)
		vol->ClearCOP2(&PiggyBS);

	    /* Manually compute the OUT parameters from the mgrpent::Mkdir() call! -JJK */
	    int dh_ix; dh_ix = -1;
	    (void)m->DHCheck(0, -1, &dh_ix);
	    ARG_UNMARSHALL(target_statusvar, target_status, dh_ix);
	    ARG_UNMARSHALL(target_fidvar, target_fid, dh_ix);
	    ARG_UNMARSHALL(parent_statusvar, parent_status, dh_ix);
	}

	/* Do Symlink locally. */
	ATOMIC(
	    LocalSymlink(Mtime, target_fso, name, contents, vuid, Mode);
	    UpdateStatus(&parent_status, &UpdateSet, vuid);
	    target_fso->UpdateStatus(&target_status, &UpdateSet, vuid);
	, CMFP)
	if (target_fso->flags.usecallback &&
	    target_status.CallBack == CallBackSet &&
	    cbtemp == cbbreaks)
	    target_fso->SetRcRights(RC_STATUS | RC_DATA);
	if (ASYNCCOP2) target_fso->ReturnEarly();

	/* Send the COP2 message or add an entry for piggybacking. */
	if (PIGGYCOP2)
	    vol->AddCOP2(&sid, &UpdateSet);
	else
	    (void)vol->COP2(m, &sid, &UpdateSet);

RepExit:
	PutMgrp(&m);
	switch(code) {
	    case 0:
		if (asy_resolve) {
		    vol->ResSubmit(0, &fid);
		    if (target_fso != 0)
			vol->ResSubmit(0, &target_fso->fid);
		}
		break;

	    case ETIMEDOUT:
	    case ESYNRESOLVE:
	    case EINCONS:
		code = ERETRY;
		break;

	    default:
		break;
	}
    }
    else {
	/* Acquire a Connection. */
	connent *c;
	ViceStoreId Dummy;                   /* Need an address for ViceSymLink */
	code = vol->GetConn(&c, vuid);
	if (code != 0) goto NonRepExit;

	/* Make the RPC call. */
	long cbtemp; cbtemp = cbbreaks;
	CFSOP_PRELUDE("store::Symlink %-30s\n", contents, NullFid);
	UNI_START_MESSAGE(ViceVSymLink_OP);
	code = (int) ViceVSymLink(c->connid, &fid, (RPC2_String)name, 
				  (RPC2_String)contents, 
				  &target_fid, &target_status,
				  &parent_status, 0, &Dummy, 
				  &OldVS, &VS, &VCBStatus, &PiggyBS);
	UNI_END_MESSAGE(ViceVSymLink_OP);
	CFSOP_POSTLUDE("store::symlink done\n");

	/* Examine the return code to decide what to do next. */
	code = vol->Collate(c, code);
	UNI_RECORD_STATS(ViceVSymLink_OP);
	if (code != 0) goto NonRepExit;

	/* Allocate the fsobj. */
	target_fso = FSDB->Create(&target_fid, WR, target_pri, name);
	if (target_fso == 0) {
	    UpdateCacheStats(&FSDB->FileAttrStats, NOSPACE,
			     NBLOCKS(sizeof(fsobj)));
	    code = ENOSPC;
	    goto NonRepExit;
	}
	UpdateCacheStats(&FSDB->FileAttrStats, CREATE,
			 NBLOCKS(sizeof(fsobj)));

	/* Do Symlink locally. */
	ATOMIC(
	    LocalSymlink(Mtime, target_fso, name, contents, vuid, Mode);
	    UpdateStatus(&parent_status, 0, vuid);
	    target_fso->UpdateStatus(&target_status, 0, vuid);
	, CMFP)
	if (target_fso->flags.usecallback &&
	    target_status.CallBack == CallBackSet &&
	    cbtemp == cbbreaks)
	    target_fso->SetRcRights(RC_STATUS | RC_DATA);

NonRepExit:
	PutConn(&c);
    }

    if (code == 0) {
	*t_fso_addr = target_fso;
    }
    else {
	if (target_fso != 0) {
	    FSO_ASSERT(target_fso, !HAVESTATUS(target_fso));
	    ATOMIC(
		target_fso->Kill();
	    , DMFP);
	    FSDB->Put(&target_fso);
	}
    }
    return(code);
}


/* local-repair modification */
int fsobj::DisconnectedSymlink(Date_t Mtime, vuid_t vuid, fsobj **t_fso_addr,
			       char *name, char *contents, unsigned short Mode, 
			       int target_pri, int Tid) {
    FSO_ASSERT(this, (EMULATING(this) || LOGGING(this)));

    int code = 0;
    fsobj *target_fso = 0;
    ViceFid target_fid = NullFid;
    RPC2_Unsigned AllocHost = 0;

    if (!flags.replicated) {
	code = ETIMEDOUT;
	goto Exit;
    }

    /* Allocate a fid for the new object. */
    /* if we time out, return so we will try again with a local fid. */
    code = vol->AllocFid(SymbolicLink, &target_fid, &AllocHost, vuid);
    if (code != 0) goto Exit;

    /* Allocate the fsobj. */
    target_fso = FSDB->Create(&target_fid, WR, target_pri, name);
    if (target_fso == 0) {
	UpdateCacheStats(&FSDB->FileAttrStats, NOSPACE,
			 NBLOCKS(sizeof(fsobj)));
	code = ENOSPC;
	goto Exit;
    }
    UpdateCacheStats(&FSDB->FileAttrStats, CREATE,
		      NBLOCKS(sizeof(fsobj)));

    ATOMIC(
	code = vol->LogSymlink(Mtime, vuid, &fid, name,
			       contents, &target_fso->fid, Mode, Tid);

	   if (code == 0) {
	    /* This MUST update second-class state! */
	    LocalSymlink(Mtime, target_fso, name, contents, vuid, Mode);

	    /* target_fso->stat is not initialized until LocalSymlink */
	    RVMLIB_REC_OBJECT(target_fso->CleanStat);
	    target_fso->CleanStat.Length = target_fso->stat.Length;
	    target_fso->CleanStat.Date = target_fso->stat.Date;
	   }
    , DMFP)

Exit:
    if (code == 0) {
	*t_fso_addr = target_fso;
    }
    else {
	if (target_fso != 0) {
	    FSO_ASSERT(target_fso, !HAVESTATUS(target_fso));
	    ATOMIC(
		target_fso->Kill();
	    , DMFP);
	    FSDB->Put(&target_fso);
	}
    }
    return(code);
}


/* local-repair modification */
int fsobj::Symlink(char *s_name, char *t_name,
		    vuid_t vuid, unsigned short Mode, int target_pri) {
    LOG(10, ("fsobj::Symlink: (%s, %s, %s, %d), uid = %d\n",
	      comp, s_name, t_name, target_pri, vuid));

    int code = 0;
    Date_t Mtime = Vtime();
    fsobj *target_fso = 0;

    int conn, tid;
    GetOperationState(&conn, &tid);
    if (conn == 0) {
	code = DisconnectedSymlink(Mtime, vuid, &target_fso, t_name, s_name, Mode, 
				   target_pri, tid);
    }
    else {
	code = ConnectedSymlink(Mtime, vuid, &target_fso,
				t_name, s_name, Mode, target_pri);
    }

    if (code == 0) {
	/* Target is NOT an OUT parameter. */
	FSDB->Put(&target_fso);
    }
    else {
	Demote();
    }
    return(code);
}


/*  *****  SetVV  *****  */

/* This should eventually disappear to be a side-effect of the Repair call! -JJK */
/* Call with object write-locked. */
int fsobj::SetVV(ViceVersionVector *newvv, vuid_t vuid) {
    LOG(10, ("fsobj::SetVV: (%s), uid = %d\n",
	      comp, vuid));

    int code = 0;

    if (EMULATING(this) || LOGGING(this)) {
	/* This is a connected-mode only routine! */
	code = ETIMEDOUT;
    }
    else {
	FSO_ASSERT(this, HOARDING(this));

	/* COP2 Piggybacking. */
	char PiggyData[COP2SIZE];
	RPC2_CountedBS PiggyBS;
	PiggyBS.SeqLen = 0;
	PiggyBS.SeqBody = (RPC2_ByteSeq)PiggyData;

	if (flags.replicated) {
	    mgrpent *m = 0;

	    /* Acquire an Mgroup. */
	    code = vol->GetMgrp(&m, vuid, (PIGGYCOP2 ? &PiggyBS : 0));
	    if (code != 0) goto RepExit;

	    /* The SetVV call. */
	    {
		/* Make the RPC call. */
		CFSOP_PRELUDE("store::SetVV %-30s\n", comp, fid);
		MULTI_START_MESSAGE(ViceSetVV_OP);
		code = (int) MRPC_MakeMulti(ViceSetVV_OP, ViceSetVV_PTR,
				      VSG_MEMBERS, m->rocc.handles,
				      m->rocc.retcodes, m->rocc.MIp, 0, 0,
				      &fid, newvv, &PiggyBS);
		MULTI_END_MESSAGE(ViceSetVV_OP);
		CFSOP_POSTLUDE("store::setvv done\n");

		/* Collate responses from individual servers and decide what to do next. */
		code = vol->Collate_COP2(m, code);
		MULTI_RECORD_STATS(ViceSetVV_OP);
		if (code != 0) goto RepExit;

		/* Finalize COP2 Piggybacking. */
		if (PIGGYCOP2)
		    vol->ClearCOP2(&PiggyBS);
	    }

	    /* Do op locally. */
	    ATOMIC(
		RVMLIB_REC_OBJECT(stat);
		stat.VV = *newvv;
	    , CMFP)

RepExit:
	    PutMgrp(&m);
	    switch(code) {
		case 0:
		    break;

		case ETIMEDOUT:
		    code = ERETRY;
		    break;

		case ESYNRESOLVE:
		case EINCONS:
		    Choke("fsobj::SetVV: code = %d", code);
		    break;

		default:
		    break;
	    }
	}
	else {
	    /* Acquire a Connection. */
	    connent *c;
	    code = vol->GetConn(&c, vuid);
	    if (code != 0) goto NonRepExit;

	    /* Make the RPC call. */
	    CFSOP_PRELUDE("store::SetVV %-30s\n", comp, fid);
	    UNI_START_MESSAGE(ViceSetVV_OP);
	    code = (int) ViceSetVV(c->connid, &fid, newvv, &PiggyBS);
	    UNI_END_MESSAGE(ViceSetVV_OP);
	    CFSOP_POSTLUDE("store::setvv done\n");

	    /* Examine the return code to decide what to do next. */
	    code = vol->Collate(c, code);
	    UNI_RECORD_STATS(ViceSetVV_OP);
	    if (code != 0) goto NonRepExit;

	    /* Do op locally. */
	    ATOMIC(
		RVMLIB_REC_OBJECT(stat);
		stat.VV = *newvv;
	    , CMFP)

NonRepExit:
	    PutConn(&c);
	}
    }

    /* Replica control rights are invalid in any case. */
    Demote();

  LOG(0, ("MARIA:  We just SetVV'd.\n"));

    return(code);
}
