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
 * A program to help decipher dump files. The basic idea is to have a command
 * interpreter (ci?) which provides commands to help a user poke around
 * the dump file. basic commands are show header, show vol, show vnode,
 * set vnode index, etc.
 */

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/param.h>
#include <stdio.h>
#include <ci.h>
#include <libcs.h>
#ifdef __MACH__
#include <sysent.h>
#else	/* __linux__ || __BSD44__ */
#include <unistd.h>
#include <stdlib.h>
#endif

#include <lwp.h>
#include <lock.h>

#ifdef __cplusplus
}
#endif __cplusplus

#include <util.h>
#include <voltypes.h>
#include <vcrcommon.h>
#include <cvnode.h>
#include <volume.h>
#define PAGESIZE 2048	/* This is a problem, but can't inherit rvmdir.h */
#include "dump.h"
#include "dumpstream.h"


/* Obviously needs work. */

int OpenDumpFile(char *);
int setIndex(char *);
int showHeader(char *);
int showVolumeDiskData(char *);
int showVnodeDiskObject(char *);
int skipVnodes(char *args);

CIENTRY CiList[] =
{
    CICMD("openDumpFile", OpenDumpFile),		/* FileName */
    CICMD("setIndex", setIndex),			/* "large" or "small" */
    CICMD("showHeader", showHeader),			/* no args */
    CICMD("showVolumeDiskData", showVolumeDiskData),	/* no args */
    CICMD("nextVnode", showVnodeDiskObject),		/* args? */
    CICMD("skipVnodes", skipVnodes),			/* nvnodes to skip */
    CICMD("quit", exit),
    CIEND
};

char DefaultDumpFile[MAXPATHLEN];
char DefaultSize[10];			/* "large" or "small" */
dumpstream *DumpStream;
int Open = 0;

int OpenDumpFile(char *args) {		/* FileName */
    char filename[MAXPATHLEN], *p = args;

    *filename = 0;
    while (*filename == 0)
	strarg(&p, " ", "Name of DumpFile?", DefaultDumpFile, filename);

    strcpy(DefaultDumpFile, filename);

    if (DumpStream)
	delete DumpStream;

    DumpStream = new dumpstream(filename);
    Open = 1;
    return 0;
}

int Rewind(char *args) {
    if (!Open) {
	printf("No DumpFile open yet!\n");
	return(-1);
    }

    assert(DumpStream);
    delete DumpStream;
    DumpStream = new dumpstream(DefaultDumpFile);
    return 0;
}
    
void PrintVersionVector(vv_t *v, char *str) {
    printf("%s{[", str);
    for (int i = 0; i < VSG_MEMBERS; i++)
	printf(" %d", (&(v->Versions.Site0))[i]);
    printf(" ] [ %d %d ] [ %#x ]}\n",
	     v->StoreId.Host, v->StoreId.Uniquifier, v->Flags);
}    

int showHeader(char *args) {
    if (!Open) {
	printf("No DumpFile open yet!\n");
	return(-1);
    }

    Rewind(0);	/* Start from the beginning of the dump */

    struct DumpHeader head;
    if (DumpStream->getDumpHeader(&head) == 0) {
	printf("Error -- DumpHead is not valid\n");
        return(-1);
    }

    printf("%s Dump Version = %d\n", (head.Incremental)?"Incremental":"Full",
	   head.version);
    printf("VolId = 0x%x, name = %s\n", head.volumeId, head.volumeName);
    printf("Parent = 0x%x, backupDate = %s", head.parentId, ctime((long *)&head.backupDate));
    printf("Ordering references: Oldest %d, Latest %d\n",head.oldest,head.latest);
    return 0;
}

