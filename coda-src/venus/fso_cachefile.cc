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
 *    Cache file management
 */

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef __cplusplus
}
#endif __cplusplus

/* interfaces */
#include <vice.h>

/* from venus */
#include "fso.h"
#include "venus.private.h"


/*  *****  CacheFile Members  *****  */

/* Pre-allocation routine. */
/* MUST be called from within transaction! */
CacheFile::CacheFile(int i) {
    CODA_ASSERT(this	!= 0);

    /* Assume caller has done RVMLIB_REC_OBJECT! */
/*    RVMLIB_REC_OBJECT(*this);*/
    sprintf(name, "V%d", i);
    inode = (ino_t)-1;
    length = 0;
    /* Container reset will be done by eventually by FSOInit()! */
}


CacheFile::CacheFile() {
    CODA_ASSERT(inode != (ino_t)-1 && length == 0);
}


CacheFile::~CacheFile() {
    CODA_ASSERT(inode != (ino_t)-1 && length == 0);
}


/* MUST NOT be called from within transaction! */
void CacheFile::Validate() {
    if (!ValidContainer())
	ResetContainer();
}


/* MUST NOT be called from within transaction! */
void CacheFile::Reset() {
    if (inode == (ino_t)-1 || length != 0 || !ValidContainer())
	ResetContainer();
}


int CacheFile::ValidContainer() {
    int code = 0;
    struct stat tstat;
    int valid = (code = ::stat(name, &tstat)) == 0 &&
      tstat.st_uid == (uid_t)V_UID &&
      tstat.st_gid == (gid_t)V_GID &&
      (tstat.st_mode & ~S_IFMT) == V_MODE &&
      tstat.st_ino == inode &&
      tstat.st_size == length;

    if (!valid && LogLevel >= 10) {
	dprint("CacheFile::ValidContainer: %s invalid\n", name);
	if (code == 0)
	    dprint("\t(%u, %u), (%u, %u), (%o, %o), (%d, %d), (%d, %d)\n",
		   tstat.st_uid, (uid_t)V_UID, tstat.st_gid, (gid_t)V_GID,
		   (tstat.st_mode & ~S_IFMT), V_MODE,
		   tstat.st_ino, inode, tstat.st_size, length);
	else
	    dprint("\tstat failed (%d)\n", errno);
    }

    return(valid);
}


