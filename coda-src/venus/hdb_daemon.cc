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
 *    Hoard database daemon.
 *
 *    The daemon insists that all commands have been issued with an effective uid of root (V_UID).
 *    This ensures that we are dealing with a "locally authoritative" user/program.
 *
 */


#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <stdio.h>
#include <sys/types.h>

#include <lwp.h>

#ifdef __cplusplus
}
#endif __cplusplus

/* from venus */
#include "hdb.h"
#include "venus.private.h"
#include "vproc.h"


/*  *****  Private constants  *****  */

PRIVATE const int HDBDaemonInterval = 300;
PRIVATE const int HdbWalkInterval = 10 * 60;
PRIVATE const int HDBDaemonStackSize = 65536;
PRIVATE const int HDBDaemonPriority = LWP_NORMAL_PRIORITY-1;

/* *****  Private types  ***** */

struct hdbd_msg : public olink {
    char wait_blk;
    enum hdbd_request type;
    void *request;
    int result;
};


/*  *****  Private variables  *****  */

PRIVATE char hdbdaemon_sync;
PRIVATE olist hdbd_msgq;


/*  *****  Private routines  ***** */

PRIVATE void HDBD_HandleRequests();


/*  *****  The HDB Daemon  *****  */

void HDBD_Init() {
    (void)new vproc("HDBDaemon", (PROCBODY) &HDBDaemon,
		     VPT_HDBDaemon, HDBDaemonStackSize, HDBDaemonPriority);
}


void HDBDaemon() {

    /* Hack!  Vproc must yield before data members become valid! */
    VprocYield();

    vproc *vp = VprocSelf();
    RegisterDaemon(HDBDaemonInterval, &hdbdaemon_sync);

    long LastHdbWalk = /*0*/Vtime();	    /* skip initial walk at startup! */

    for (;;) {
	VprocWait(&hdbdaemon_sync);

	START_TIMING();
	long curr_time = Vtime();

	/* Handle requests BEFORE periodic events. */
	HDBD_HandleRequests();

	/* Periodic events. */
	if (PeriodicWalksAllowed) {
	    /* Walk HDB. */
	    if (curr_time - LastHdbWalk >= HdbWalkInterval) {
		struct hdb_walk_msg m;
		m.ruid = V_UID;
		(void)HDB->Walk(&m);
		LastHdbWalk = curr_time;
	    }
	}

	/* Handle requests AFTER periodic events (in case we blocked during their execution). */
	HDBD_HandleRequests();

	END_TIMING();
	LOG(10, ("HDBDaemon: elapsed = %3.1f (%3.1f, %3.1f)\n",
		 elapsed, elapsed_ru_utime, elapsed_ru_stime));

	/* Bump sequence number. */
	vp->seq++;
    }
}


int HDBD_Request(hdbd_request type, void *request, vuid_t euid, vuid_t ruid) {
    /* Ensure request was issued by "locally authoritative" entity. */
    if (euid != V_UID/* || ruid != msg->ruid*/)
	return(EACCES);

    /* Form message. */
    hdbd_msg m;
    m.type = type;
    m.request = request;
    m.result = 0;

    /* Send it, and wait for reply. */
    hdbd_msgq.append(&m);
    VprocSignal(&hdbdaemon_sync);
    LOG(0, ("WAITING(HDBD_Request): %s\n", PRINT_HDBDREQTYPE(type)));
    START_TIMING();
    VprocWait(&m.wait_blk);
    END_TIMING();
    LOG(0, ("WAIT OVER, elapsed = %3.1f, returns %d\n", elapsed, m.result));

    /* Return result. */
    return(m.result);
}


PRIVATE void HDBD_HandleRequests() {
    hdbd_msg *m = 0;
    while (m = (hdbd_msg *)hdbd_msgq.get()) {
	switch(m->type) {
	    case HdbAdd:
		m->result = HDB->Add((hdb_add_msg *)m->request);
		break;

	    case HdbDelete:
		m->result = HDB->Delete((hdb_delete_msg *)m->request);
		break;

	    case HdbClear:
		m->result = HDB->Clear((hdb_clear_msg *)m->request);
		break;

	    case HdbList:
		m->result = HDB->List((hdb_list_msg *)m->request);
		break;

	    case HdbWalk:
		m->result = HDB->Walk((hdb_walk_msg *)m->request);
		break;

	    case HdbEnable:
		m->result = HDB->Enable((hdb_walk_msg *)m->request);
		break;

	    case HdbDisable:
		m->result = HDB->Disable((hdb_walk_msg *)m->request);
		break;
		
	    case HdbVerify:
		m->result = HDB->Verify((hdb_verify_msg *)m->request);
		break;

	    default:
		Choke("HDBD_HandleRequests: bogus type (%d)", m->type);
	}

	/* Send back the result. */
	VprocSignal(&m->wait_blk);
    }
}
