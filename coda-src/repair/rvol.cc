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
                           none currently

#*/






/*
    Routines to mainpulate data structures of volumes in repair
    
    Created: M. Satyanarayanan
	     October 1989
*/

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include "coda_assert.h"
#include <setjmp.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/stat.h>
#include <rpc2.h>
#include <inodeops.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <vice.h>
#include <venusioctl.h>
#include "repair.h"


/* Head of circular linked list of volumes in repair */
struct repvol *RepVolHead;
static char compresult[2048];
static char junk[2048];

static char *volstr(char *), *srvstr(char *, VolumeId);
static char *compstr(char *, VolumeId);

int repair_findrep(VolumeId vid, struct repvol **repv /* OUT */)
    /* Returns 0 and fills repv if an entry exists for rep vol vid.
       Returns -1 and sets repv to null otherwise.
    */
    {
    register struct repvol *r;

    for (r = RepVolHead; r; r = r->next)
	if (r->vid == vid)
	    {
	    *repv = r;
	    return(0);
	    }
    *repv = 0;
    return(-1);
    }

int repair_newrep(VolumeId vid, char *mnt, struct repvol **repv /* OUT */)
    /* Allocates a new element */
    {

    *repv = (struct repvol *) calloc(1, sizeof(struct repvol));  /* inits all fields to 0 */
    CODA_ASSERT(*repv); /* better not run out of memory! */
    (*repv)->vid = vid;
    strcpy((*repv)->mnt, mnt);  /* remember its mount point */
    
    /* get its vol name: GETVOLSTAT does not work on rep volumes right now.
       so just use hex version of volid */
    sprintf((*repv)->vname, "%#lx", vid);

    return(0);
    }



int repair_mountrw(struct repvol *repv, VolumeId *rwarray, int arraylen)
    /*
    rwarray:	non-zero elements specify volids of replicas
    arraylen:	how many elements in rwarray

    Allocates and links each rw replica in rwarray.
    "mount" in function name is historical
       Returns 0 on success
       Returns -1 on failure. NO CLEANUP IS DONE.
    */
    {
    register int i;
    register struct rwvol *rwv, *rwtail;

    rwtail = NULL;
    for (i = 0; i < arraylen; i++) {
	/* Create an entry for each RW replica */

	if (!rwarray[i]) continue;  /* pioctl output need not be contiguous */

	/* Create  element */
	rwv = (struct rwvol *) calloc(1, sizeof(struct rwvol));
	CODA_ASSERT(rwv);
	rwv->vid = rwarray[i];

	/* Link it in */
	if (rwtail) rwtail->next = rwv;
	else repv->rwhead = rwv;
	rwtail = rwv;

	/* Get its component name */
	strncpy(rwv->compname, compstr(repv->rodir, rwv->vid),
		sizeof(rwv->compname)-1);

	/* Get its volume name */
	strncpy(rwv->vname, volstr(repv->rodir), sizeof(rwv->vname)-1);
	
	/* And its server */
	strncpy(rwv->srvname, srvstr(repv->rodir, rwv->vid), sizeof(rwv->srvname)-1);
    }
    return(0);
}

int repair_linkrep(struct repvol *repv)
    /* Adds an element to the circular list */
    {
    if (RepVolHead)
	{/* Add just before head */
	repv->prev = RepVolHead->prev;
	repv->next = RepVolHead;
	RepVolHead->prev->next = repv;
	RepVolHead->prev = repv;
	}
    else
	{
	RepVolHead = repv;
	repv->prev = repv->next = repv;
	}
    return(0);
    }


int repair_unlinkrep(struct repvol *repv)
    
    /* Removes an element from circular list.
       repv better be in the linked list; I don't verify this */
    {
    
    repv->prev->next = repv->next;
    repv->next->prev = repv->prev;
    if (RepVolHead == repv) RepVolHead = repv->next;
    if (RepVolHead == repv) RepVolHead = NULL; /* must have been a 1-element list */
    return(0);
    }


int repair_finish(struct repvol *repv)
    /* Frees all data structures associated with repv.*/
{
    struct rwvol *rwv, *next;

    rwv = repv->rwhead;
    while (rwv)	{
	next = rwv->next;
	free(rwv);
	rwv = next;
    }
    free(repv);
    return(0);
}

int repair_countRWReplicas(struct repvol *repv)
{
    int count = 0;
    struct rwvol *rwv = repv->rwhead;
    
    for ( ; rwv; rwv = rwv->next)
	count++;
    return(count);
}