int showVolumeDiskData(char *args) {
    if (!Open) {
	printf("No DumpFile open yet!\n");
	return(-1);
    }

    int i = 0;
    VolumeDiskData data;

    /* Place ourselves in the correct point in the dump. */
    Rewind(0);
    struct DumpHeader head;
    if (DumpStream->getDumpHeader(&head) == 0) {
	printf("Error -- DumpHead is not valid\n");
        return(-1);
    }
    
    assert(DumpStream->getVolDiskData(&data) == 0);

    printf("\tversion stamp = %x, %u\n", data.stamp.magic, data.stamp.version);
    printf("\tid = %x\n\tpartition = %s\n\tname = %s\n\tinUse = %u\n\tinService = %u\n",
	    data.id, data.partition, data.name, data.inUse, data.inService);

    if (data.stamp.magic != 0) {
	printf("\tblessed = %u\n\tneedsSalvaged = %u\n\tuniquifier= %u\n\ttype = %d\n",
	    data.blessed, data.needsSalvaged, data.uniquifier, data.type);
	printf("\tparentId = %x\n\tgrpId = %x\n\tcloneId = %x\n\tbackupId = %xn\trestoreFromId = %x\n",
	    data.parentId, data.groupId, data.cloneId, data.backupId, data.restoredFromId);
	printf("\tneedsCallback = %u\n\tdestroyMe = %u\n\tdontSalvage = %u\n\treserveb3 = %u\n",
	    data.needsCallback, data.destroyMe, data.dontSalvage, data.reserveb3);
	PrintVersionVector(&data.versionvector, "\t");
    }
    printf("\t");
    for (i = 0; i < 3; i++) {
	printf("reserved1[%d] = %u, ", i, data.reserved1[i]);
    }
    printf("\n\t");
    for (i = 3; i < 6; i++) {
	printf("reserved1[%d] = %u, ", i, data.reserved1[i]);
    }
    printf("\n");

    printf("\tmaxquota = %d\n\tminquota = %d\n\tmaxfiles = %d\n\tacctNum = %u\n\towner = %u\n",
	data.maxquota, data.minquota, data.maxfiles, data.accountNumber, data.owner);
    printf("\t");
    for (i = 0; i < 3; i++) {
	printf("reserved2[%d] = %d, ", i, data.reserved2[i]);
    }
    printf("\n\t");
    for (i = 3; i < 6; i++) {
	printf("reserved2[%d] = %d, ", i, data.reserved2[i]);
    }
    printf("\n\t");
    for (i = 6; i < 8; i++) {
	printf("reserved2[%d] = %d, ", i, data.reserved2[i]);
    }
    printf("\n");

    printf("\tfilecount = %d\n\tlinkcount = %u\n\tdiskused = %d\n\tdayUse = %d\n\tdayUseDate = %u\n",
	data.filecount, data.linkcount, data.diskused, data.dayUse, data.dayUseDate);
    printf("\t");
    for (i = 0; i < 3; i++) {
	printf("weekUse[%d] = %d, ", i, data.weekUse[i]);
    }
    printf("\n\t");
    for (i = 3; i < 6; i++) {
	printf("weekUse[%d] = %d, ", i, data.weekUse[i]);
    }
    printf("\n\t");
    for (i = 6; i < 7; i++) {
	printf("weekUse[%d] = %d, ", i, data.weekUse[i]);
    }
    printf("\n");

    printf("\t");
    for(i = 0; i < 3; i++) {
	printf("reserved3[%d] = %d, ", i, data.reserved3[i]);
    }
    printf("\n\t");
    for(i = 3; i < 6; i++) {
	printf("reserved3[%d] = %d, ", i, data.reserved3[i]);
    }
    printf("\n");
    printf("\t");
    for(i = 6; i < 9; i++) {
	printf("reserved3[%d] = %d, ", i, data.reserved3[i]);
    }
    printf("\n\t");
    for(i = 9; i < 11; i++) {
	printf("reserved3[%d] = %d, ", i, data.reserved3[i]);
    }
    printf("\n");

    printf("\tcreationDate = %u\n\taccessDate = %u\n\tupdateDate = %u\n\texpirationDate = %u\n",
	data.creationDate, data.accessDate, data.updateDate, data.expirationDate);
    printf("\tbackupDate = %u\n\tcopyDate = %u\n", data.backupDate, data.copyDate);
    printf("\t");
    for (i = 0; i < 3; i++) {
	printf("reserved4[%d] = %d, ", i, data.reserved4[i]);
    }
    printf("\n\t");
    for (i = 3; i < 6; i++) {
	printf("reserved4[%d] = %d, ", i, data.reserved4[i]);
    }
    printf("\n\t");
    for (i = 6; i < 8; i++) {
	printf("reserved4[%d] = %d, ", i, data.reserved4[i]);
    }
    printf("\n");
    printf("\tofflineMessage = %s\n", data.offlineMessage);
    printf("\tmotd = %s\n\n", data.motd);
    return 0;
}

int setIndex(char *args) {
    char vnodeType[10], *p = args;

    *vnodeType = 0;
    while (*vnodeType == 0)
	strarg(&p, " ", "Large or Small?", DefaultSize, vnodeType);

    /* Case insensitize the input. */
    for (int i = 0; i < 10; i++) 
	if ((vnodeType[i] >= 'A') && (vnodeType[i] <= 'Z'))
	    vnodeType[i] = (vnodeType[i] - 'A') + 'a';

    if ((strcmp(vnodeType, "large") != 0) && (strcmp(vnodeType, "small") != 0)) {
	printf("Index must be either large or small\n");
	return(-1);
    }

    if (vnodeType[0] == 'l') { /* Probably will pick small next time. */
	strcpy(DefaultSize, "small");
    }
    
    Rewind(0);
    struct DumpHeader head;
    if (DumpStream->getDumpHeader(&head) == 0) {
	printf("DumpHeader is not valid.\n");
	return (-1);
    }

    /* skip over the volumediskdata */
    VolumeDiskData vol;
    if (DumpStream->getVolDiskData(&vol) != 0) {
	printf("VolumeDiskData is not valid.\n");
	return (-1);
    }

    /* Large index */
    long nvnodes, nslots;
    if (DumpStream->getVnodeIndex(vLarge, &nvnodes, &nslots) == -1) {
	printf("Large Index header is not valid.\n");
	return (-1);
    }

    if (vnodeType[0] == 's') {
	/* Skip large vnodes to get to small vnode index. */
	char buf[SIZEOF_LARGEDISKVNODE];
	VnodeDiskObject *vnode = (VnodeDiskObject *)buf;
	long vn, offset;
	int del, count = 0;

	while (DumpStream->getNextVnode(vnode, &vn, &del, &offset) != -1) {
	    count++;
	}

	assert(nvnodes >= count); /* May have less if dump is incremental */
	
	/* We should have read all the Large Vnodes now, get the small index */
	if (DumpStream->getVnodeIndex(vSmall, &nvnodes, &nslots) == -1) {
	    printf("Small Index header is not valid.\n");
	    return (-1);
	}
    }

    printf("There are %d vnodes in %d slots.\n", nvnodes, nslots);
    return (0);
}

