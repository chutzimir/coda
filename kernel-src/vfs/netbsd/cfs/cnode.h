/* 
 * Mach Operating System
 * Copyright (c) 1990 Carnegie-Mellon University
 * Copyright (c) 1989 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */

/*
 * This code was written for the Coda file system at Carnegie Mellon University.
 * Contributers include David Steere, James Kistler, and M. Satyanarayanan.
 */

/* 
 * HISTORY
 * $Log$
 * Revision 1.5  1998/01/23 11:53:51  rvb
 * Bring RVB_CFS1_1 to HEAD
 *
 * Revision 1.4.2.5  98/01/23  11:21:14  rvb
 * Sync with 2.2.5
 * 
 * Revision 1.4.2.4  98/01/22  13:03:38  rvb
 * Had Breaken ls .
 * 
 * Revision 1.4.2.3  97/12/19  14:26:09  rvb
 * session id
 * 
 * Revision 1.4.2.2  97/12/16  12:40:24  rvb
 * Sync with 1.3
 * 
 * Revision 1.4.2.1  97/12/06  17:41:28  rvb
 * Sync with peters coda.h
 * 
 * Revision 1.4  97/12/05  10:39:30  rvb
 * Read CHANGES
 * 
 * Revision 1.3.18.2  97/11/12  12:09:45  rvb
 * reorg pass1
 * 
 * Revision 1.3.18.1  97/10/29  16:06:31  rvb
 * Kill DYING
 * 
 * Revision 1.3  1996/12/12 22:11:03  bnoble
 * Fixed the "downcall invokes venus operation" deadlock in all known cases.  There may be more
 *
 * Revision 1.2  1996/01/02 16:57:26  bnoble
 * Added support for Coda MiniCache and raw inode calls (final commit)
 *
 * Revision 1.1.2.1  1995/12/20 01:57:53  bnoble
 * Added CFS-specific files
 *
 * Revision 3.1.1.1  1995/03/04  19:08:23  bnoble
 * Branch for NetBSD port revisions
 *
 * Revision 3.1  1995/03/04  19:08:23  bnoble
 * Bump to major revision 3 to prepare for NetBSD port
 *
 * Revision 2.2  1994/12/06  13:39:18  dcs
 * Add a flag value to indicate a cnode was orphaned, e.g. the venus
 * that created it has exited. This will allow one to restart venus
 * even though some process may be cd'd into /coda.
 *
 * Revision 2.1  94/07/21  16:25:33  satya
 * Conversion to C++ 3.0; start of Coda Release 2.0
 * 
 * Revision 1.2.7.1  94/06/16  11:26:02  raiff
 * Branch for release beta-16Jun1994_39118
 * 
 * Revision 1.2  92/10/27  17:58:41  lily
 * merge kernel/latest and alpha/src/cfs
 * 
 * Revision 2.3  92/09/30  14:16:53  mja
 * 	Picked up fixed #ifdef _KERNEL. Also...
 * 
 * 	Substituted rvb's history blurb so that we agree with Mach 2.5 sources.
 * 	[91/02/09            jjk]
 * 
 * 	Added contributors blurb.
 * 	[90/12/13            jjk]
 * 
 * Revision 2.2  90/07/05  11:27:24  mrt
 * 	Created for the Coda File System.
 * 	[90/05/23            dcs]
 * 
 * Revision 1.4  90/05/31  17:02:16  dcs
 * Prepare for merge with facilities kernel.
 * 
 * 
 * 
 */

#ifndef	_CNODE_H_
#define	_CNODE_H_

#include <sys/vnode.h>

#ifdef	__FreeBSD__

#define	__DEBUG_FreeBSD__ 1

/* for the prototype of DELAY() */
#include <machine/clock.h>
#endif

/*
 * tmp below since we need struct queue
 */
#include <cfs/cfsk.h>

/*
 * Cnode lookup stuff.
 * NOTE: CFS_CACHESIZE must be a power of 2 for cfshash to work!
 */
#define CFS_CACHESIZE 512

#define CFS_ALLOC(ptr, cast, size)                                        \
do {                                                                      \
    ptr = (cast)malloc((unsigned long) size, M_CFS, M_WAITOK);            \
    if (ptr == 0) {                                                       \
	panic("kernel malloc returns 0 at %s:%d\n", __FILE__, __LINE__);  \
    }                                                                     \
} while (0)

#define CFS_FREE(ptr, size)  free((ptr), M_CFS)

/*
 * global cache state control
 */
extern int cfsnc_use;

/*
 * Used to select debugging statements throughout the cfs code.
 */
extern int cfsdebug;
extern int cfsnc_debug;
extern int cfs_printf_delay;
extern int cfs_vnop_print_entry;
extern int cfs_psdev_print_entry;
extern int cfs_vfsop_print_entry;

