/* 
 * Mach Operating System
 * Copyright (c) 1990 Carnegie-Mellon University
 * Copyright (c) 1989 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */

/*
 * This code was written for the Coda file system at Carnegie Mellon
 * University.  Contributers include David Steere, James Kistler, and
 * M. Satyanarayanan.  
 */

/*
 * HISTORY
 * $Log$
 * Revision 1.2.34.1  1997/11/13 22:03:04  rvb
 * pass2 cfs_NetBSD.h mt
 *
 * Revision 1.2  96/01/02  16:57:14  bnoble
 * Added support for Coda MiniCache and raw inode calls (final commit)
 * 
 * Revision 1.1.2.1  1995/12/20 01:57:40  bnoble
 * Added CFS-specific files
 *
 */

enum vcexcl	{ NONEXCL, EXCL};		/* (non)excl create (create) */


extern int
cfs_open(struct vnode **, int,
	      struct ucred *, struct proc *);

extern int
cfs_close(struct vnode *, int,
	       struct ucred *, struct proc *);

extern int
cfs_rdwr(struct vnode *, struct uio *, enum uio_rw, int,
	      struct ucred *, struct proc *);

extern int
cfs_ioctl(struct vnode *, int, caddr_t, int,
	       struct ucred *, struct proc  *);

extern int
cfs_select(struct vnode *, int,
		struct ucred *, struct proc *);
		
extern int
cfs_getattr(struct vnode *, struct vattr *,
		 struct ucred *, struct proc *);

extern int
cfs_setattr(struct vnode *, struct vattr *,
		 struct ucred *, struct proc *);

extern int
cfs_access(struct vnode *, int,
		struct ucred *, struct proc *);
		
extern int
cfs_readlink(struct vnode *, struct uio *,
		  struct ucred *, struct proc *);

extern int
cfs_fsync(struct vnode *,
	       struct ucred *, struct proc *);

extern int
cfs_inactive(struct vnode *,
		  struct ucred *, struct proc *);

extern int
cfs_lookup(struct vnode *, char *, struct vnode **,
		struct ucred *, struct proc *);

extern int
cfs_create(struct vnode *, char *, struct vattr *, enum vcexcl,
		int, struct vnode **,
		struct ucred *, struct proc *);

extern int
cfs_remove(struct vnode *, char *,
		struct ucred *, struct proc *);

extern int
cfs_link(struct vnode *, struct vnode *, char *,
	      struct ucred *, struct proc *);

extern int
cfs_rename(struct vnode *, char *, struct vnode *, char *,
		struct ucred *, struct proc *);

extern int
cfs_mkdir(struct vnode *, char *, register struct vattr *, struct vnode **,
	       struct ucred *, struct proc *);

extern int
cfs_rmdir(struct vnode *, char *,
	       struct ucred *, struct proc *);

extern int
cfs_symlink(struct vnode *, char *, struct vattr *, char *,
		 struct ucred *, struct proc *);

extern int
cfs_readdir(struct vnode *, register struct uio *, struct ucred *, int *,
		 u_long *, int,
		 struct proc *);

extern int
cfs_bmap(struct vnode *, daddr_t, struct vnode **,
	      daddr_t *, struct proc *);

extern int
cfs_strategy(struct buf *,
		  struct proc *);

