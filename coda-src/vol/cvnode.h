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








/********************************
 * cvnode.h			*
 ********************************/

#ifndef _CVNODE_H_
#define _CVNODE_H_ 1

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <lwp.h>
#include <lock.h>

#ifdef __cplusplus
}
#endif __cplusplus

#include <prs.h>
#include <al.h>
#include <nfs.h>
#include <errors.h>
#include <inconsist.h>
#include <rec_smolist.h>
#include <rec_dlist.h>
#include "vicelock.h"

#define ROOTVNODE 1

typedef int VnodeType;

/* more lock types -- leave small #s for lock.h */
#define TRY_READ_LOCK	16
#define TRY_WRITE_LOCK	32

/*typedef enum {vNull=0, vFile=1, vDirectory=2, vSymlink=3} VnodeType;*/
#define vNull 0
#define vFile 1
#define vDirectory 2
#define vSymlink 3

/*typedef enum {vLarge=0,vSmall=1} VnodeClass;*/
#define vLarge	0
#define vSmall	1

typedef int VnodeClass;
#define VNODECLASSWIDTH 1
#define VNODECLASSMASK	((1<<VNODECLASSWIDTH)-1)
#define nVNODECLASSES	(VNODECLASSMASK+1)

struct VnodeClassInfo {
    struct Vnode *lruHead;	/* Head of list of vnodes of this class */
    int diskSize;		/* size of vnode disk object, power of 2 */
    int logSize;		/* log 2 diskSize */
    int residentSize;		/* resident size of vnode */
    int cacheSize;		/* Vnode cache size */
    bit32 magic;		/* Magic number for this type of vnode,
    				   for as long as we're using vnode magic
				   numbers */
    int	allocs;			/* Total number of successful allocation
    				   requests; this is the same as the number
				   of sanity checks on the vnode index */
    int gets,reads;		/* Number of VGetVnodes and corresponding
    				   reads */
    int writes;			/* Number of vnode writes */
};

extern struct VnodeClassInfo VnodeClassInfo_Array[nVNODECLASSES];

#define vnodeTypeToClass(type)  ((type) == vDirectory? vLarge: vSmall)
#define vnodeIdToClass(vnodeId) ((vnodeId-1)&VNODECLASSMASK)
#define vnodeIdToBitNumber(v) (((v)-1)>>VNODECLASSWIDTH)
/* The following calculation allows for a header record at the beginning
   of the index.  The header record is the same size as a vnode */
/*
 * #define vnodeIndexOffset(vcp,vnodeNumber) \
 *    ((vnodeIdToBitNumber(vnodeNumber)+1)<<(vcp)->logSize)
 */
#define bitNumberToVnodeNumber(b,vclass) (((b)<<VNODECLASSWIDTH)+(vclass)+1)
#define vnodeIsDirectory(vnodeNumber) (vnodeIdToClass(vnodeNumber) == vLarge)

/* VnodeDiskObject: Structure of vnode stored in RVM */
typedef struct VnodeDiskObjectStruct {
    VnodeType	  type:3;	/* Vnode is file, directory, symbolic link
    				   or not allocated */
    unsigned	  cloned:1;	/* This vnode was cloned--therefore the inode
    				   is copy-on-write; only set for directories*/
    unsigned	  modeBits:12;	/* Unix mode bits */
    bit16	  linkCount;	/* Number of directory references to vnode
    				   (from single directory only!) */
    bit32	  length;	/* Number of bytes in this file */
    Unique_t	  uniquifier;	/* Uniquifier for the vnode; assigned
				   from the volume uniquifier (actually
				   from nextVnodeUnique in the Volume
				   structure) */
    FileVersion   dataVersion;	/* version number of the data */
#define	NEWVNODEINODE -1	/* inode number for a vnode allocated 
                                   but not used for creation */
    Inode	  inodeNumber;	/* inode number of the data attached to
    				   this vnode */
    /* version vector is updated atomically with the data */
    vv_t	  versionvector;/* CODA file version vector for this vnode */
    int		  vol_index;	/* index of vnode's volume in recoverable volume array */
    Date_t	  unixModifyTime;/* set by user */
    UserId	  author;	/* Userid of the last user storing the file */
    UserId	  owner;	/* Userid of the user who created the file */
    VnodeId	  vparent;	/* Parent directory vnode */
    Unique_t	  uparent;	/* Parent directory uniquifier */
    bit32	  vnodeMagic;	/* Magic number--mainly for file server
    				   paranoia checks */
#define	  SMALLVNODEMAGIC	0xda8c041F
#define	  LARGEVNODEMAGIC	0xad8765fe
    /* Vnode magic can be removed, someday, if we run need the room.  Simply
       have to be sure that the thing we replace can be VNODEMAGIC, rather
       than 0 (in an old file system).  Or go through and zero the fields,
       when we notice a version change (the index version number) */
    ViceLock	  lock;		/* Advisory lock */
    Date_t	  serverModifyTime;	/* Used only by the server;
					   for incremental backup purposes */
    rec_smolink	  nextvn;	/* link to next vnode with same vnodeindex */
    rec_dlist	  *log;		/* resolution log in RVM */
    /* Missing:
       archiving/migration
       encryption key
     */
} VnodeDiskObject;

