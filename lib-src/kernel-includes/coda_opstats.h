
/*
 * Operation statistics for Coda.
 * Copyright (C) 1997 Carnegie Mellon University
 * 
 * Carnegie Mellon University encourages users of this software
 * to contribute improvements to the Coda project. Contact Peter Braam
 * <coda@coda.cs.cmu.edu>.
 */



#define CODA_MOUNT_STATS  0
#define CODA_UMOUNT_STATS 1
#define CODA_ROOT_STATS   2
#define CODA_STATFS_STATS 3
#define CODA_SYNC_STATS   4
#define CODA_VGET_STATS   5
#define CODA_VFSOPS_SIZE  6

/* vnodeops:
 *            open: all to venus
 *            close: all to venus
 *            rdrw: bogus.  Maybe redirected to UFS.
 *                          May call open/close for internal opens/closes
 *                          (Does exec not call open?)
 *            ioctl: causes a lookupname
 *                   passes through
 *            select: can't get there from here.
 *            getattr: can be satsified by cache
 *            setattr: all go through
 *            access: can be satisfied by cache
 *            readlink: can be satisfied by cache
 *            fsync: passes through
 *            inactive: passes through
 *            lookup: can be satisfied by cache
 *            create: passes through
 *            remove: passes through
 *            link: passes through
 *            rename: passes through
 *            mkdir: passes through
 *            rmdir: passes through
 *            symlink: passes through
 *            readdir: may be redirected to UFS
 *                     may cause an "internal" open/close
 */

#define CODA_OPEN_STATS     0
#define CODA_CLOSE_STATS    1
#define CODA_RDWR_STATS     2
#define CODA_IOCTL_STATS    3
#define CODA_SELECT_STATS   4
#define CODA_GETATTR_STATS  5
#define CODA_SETATTR_STATS  6
#define CODA_ACCESS_STATS   7
#define CODA_READLINK_STATS 8
#define CODA_FSYNC_STATS    9
#define CODA_INACTIVE_STATS 10
#define CODA_LOOKUP_STATS   11
#define CODA_CREATE_STATS   12
#define CODA_REMOVE_STATS   13
#define CODA_LINK_STATS     14
#define CODA_RENAME_STATS   15
#define CODA_MKDIR_STATS    16
#define CODA_RMDIR_STATS    17
#define CODA_SYMLINK_STATS  18
#define CODA_READDIR_STATS  19
#define CODA_VNODEOPS_SIZE  20


/*
 * I propose the following structres:
 */


struct coda_op_stats {
    int opcode;       /* vfs opcode */
    long entries;     /* number of times call attempted */
    long sat_intrn;   /* number of times call satisfied by cache */
    long unsat_intrn; /* number of times call failed in cache, but
                         was not bounced to venus proper. */
    long gen_intrn;   /* number of times call generated internally */
                      /* (do we need that?) */
};


/*
 * With each call to the minicache, we'll bump the counters whenver
 * a call is satisfied internally (through the cache or through a
 * redirect), and whenever an operation is caused internally.
 * Then, we can add the total operations caught by the minicache
 * to the world-wide totals, and leave a caveat for the specific
 * graphs later.
 */
