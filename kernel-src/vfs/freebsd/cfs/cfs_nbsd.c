/* 
 * Mach Operating System
 * Copyright (c) 1990 Carnegie-Mellon University
 * Copyright (c) 1989 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */

/*
 * This code was written for the Coda file system at Carnegie Mellon
 * University.  Contributers include David Steere, James Kistler,
 * M. Satyanarayanan, and Brian Noble.  
 */

/* 
 * HISTORY
 * $Log$
 * Revision 1.16.6.4  1997/11/18 10:27:14  rvb
 * cfs_nbsd.c is DEADcvs diff | & more; integrated into cfs_vf/vnops.c; cfs_nb_foo and cfs_foo are joined
 *
 * Revision 1.16.6.3  97/11/13  22:02:58  rvb
 * pass2 cfs_NetBSD.h mt
 * 
 * Revision 1.16.6.2  97/11/12  12:09:37  rvb
 * reorg pass1
 * 
 * Revision 1.16.6.1  97/10/28  23:10:14  rvb
 * >64Meg; venus can be killed!
 * 
 * Revision 1.16  97/07/18  15:28:41  rvb
 * Bigger/Better/Faster than AFS
 * 
 * Revision 1.15  1997/02/19 18:41:39  bnoble
 * Didn't sufficiently unswap the now-unswapped dvp and vp in cfs_nb_link
 *
 * Revision 1.13  1997/02/18 23:46:25  bnoble
 * NetBSD swapped the order of arguments to VOP_LINK between 1.1 and 1.2.
 * This tracks that change.
 *
 * Revision 1.12  1997/02/18 22:23:38  bnoble
 * Rename lockdebug to cfs_lockdebug
 *
 * Revision 1.11  1997/02/13 18:46:14  rvb
 * Name CODA FS for df
 *
 * Revision 1.10  1997/02/12 15:32:05  rvb
 * Make statfs return values like for AFS
 *
 * Revision 1.9  1997/01/30 16:42:02  bnoble
 * Trace version as of SIGCOMM submission.  Minor fix in cfs_nb_open
 *
 * Revision 1.8  1997/01/13 17:11:05  bnoble
 * Coda statfs needs to return something other than -1 for blocks avail. and
 * files available for wabi (and other windowsish) programs to install
 * there correctly.
 *
 * Revision 1.6  1996/12/05 16:20:14  bnoble
 * Minor debugging aids
 *
 * Revision 1.5  1996/11/25 18:25:11  bnoble
 * Added a diagnostic check for cfs_nb_lock
 *
 * Revision 1.4  1996/11/13 04:14:19  bnoble
 * Merging BNOBLE_WORK_6_20_96 into main line
 *
 *
 * Revision 1.3  1996/11/08 18:06:11  bnoble
 * Minor changes in vnode operation signature, VOP_UPDATE signature, and
 * some newly defined bits in the include files.
 *
 * Revision 1.2.8.1  1996/06/26 16:28:26  bnoble
 * Minor bug fixes
 *
 * Revision 1.2  1996/01/02 16:56:52  bnoble
 * Added support for Coda MiniCache and raw inode calls (final commit)
 *
 * Revision 1.1.2.1  1995/12/20 01:57:17  bnoble
 * Added CFS-specific files
 *
 */

int pass_process_info;