static char *volstr(char *path)
    /* Returns a static string identifying volume name of path.
       String contains msg if pioctl fails */
{
    struct ViceIoctl vioc;
    int rc;

    bzero(junk, sizeof(junk));
    vioc.in = 0;
    vioc.in_size = 0;
    vioc.out = junk;
    vioc.out_size = sizeof(junk);
    rc = pioctl(path, VIOCGETVOLSTAT, &vioc, 1);
    if (rc) return("VIOCGETVOLSTAT failed");
    else return(junk+sizeof(struct VolumeStatus)); /* name is after VolumeStatus */
}

/* Returns a static string identifying servers with replicas of path
   String contains msg if pioctl fails */    
static char *srvstr(char *path, VolumeId rwid)
{
    struct ViceIoctl vioc;
    char junk[2048], tmp[64];
    static char result[2048];
    struct hostent *thp;
    long *hosts;
    int i, rc;
    char rwpath[MAXPATHLEN];
    VolumeStatus *vs;

    /* this is a big hack.  Venus returns volumeids for beginrepair 
       but puts names in the fake directory.  To get the location we
       check each entry in the directory until we find the volume id
       that matches */
    struct dirent *de;
    DIR *d = opendir(path);
    if (!d) {
	sprintf(result, "srvstr couldn't open %s", path);
	return(result);
    }
    while ( (de = readdir(d)) ) {
	if (!strcmp(de->d_name, ".") ||
	    !strcmp(de->d_name, ".."))
	    continue;
	// get volume id of pathname 
	sprintf(rwpath, "%s/%s", path, de->d_name);
	bzero(junk, sizeof(junk));
	vioc.in = 0;
	vioc.in_size = 0;
	vioc.out = junk;
	vioc.out_size = sizeof(junk);
	rc = pioctl(rwpath, VIOCGETVOLSTAT, &vioc, 1);
	if (rc) continue;
	else {
	    vs = (VolumeStatus *)junk;
	    if (vs->Vid == rwid)  break;
	}
    }
    if (de) {
	/* get the server name by doing the pioctl (for compatibility with old venii)*/
	vioc.in_size = 0;
	vioc.in = 0;
	vioc.out_size = sizeof(junk);
	vioc.out = junk;
	bzero(junk, sizeof(junk));
	sprintf(rwpath, "%s/%s", path, de->d_name);
	rc = pioctl(rwpath, VIOCWHEREIS, &vioc, 1);
	if (rc) return("VIOCWHEREIS failed");
	hosts = (long *) junk;
	bzero(result, sizeof(result));
	for(i = 0; i < 8; i++)
	    {
		if (hosts[i] == 0) break;
		hosts[i] = htonl(hosts[i]);    /* gethostbyaddr requires network order */
		thp = gethostbyaddr((char *)&hosts[i], sizeof(long), AF_INET);
		if (thp) sprintf(tmp, "%s ", thp->h_name);
		else sprintf(tmp, "%08lx ", hosts[i]);
		strcat(result, tmp);
	    }
    }
    else 
	sprintf(result, "UnknownServer");

    closedir(d);

    return(result);
}

static char *compstr(char *path, VolumeId rwid)
/* returns a static string identifying the component corresponding to
   this rw volume id. */
{
    struct ViceIoctl vioc;
    char junk[2048];
    int rc;
    char rwpath[MAXPATHLEN];
    VolumeStatus *vs;

    /* this is a big hack.  Venus returns volumeids for beginrepair 
       but puts names in the fake directory.  To get the location we
       check each entry in the directory until we find the volume id
       that matches */
    struct dirent *de;
    DIR *d = opendir(path);
    if (!d) {
	sprintf(compresult, "compstr couldn't open %s", path);
	return(compresult);
    }
    while ( (de = readdir(d)) ) {
	if (!strcmp(de->d_name, ".") ||
	    !strcmp(de->d_name, ".."))
	    continue;
	// get volume id of pathname 
	sprintf(rwpath, "%s/%s", path, de->d_name);
	bzero(junk, sizeof(junk));
	vioc.in = 0;
	vioc.in_size = 0;
	vioc.out = junk;
	vioc.out_size = sizeof(junk);
	rc = pioctl(rwpath, VIOCGETVOLSTAT, &vioc, 1);
	if (rc) continue;
	else {
	    vs = (VolumeStatus *)junk;
	    if (vs->Vid == rwid)  break;
	}
    }

    if (de) {
	strncpy(compresult, de->d_name, 2047);
	compresult[2048] = '\0';
    } else
	strcpy(compresult, "UnknownComponent");

    closedir(d);

    return(compresult);
}
