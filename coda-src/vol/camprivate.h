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

static char *rcsid = "/afs/cs/project/coda-rvb/cvs/src/coda-4.0.1/coda-src/vol/camprivate.h,v 1.1.1.1 1996/11/22 19:10:04 rvb Exp";
#endif /*_BLURB_*/








#ifndef _CAMPRIVATE_H_
#define _CAMPRIVATE_H_ 1
#include "rec_smolist.h"
/* Declarations private to the camelot part of the volume package */

/*
  BEGIN_HTML
   <a name="VolumeData"> <strong>Volume meta data in RVM </strong></a>
   END_HTML
*/

/* Used to be part of struct VolumeHeader, now private to camelot storage */
struct VolumeData {
    VolumeDiskData	*volumeInfo;	/* pointer to VolumeDiskData structure */
    rec_smolist *smallVnodeLists;	/* pointer to array of Vnode list ptrs */
    bit32	    nsmallvnodes;	/* number of alloced vnodes */
    bit32	    nsmallLists;	/* number of vnode lists */
    rec_smolist *largeVnodeLists;	/* pointer to array of Vnode list ptrs */
    bit32	    nlargevnodes;	/* number of alloced vnodes */
    bit32	    nlargeLists;	/* number of vnode lists */
    /* removed fields for VolumeAcl and volumeMountTable (obsolete) */
    bit32	reserved[10];	/* If you add fields, add them before
    				   here and reduce the size of this array */
};

/* Top level of camelot recoverable storage structures */
struct VolHead {
    struct VolumeHeader header;
    struct VolumeData	data;
};

#endif _CAMPRIVATE_H_