/* MUST NOT be called from within transaction! */
void CacheFile::ResetContainer() {
    LOG(10, ("CacheFile::ResetContainer: %s, %d, %d\n",
	      name, inode, length));

    int tfd;
    struct stat tstat;
    if ((tfd = ::open(name, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, V_MODE)) < 0)
	CHOKE("CacheFile::ResetContainer: open failed (%d)", errno);
#ifndef DJGPP
    if (::fchmod(tfd, V_MODE) < 0)
	CHOKE("CacheFile::ResetContainer: fchmod failed (%d)", errno);

#ifndef __CYGWIN32__
    if (::fchown(tfd, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::ResetContainer: fchown failed (%d)", errno);
#else
    if (::chown(name, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::ResetContainer: fchown failed (%d)", errno);
#endif
#endif
    if (::fstat(tfd, &tstat) < 0)
	CHOKE("CacheFile::ResetContainer: fstat failed (%d)", errno);
    if (::close(tfd) < 0)
	CHOKE("CacheFile::ResetContainer: close failed (%d)", errno);

    Recov_BeginTrans();
    RVMLIB_REC_OBJECT(*this);
    inode = tstat.st_ino;
    length = 0;
    Recov_EndTrans(MAXFP);
}


/*
 * moves a cache file to a new one.  destination's name already
 * there, created by cache file constructor.
 */  
void CacheFile::Move(CacheFile *destination) {
    LOG(10, ("CacheFile::Move: source: %s, %d, %d, dest: %s\n",
	      name, inode, length, destination->name));

    destination->inode = inode;
    destination->length = length;
    if (::rename(name, destination->name) != 0)
        CHOKE("CacheFile::RenameContainer: rename failed (%d)", errno);
}


/* 
 * copies a cache file, data and attributes, to a new one.  
 */
void CacheFile::Copy(CacheFile *source) {
    LOG(10, ("CacheFile::Copy: %s, %d, %d\n",
	      name, inode, length));

    int tfd, ffd, n;
    struct stat tstat;
    char buf[DIR_PAGESIZE];

    if ((tfd = ::open(name, O_RDWR | O_CREAT | O_TRUNC| O_BINARY, V_MODE)) < 0)
	CHOKE("CacheFile::Copy: open failed (%d)", errno);
#ifndef DJGPP
    if (::fchmod(tfd, V_MODE) < 0)
	CHOKE("CacheFile::Copy: fchmod failed (%d)", errno);
#ifndef __CYGWIN32__
    if (::fchown(tfd, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::Copy: fchown failed (%d)", errno);
#else
    if (::chown(name, (uid_t)V_UID, (gid_t)V_GID) < 0)
	CHOKE("CacheFile::ResetCopy: fchown failed (%d)", errno);
#endif
#endif
    if ((ffd = ::open(source->name, O_RDONLY| O_BINARY, V_MODE)) < 0)
	CHOKE("CacheFile::Copy: source open failed (%d)", errno);

    for (;;) {
        n = ::read(ffd, buf, (int) sizeof(buf));
        if (n == 0)
	    break;
        if (n < 0)
	    CHOKE("CacheFile::Copy: read failed! (%d)", errno);
	if (::write(tfd, buf, n) != n)
	    CHOKE("CacheFile::Copy: write failed! (%d)", errno);
    }
    if (::fstat(tfd, &tstat) < 0)
	CHOKE("CacheFile::Copy: fstat failed (%d)", errno);
    if (::close(tfd) < 0)
	CHOKE("CacheFile::Copy: close failed (%d)", errno);
    if (::close(ffd) < 0)
	CHOKE("CacheFile::Copy: source close failed (%d)", errno);
    
    CODA_ASSERT(source->length == tstat.st_size);

    inode = tstat.st_ino;
    length = source->length;
}


void CacheFile::Remove() {
    length = 0;
    if (::unlink(name) < 0)
        CHOKE("CacheFile::Remove: unlink failed (%d)", errno);
}


/* N.B. length member is NOT updated as side-effect! */
void CacheFile::Stat(struct stat *tstat) {
    CODA_ASSERT(inode != (ino_t)-1);

    CODA_ASSERT(::stat(name, tstat) == 0);
#if ! defined(DJGPP) && ! defined(__CYGWIN32__)
    CODA_ASSERT(tstat->st_ino == inode);
#endif
}


/* MUST be called from within transaction! */
void CacheFile::Truncate(unsigned newlen) {
    int fd;

    CODA_ASSERT(inode != (ino_t)-1);

    /*
    if (length < newlen) {
       eprint("Truncate: %d->%d:  -> ::truncate(name %s, length %d)\n",
		length, newlen, name, newlen);
    }
    */
    if (length != newlen) {
	RVMLIB_REC_OBJECT(*this);
	length = newlen;
    }
#ifndef __CYGWIN32__
    CODA_ASSERT(::truncate(name, length) == 0);
#else 
    fd = open(name, O_RDWR);
    if ( fd < 0 )
	    CODA_ASSERT(0);
    CODA_ASSERT(::ftruncate(fd, length) == 0);
    close(fd);
#endif
}


/* MUST be called from within transaction! */
void CacheFile::SetLength(unsigned newlen) {
    CODA_ASSERT(inode != (ino_t)-1);

    if (length != newlen) {
	 RVMLIB_REC_OBJECT(*this);
	length = newlen;
    }
}


void CacheFile::print(int fdes) {
    fdprint(fdes, "[ %s, %d, %d ]\n", name, inode, length);
}

