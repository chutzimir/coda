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
 * Revision 1.2  1996/01/02 16:57:15  bnoble
 * Added support for Coda MiniCache and raw inode calls (final commit)
 *
 * Revision 1.1.2.1  1995/12/20 01:57:42  bnoble
 * Added CFS-specific files
 *
 * Revision 3.1.1.1  1995/03/04  19:08:20  bnoble
 * Branch for NetBSD port revisions
 *
 * Revision 3.1  1995/03/04  19:08:20  bnoble
 * Bump to major revision 3 to prepare for NetBSD port
 *
 * Revision 2.1  1994/07/21  16:25:25  satya
 * Conversion to C++ 3.0; start of Coda Release 2.0
 *
 * Revision 1.3  94/06/14  16:53:47  dcs
 * Added support for ODY-like mounting in the kernel (SETS)
 * 
 * Revision 1.3  94/06/14  16:48:03  dcs
 * Added support for ODY-like mounting in the kernel (SETS)
 * 
 * Revision 1.2  92/10/27  17:58:28  lily
 * merge kernel/latest and alpha/src/cfs
 * 
 * Revision 1.1  92/04/03  17:35:34  satya
 * Initial revision
 * 
 * Revision 1.5  91/02/09  12:53:26  jjk
 * Substituted rvb's history blurb so that we agree with Mach 2.5 sources.
 * 
 * Revision 2.2.1.1  91/01/06  22:08:22  rvb
 * 	Created for the Coda File System.
 * 	[90/05/23            dcs]
 * 
 * Revision 1.3  90/07/19  10:23:05  dcs
 * Added ; to cfs_resize definition for port to 386.
 * 
 * Revision 1.2  90/05/31  17:02:09  dcs
 * Prepare for merge with facilities kernel.
 * 
 * 
 * 
 */

#ifndef _CFSIO_H_
#define _CFSIO_H_

/* Define ioctl commands for vcioctl, /dev/cfs */

#ifdef __STDC__
#define CFSRESIZE    _IOW('c', 1, struct cfs_resize )  /* Resize CFS NameCache */
#define CFSSTATS      _IO('c', 2)                      /* Collect stats */
#define CFSPRINT      _IO('c', 3)                      /* Print Cache */
#define CFSTEST       _IO('c', 4)                      /* Print Cache */
#define ODYBIND	     _IOW('c', 5, struct ody_bind )    /* Bind a name to a FS */
#else /* sys/ioctl.h puts the quotes on */
#define CFSRESIZE    _IOW(c, 1, struct cfs_resize )  /* Resize CFS NameCache */
#define CFSSTATS      _IO(c, 2)                      /* Collect stats */
#define CFSPRINT      _IO(c, 3)                      /* Print Cache */
#define CFSTEST       _IO(c, 4)                      /* Print Cache */
#define ODYBIND	     _IOW(c, 5, struct ody_bind )    /* Bind a name to a FS */
#endif __STDC__


struct cfs_resize { int hashsize, heapsize; };
struct ody_bind { int size; char *name; };

#endif !_CFSIO_H_