#define CFSDBGMSK(N)            (1 << N)
#define CFSDEBUG(N, STMT)       { if (cfsdebug & CFSDBGMSK(N)) { STMT } }
#define myprintf(args)          \
do {                            \
    if (cfs_printf_delay)       \
	DELAY(cfs_printf_delay);\
    printf args ;               \
} while (0)

struct cnode {
    struct vnode	*c_vnode;
    u_short		 c_flags;	/* flags (see below) */
    ViceFid		 c_fid;		/* file handle */
    struct vnode	*c_ovp;		/* open vnode pointer */
    u_short		 c_ocount;	/* count of openers */
    u_short		 c_owrite;	/* count of open for write */
    struct vattr	 c_vattr; 	/* attributes */
    char		*c_symlink;	/* pointer to symbolic link */
    u_short		 c_symlen;	/* length of symbolic link */
    dev_t		 c_device;	/* associated vnode device */
    ino_t		 c_inode;	/* associated vnode inode */
    struct cnode	*c_next;	/* links if on NetBSD machine */
};
#define	VTOC(vp)	((struct cnode *)(vp)->v_data)
#define	CTOV(cp)	((struct vnode *)((cp)->c_vnode))

/* flags */
#define C_VATTR		0x01	/* Validity of vattr in the cnode */
#define C_SYMLINK	0x02	/* Validity of symlink pointer in the Code */
#define C_WANTED	0x08	/* Set if lock wanted */
#define C_LOCKED	0x10	/* Set if lock held */
#define C_UNMOUNTING	0X20	/* Set if unmounting */
#define C_PURGING	0x40	/* Set if purging a fid */

#define VALID_VATTR(cp)		((cp->c_flags) & C_VATTR)
#define VALID_SYMLINK(cp)	((cp->c_flags) & C_SYMLINK)
#define IS_UNMOUNTING(cp)	((cp)->c_flags & C_UNMOUNTING)

struct vcomm {
	u_long		vc_seq;
	struct selinfo	vc_selproc;
	struct queue	vc_requests;
	struct queue	vc_replys;
};

#define	VC_OPEN(vcp)	    ((vcp)->vc_requests.forw != NULL)
#define MARK_VC_CLOSED(vcp) (vcp)->vc_requests.forw = NULL;
#define MARK_VC_OPEN(vcp)    /* MT */

struct cfs_clstat {
	int	ncalls;			/* client requests */
	int	nbadcalls;		/* upcall failures */
	int	reqs[CFS_NCALLS];	/* count of each request */
};
extern struct cfs_clstat cfs_clstat;

/*
 * CFS structure to hold mount/file system information
 */
struct cfs_mntinfo {
    struct vnode	*mi_rootvp;
    struct mount	*mi_vfsp;
    struct vcomm	 mi_vcomm;
};
extern struct cfs_mntinfo cfs_mnttbl[]; /* indexed by minor device number */

/*
 * vfs pointer to mount info
 */
#define vftomi(vfsp)    ((struct cfs_mntinfo *)(vfsp->mnt_data))
#define	CFS_MOUNTED(vfsp)   (vftomi((vfsp)) != (struct cfs_mntinfo *)0)

/*
 * vnode pointer to mount info
 */
#define vtomi(vp)       ((struct cfs_mntinfo *)(vp->v_mount->mnt_data))

/*
 * Used for identifying usage of "Control" object
 */
extern struct vnode *cfs_ctlvp;
#define	IS_CTL_VP(vp)		((vp) == cfs_ctlvp)
#define	IS_CTL_NAME(vp, name, l)((l == CFS_CONTROLLEN) \
 				 && ((vp) == vtomi((vp))->mi_rootvp)    \
				 && strncmp(name, CFS_CONTROL, l) == 0)

/* 
 * An enum to tell us whether something that will remove a reference
 * to a cnode was a downcall or not
 */
enum dc_status {
    IS_DOWNCALL = 6,
    NOT_DOWNCALL = 7
};

/* cfs_psdev.h */
int cfscall(struct cfs_mntinfo *mntinfo, int inSize, int *outSize, caddr_t buffer);

/* cfs_subr.h */
int  handleDownCall(int opcode, union outputArgs *out);
void cfs_unmounting(struct mount *whoIam);
int  cfs_vmflush(struct cnode *cp);

/* cfs_vnodeops.h */
struct cnode *makecfsnode(ViceFid *fid, struct mount *vfsp, short type);
int cfs_vnodeopstats_init(void);


#ifndef	NetBSD1_3
#define __RCSID(x) static char *rcsid = x
#endif

/* sigh */
#define CFS_RDWR ((u_long) 31)

#endif	/* _CNODE_H_ */

