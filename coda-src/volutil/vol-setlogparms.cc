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



#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/signal.h>

#ifdef __MACH__
#include <libc.h>
#include <sysent.h>
#endif /* __MACH__ */

#if defined(__linux__) || defined(__NetBSD__)
#include <unistd.h>
#include <stdlib.h>
#endif /* __NetBSD__ || LINUX */

#include <lwp.h>
#include <lock.h>

#ifdef __cplusplus
}
#endif __cplusplus

#include <util.h>
#include <rvmlib.h>
#include <vice.h>
#include <nfs.h>
#include <cvnode.h>
#include <volume.h>
#include <vrdb.h>
#include <srv.h>
#include <vutil.h>
#include <logalloc.h>
#include <recov_vollog.h>

extern PMemMgr *LogStore[];

/*
  BEGIN_HTML
  <a name="S_VolSetLogParms"><strong>Set the parameters for the resolution log </strong></a> 
  END_HTML
*/
long S_VolSetLogParms(RPC2_Handle rpcid, VolumeId Vid, RPC2_Integer OnFlag, RPC2_Integer maxlogsize) {
    Volume *volptr = 0;
    Error error;
    ProgramType *pt;
    int rc = 0;
    int status = 0;

    LogMsg(9, VolDebugLevel, stdout, "Entering S_VolSetLogParms: rpcid = %d, Volume = %x", 
	 rpcid, Vid);
    
    assert(LWP_GetRock(FSTAG, (char **)&pt) == LWP_SUCCESS);

    CAMLIB_BEGIN_TOP_LEVEL_TRANSACTION_2(CAM_TRAN_NV_SERVER_BASED);
    rc = VInitVolUtil(volumeUtility);
    if (rc != 0){
	CAMLIB_ABORT(rc);
    }
    XlateVid(&Vid);
    volptr = VGetVolume(&error, Vid);

    if (error) {
	LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: VGetVolume error %d",error);
	CAMLIB_ABORT(error);
    }

    LogMsg(9, SrvDebugLevel, stdout, "S_VolSetLogParms: Got Volume %x",Vid);
    if ((OnFlag & VMRES) || (OnFlag & RVMRES)) {
	volptr->header->diskstuff.ResOn |= OnFlag;
	LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: res flag on volume 0x%x set to %d", 
	       Vid, volptr->header->diskstuff.ResOn);
    }
    if (OnFlag == 0) {
	volptr->header->diskstuff.ResOn = 0;
	LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: res flag on volume 0x%x set to %d", Vid, OnFlag);
    }
    
    if (maxlogsize != 0) {
	if ((maxlogsize & 0x1F) != 0) {
	    LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: Log Size has to be a multiple of 32");
	    VPutVolume(volptr);
	    CAMLIB_ABORT(EINVAL);
	}
	if (AllowResolution && V_VMResOn(volptr)) {
	    if (LogStore[V_volumeindex(volptr)]->maxRecordsAllowed > maxlogsize) {
		LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: Cant reduce log size");
		VPutVolume(volptr);
		CAMLIB_ABORT(EINVAL);
	    }
	    LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: Changing log size from %d to %d\n", 
		   LogStore[V_volumeindex(volptr)]->maxRecordsAllowed,
		   maxlogsize);
	    LogStore[V_volumeindex(volptr)]->maxRecordsAllowed = maxlogsize;
	    V_maxlogentries(volptr) = maxlogsize;
	}
	if (AllowResolution && V_RVMResOn(volptr) && V_VolLog(volptr)){
	    V_VolLog(volptr)->Increase_Admin_Limit(maxlogsize);
	    LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: Changed RVM log size to %d\n",
		   maxlogsize);
	}
    }
    VUpdateVolume(&error, volptr);
    if (error) {
	LogMsg(0, SrvDebugLevel, stdout, "S_VolSetLogParms: Error updating volume %x", Vid);
	VPutVolume(volptr);
	CAMLIB_ABORT(error);
    }
    VPutVolume(volptr);
    CAMLIB_END_TOP_LEVEL_TRANSACTION_2(CAM_PROT_TWO_PHASED, status);
    VDisconnectFS();
    if (status == 0) 
	LogMsg(0, VolDebugLevel, stdout, "S_VolSetLogParms: volume %x log parms set", Vid);
    else 
	LogMsg(0, VolDebugLevel, stdout, "S_VolSetLogParms: set log parameters failed for %x", Vid);
    
    return(status?status:0);
}

