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







#ifndef _VICE_VRDB_H_
#define	_VICE_VRDB_H_	1

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <stdio.h>

#include <vcrcommon.h>
#include <vice.h>
#ifdef __cplusplus
}
#endif __cplusplus

#include <ohash.h>
#include <inconsist.h>


#define VRTABHASHSIZE	128

class vrtab;
class vrtab_iterator;
class vrent;


class vrtab : public ohashtab {
    friend void PrintVRDB();
    char *name;
    ohashtab namehtb;
  public:
    vrtab(char * ="anonymous vrtab");
    ~vrtab();
    void add(vrent *);
    void remove(vrent *);
    vrent *find(VolumeId);
    vrent *find(char *);
    vrent *ReverseFind(VolumeId);
    void clear();
    void print();
    void print(FILE *);
    void print(int);
};


/* each vrent is in 2 hash tables; lookup by replicated id and by volume name */
class vrent : public olink {
  public:   /* made public (temporarily?) to avoid multiple header include problems */
    char key[33];
    VolumeId volnum;
    olink	namehtblink;
    /*byte*/unsigned char nServers;
    VolumeId ServerVolnum[VSG_MEMBERS];
    unsigned long addr;

    vrent();
    vrent(vrent&);
    operator=(vrent&);	    /* not supported! */
    ~vrent();

//  public:
    void GetHosts(unsigned long *);
    int index(unsigned long);
    void HostListToVV(unsigned long *, vv_t *);
    int GetVolumeInfo(VolumeInfo *);
    void Canonicalize();	// note that the function should not
				// be confused with the global var.
    void hton();
    void ntoh();
    void print();
    void print(FILE *);
    void print(int);
};


extern const char *VRDB_PATH;
extern const char *VRDB_TEMP;
extern vrtab VRDB;
extern void CheckVRDB();
extern int XlateVid(VolumeId *);
extern int XlateVid(VolumeId *, int *, int *);
extern int ReverseXlateVid(VolumeId *);
extern unsigned long XlateVidToVSG(VolumeId);

#endif	not _VICE_VRDB_H_