#define SIZEOF_SMALLDISKVNODE	112	/* used to be 64 */
#define CHECKSIZE_SMALLVNODE\
	(sizeof(VnodeDiskObject) == SIZEOF_SMALLDISKVNODE)
/* must be a power of 2! */
#define SIZEOF_LARGEDISKVNODE	512	/* used to be 256 */

typedef struct Vnode {
    struct	Vnode *hashNext;/* Next vnode on hash conflict chain */
    struct	Vnode *lruNext;	/* Less recently used vnode than this one */
    struct	Vnode *lruPrev;	/* More recently used vnode than this one */
				/* The lruNext, lruPrev fields are not
				   meaningful if the vnode is in use */
    bit16	hashIndex;	/* Hash table index */
    unsigned short changed:1;	/* 1 if the vnode has been changed */
    unsigned short delete_me:1;	/* 1 if the vnode should be deleted; in
    				 this case, changed must also be 1 */
    VnodeId	vnodeNumber;
    struct Volume
		*volumePtr;	/* Pointer to the volume containing this file*/
    byte	nUsers;		/* Number of lwp's who have done a VGetVnode */
    bit16	cacheCheck;	/* Must equal the value in the volume Header
    				   for the cache entry to be valid */
    struct	Lock lock;	/* Internal lock */
    PROCESS	writer;		/* Process id having write lock */
    VnodeDiskObject disk;	/* The actual disk data for the vnode */
} Vnode;

#define Vnode_vv(vptr)		((vptr)->disk.versionvector)

#define SIZEOF_LARGEVNODE \
	(sizeof(struct Vnode) - sizeof(VnodeDiskObject) + SIZEOF_LARGEDISKVNODE)
#define SIZEOF_SMALLVNODE	(sizeof (struct Vnode))

#define VVnodeDiskACL(v)     /* Only call this with large (dir) vnode!! */ \
	((AL_AccessList *) (((byte *)(v))+SIZEOF_SMALLDISKVNODE))
#define  VVnodeACL(vnp) (VVnodeDiskACL(&(vnp)->disk))
/* VAclSize is defined this way to allow information in the vnode header
   to grow, in a POSSIBLY upward compatible manner.  SIZEOF_SMALLDISKVNODE
   is the maximum size of the basic vnode.  The vnode header of either type
   can actually grow to this size without conflicting with the ACL on larger
   vnodes */
#define VAclSize(vnp)		(SIZEOF_LARGEDISKVNODE - SIZEOF_SMALLDISKVNODE)
#define VAclDiskSize(v)		(SIZEOF_LARGEDISKVNODE - SIZEOF_SMALLDISKVNODE)
#define VnLog(vnp)		((vnp)->disk.log)

extern int VolumeHashOffset();
extern void VInitVnodes(VnodeClass, int);
extern Vnode *VGetVnode(Error *, Volume *, VnodeId, Unique_t, int, int, int =0);
extern void VPutVnode(Error *ec, register Vnode *vnp);
extern void VFlushVnode(Error *, Vnode *);
extern int VAllocFid(Volume *vp, VnodeType type,
		      ViceFidRange *Range, int stride =1, int ix =0);
extern int VAllocFid(Volume *vp, VnodeType type, VnodeId vnode, Unique_t unique);
extern Vnode *VAllocVnode(Error *ec, Volume *vp, VnodeType type,
			   int stride =1, int ix =0);
extern Vnode *VAllocVnode(Error *ec, Volume *vp, VnodeType type,
			   VnodeId vnode, Unique_t unique);
extern int ObjectExists(int, int, VnodeId, Unique_t, ViceFid * =NULL);
#endif _CVNODE_H_

