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







/* lookup.c
   Manual lookup of volume location data base information 
   for a particular volume.
 */

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <sys/time.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <unistd.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <lwp.h>
#include <lock.h>
#include <timer.h>
#include <rpc2.h>
#include <se.h>
#include <util.h>
#include <rvmlib.h>

#include <volutil.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <voltypes.h>
#include <vice.h>
#include <cvnode.h>
#include <volume.h>
#include <camprivate.h>
#include <al.h>
#include <voldefs.h>
#include <vldb.h>
#include <partition.h>
#include <viceinode.h>
#include <vutil.h>
#include <index.h>
#include <volhash.h>
#include <coda_globals.h>

#define INFOFILE    "/tmp/vollookup.tmp"
static FILE * infofile;    // descriptor for info file

struct hostent *gethostent();

char *voltypes[] = {"read/write", "read only", "backup", "unknown type", "unknown type"};

/*
  S_VolLookup: Return information for a volume specified 
  by name or volume-id
*/
long int S_VolLookup(RPC2_Handle rpcid, RPC2_String formal_vol, SE_Descriptor *formal_sed) {
    VolumeInfo info;
    Error error = 0;
    int status = 0;
    long rc = 0;
    SE_Descriptor sed;
    ProgramType *pt;

    /* To keep C++ 2.0 happy */
    char *vol = (char *)formal_vol;

    LogMsg(9, VolDebugLevel, stdout, "Entering S_VolLookup(%u, %s)", rpcid, vol);
    assert(LWP_GetRock(FSTAG, (char **)&pt) == LWP_SUCCESS);


    RVMLIB_BEGIN_TRANSACTION(restore)
    VInitVolUtil(volumeUtility);

    infofile = fopen (INFOFILE, "w");

    /* See if user passed in volid rather than volname */
    long volid, index;
    if ((sscanf(vol, "%X", &volid) ==  1) && ((index = HashLookup(volid)) > 0)) {
	VolumeDiskData *vp = SRV_RVM(VolumeList[index]).data.volumeInfo;
	VGetVolumeInfo(&error, vp->name, &info);
    } else {
	VGetVolumeInfo(&error, vol, &info);
    }

    if (error) {
	LogMsg(0, VolDebugLevel, stdout, "SVolLookup: error code %d returned for volume \"%s\"",
		    error, vol);
	rvmlib_abort(error);
    }
    else {
        register VolumeId *p;
	int printed, i;
	register RPC2_Unsigned *sptr;
	RPC2_Unsigned s;
	fprintf(infofile, "Info for vol \"%s\": volume id %x, %s volume\n", vol, info.Vid, voltypes[info.Type]);
    	fprintf(infofile, "Associates: ");
	for (printed=0, p = &info.Type0, i = 0; i<MAXVOLTYPES; i++, p++) {
	    if (*p) {
		if (printed)
		    fprintf(infofile, ",");
		fprintf(infofile, " %s volume %x", voltypes[i], *p);
		printed++;
	    }
	}
	fprintf(infofile, "\nOn servers: ");
	for (i = 0, sptr = &info.Server0; i<info.ServerCount; i++,sptr++) {
	    struct hostent *h;
	    s = htonl(*sptr);
	    h = gethostbyaddr((char *)&s, sizeof(s), AF_INET);
	    if (h)
	        fprintf(infofile, "%s", h->h_name);
	    if (i<info.ServerCount-1)
	        fprintf(infofile, ", ");
	}
	fprintf(infofile, "\n");
    }

    fclose(infofile);

    /* set up SE_Descriptor for transfer */
    bzero((void *)&sed, sizeof(sed));
    sed.Tag = SMARTFTP;
    sed.Value.SmartFTPD.Tag = FILEBYNAME;
    sed.Value.SmartFTPD.TransmissionDirection = SERVERTOCLIENT;
    strcpy(sed.Value.SmartFTPD.FileInfo.ByName.LocalFileName, INFOFILE);
    sed.Value.SmartFTPD.FileInfo.ByName.ProtectionBits = 0755;

    if ((rc = RPC2_InitSideEffect(rpcid, &sed)) <= RPC2_ELIMIT) {
	LogMsg(0, VolDebugLevel, stdout, "VolLookup: InitSideEffect failed with %s", RPC2_ErrorMsg(rc));
	rvmlib_abort(VFAIL);
    }

    if ((rc = RPC2_CheckSideEffect(rpcid, &sed, SE_AWAITLOCALSTATUS)) <=
		RPC2_ELIMIT) {
	LogMsg(0, VolDebugLevel, stdout, "VolLookup: CheckSideEffect failed with %s", RPC2_ErrorMsg(rc));
	rvmlib_abort(VFAIL);
    }

    RVMLIB_END_TRANSACTION(flush, &(status));
    VDisconnectFS();

    if (status)
	LogMsg(0, VolDebugLevel, stdout, "SVolLookup failed with %d", status);
    else
	LogMsg(9, VolDebugLevel, stdout, "SVolLookup returns %s", RPC2_ErrorMsg(rc));

    return (status?status:rc);

}