int skipVnodes(char *args) {
    if (!Open) {
	printf("No DumpFile open yet!\n");
	return(-1);
    }

    char num[10], *p = args;
    *num = 0;
    while (*num == 0)
	strarg(&p, " ", "Number of Vnodes to skip?", " ", num);

    int n = atoi(num);

    char buf[SIZEOF_LARGEDISKVNODE];
    VnodeDiskObject *vnode = (VnodeDiskObject *)buf;
    long vnum, offset;
    int del;
    
    for (int i = 0; i < n; i++) {
	if (DumpStream->getNextVnode(vnode, &vnum, &del, &offset)) {
	    printf("Not positioned at a vnode, perhaps no more vNodes\n");
	    return(-1);
	}
    }

    if (del) {
	printf("Vnode 0x%#08x at offset %d was deleted.\n", vnum, offset);
	return 0;
    }
    
    printf("Vnode %#8x is at offset %d in the dump.\n", vnum, offset);
    if (vnode->type == vNull && vnode->linkCount == 0)
	return(-1);
    printf("\ttype = %u\n\tcloned = %u\n\tmode = %o\n\tlinks = %u\n",
	vnode->type, vnode->cloned, vnode->modeBits, vnode->linkCount);
    printf("\tlength = %u\n\tunique = %u\n\tversion = %u\n\tinode = %u\n",
	vnode->length, vnode->uniquifier, vnode->dataVersion, vnode->inodeNumber);
    PrintVersionVector(&vnode->versionvector, "\t");
    printf("\tvolindex = %d\n\tmodtime = %u\n\tauthor = %u\n\towner = %u\n\tparent = %x.%x\n",
	vnode->vol_index, vnode->unixModifyTime, vnode->author, vnode->owner, vnode->vparent, vnode->uparent);
    printf("\tmagic = %x\n\tservermodtime = %u\n",
	vnode->vnodeMagic, vnode->serverModifyTime);
    return 0;
}    

    
int showVnodeDiskObject(char *args)
{
    if (!Open) {
	printf("No DumpFile open yet!\n");
	return(-1);
    }

    char buf[SIZEOF_LARGEDISKVNODE];
    VnodeDiskObject *vnode = (VnodeDiskObject *)buf;
    long vnum, offset;
    int del;
    if (DumpStream->getNextVnode(vnode, &vnum, &del, &offset)) {
	printf("Not positioned at a vnode, perhaps no more vNodes\n");
	return(-1);
    }

    if (del) {
	printf("Vnode 0x%#08x at offset %d was deleted.\n", vnum, offset);
	return 0;
    }
    
    printf("Vnode %#8x is at offset %d in the dump.\n", vnum, offset);
    if (vnode->type == vNull && vnode->linkCount == 0)
	return(-1);
    printf("\ttype = %u\n\tcloned = %u\n\tmode = %o\n\tlinks = %u\n",
	vnode->type, vnode->cloned, vnode->modeBits, vnode->linkCount);
    printf("\tlength = %u\n\tunique = %u\n\tversion = %u\n\tinode = %u\n",
	vnode->length, vnode->uniquifier, vnode->dataVersion, vnode->inodeNumber);
    PrintVersionVector(&vnode->versionvector, "\t");
    printf("\tvolindex = %d\n\tmodtime = %u\n\tauthor = %u\n\towner = %u\n\tparent = %x.%x\n",
	vnode->vol_index, vnode->unixModifyTime, vnode->author, vnode->owner, vnode->vparent, vnode->uparent);
    printf("\tmagic = %x\n\tservermodtime = %u\n",
	vnode->vnodeMagic, vnode->serverModifyTime);
    return 0;
}


void main(int argc, char **argv) {
    if (argc > 2) {
	printf("Usage: %s <dumpfile>\n", argv[0]);
	exit(-1);
    }

    if (argc > 1)	/* Use any argument as the default dumpfile. */
	strncpy(DefaultDumpFile, argv[1], sizeof(DefaultDumpFile));
    
    strcpy(DefaultSize, "Large");
    ci("dump>", 0, 0, (CIENTRY *)CiList, 0, 0);
}
    
