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

static char *rcsid = "/afs/cs/project/coda-rvb/cvs/src/coda-4.0.1/coda-src/volutil/vol-dumprecstore.h,v 1.1.1.1 1996/11/22 19:13:36 rvb Exp";
#endif /*_BLURB_*/






/*
 * vol-dumprecstore.h
 * Created April, 1990
 * Author - Puneet Kumar 
 */

/*
 * Types for all of recoverable storage 
 */

#define	VOLHEADT	0
#define	SVNODEPTRARRT	1
#define	LVNODEPTRARRT	2
#define	SVNODEDISKPTRT	3
#define	LVNODEDISKPTRT	4
#define	VOLDISKDATAT	5
#define	DIRINODET	6
#define	DIRPAGET	7
#define CAMELOTFREESTORE 8
#define	SFREEVNODEPTRARRT	9
#define	LFREEVNODEPTRARRT	10
#define	SFREEVNODEDISKPTRT	11
#define	LFREEVNODEDISKPTRT	12

typedef struct dumprec_t {
    char    *rec_addr;
    int	    size;
    short   type;
    short   index;
} dumprec_t;

