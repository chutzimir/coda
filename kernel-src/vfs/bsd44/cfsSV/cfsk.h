/*
 * Cnode lookup stuff.
 * NOTE: CFS_CACHESIZE must be a power of 2 for cfshash to work!
 */
#define CFS_CACHESIZE 512

/* 
 * getting from one to the other
 */
#define	VTOC(vp)	((struct cnode *)(vp)->v_data)
#define	CTOV(cp)	((struct vnode *)((cp)->c_vnode))

/*************** allocation. */
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
	delay(cfs_printf_delay);\
    printf args ;               \
} while (0)

struct cfs_clstat {
	int	ncalls;			/* client requests */
	int	nbadcalls;		/* upcall failures */
	int	reqs[CFS_NCALLS];	/* count of each request */
};
extern struct cfs_clstat cfs_clstat;


/* WARNING 
 * These macros assume the presence of a process pointer p!
 * And, they're wrong to do so.  Phhht.
 */
#define INIT_IN(in, op, ident) \
	  (in)->opcode = (op); \
	  (in)->pid = p ? p->p_pid : -1; \
          (in)->pgid = p ? p->p_pgid : -1; \
          if (ident != NOCRED) {                              \
	      (in)->cred.cr_uid = ident->cr_uid;              \
	      (in)->cred.cr_gid = ident->cr_gid;              \
          } else {                                            \
	      bzero(&((in)->cred),sizeof(struct coda_cred));  \
	      (in)->cred.cr_uid = -1;                         \
	      (in)->cred.cr_gid = -1;                         \
          }                                                   \

#if	1
	  /* this is ok for now */
#define CNV_V2VV_ATTR(top, fromp)	\
	  *(top) = *((struct coda_vattr *)(fromp))

#define CNV_VV2V_ATTR(top, fromp)	\
	  *(top) = *((struct vattr *)(fromp))
#else
	  /* this is better */
#define CNV_VV2V_ATTR(top, fromp) \
	  CNV_ATTR(top, fromp); \
	  CNV_V_ZERO(top)

#define CNV_V2VV_ATTR(top, fromp) \
	  CNV_ATTR(top, fromp); \
	  CNV_VV_ZERO(top)

#define CNV_ATTR(top, fromp)	\
	do { \
		top->va_type = fromp->va_type; \
		top->va_mode = fromp->va_mode; \
		top->va_nlink = fromp->va_nlink; \
		top->va_uid = fromp->va_uid; \
		top->va_gid = fromp->va_gid; \
		top->va_fsid = fromp->va_fsid; \
		top->va_fileid = fromp->va_fileid; \
		top->va_size = fromp->va_size; \
		top->va_blocksize = fromp->va_blocksize; \
		top->va_atime = fromp->va_atime; \
		top->va_mtime = fromp->va_mtime; \
		top->va_ctime = fromp->va_ctime; \
		top->va_gen = fromp->va_gen; \
		top->va_flags = fromp->va_flags; \
		top->va_rdev = fromp->va_rdev; \
		top->va_bytes = fromp->va_bytes; \
		top->va_filerev = fromp->va_filerev; \
		top->vaflags = fromp->vaflags; \
		top->va_spare = fromp->va_spare; \
	} while (0)

#define CNV_V_ZERO(p)
#define CNV_VV_ZERO(p)
#endif

/* Macros to manipulate the queue */
#ifndef INIT_QUEUE
struct queue {
    struct queue *forw, *back;
};

#define INIT_QUEUE(head)                     \
do {                                         \
    (head).forw = (struct queue *)&(head);   \
    (head).back = (struct queue *)&(head);   \
} while (0)

#define GETNEXT(head) (head).forw

#define EMPTY(head) ((head).forw == &(head))

#define EOQ(el, head) ((struct queue *)(el) == (struct queue *)&(head))
		   
#define INSQUE(el, head)                             \
do {                                                 \
	(el).forw = ((head).back)->forw;             \
	(el).back = (head).back;                     \
	((head).back)->forw = (struct queue *)&(el); \
	(head).back = (struct queue *)&(el);         \
} while (0)

#define REMQUE(el)                         \
do {                                       \
	((el).forw)->back = (el).back;     \
	(el).back->forw = (el).forw;       \
}  while (0)

#endif INIT_QUEUE

	/* get these to cfs_psdev.c */
struct vmsg {
    struct queue vm_chain;
    caddr_t	 vm_data;
    u_short	 vm_flags;
    u_short      vm_inSize;	/* Size is at most 5000 bytes */
    u_short	 vm_outSize;
    u_short	 vm_opcode; 	/* copied from data to save ptr lookup */
    int		 vm_unique;
    caddr_t	 vm_sleep;	/* Not used by Mach. */
};

#define	VM_READ	    1
#define	VM_WRITE    2
#define	VM_INTR	    4

struct vcomm {
	u_long		vc_seq;
	struct selinfo	vc_selproc;
	struct queue	vc_requests;
	struct queue	vc_replys;
};


#define	VC_OPEN(vcp)	    ((vcp)->vc_requests.forw != NULL)
#define MARK_VC_CLOSED(vcp) (vcp)->vc_requests.forw = NULL;
#define MARK_VC_OPEN(vcp)    /* MT */
	/* get these to cfs_psdev.c */
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

/*
 * vnode pointer to mount info
 */
#define vtomi(vp)       ((struct cfs_mntinfo *)(vp->v_mount->mnt_data))

#define	CFS_MOUNTED(vfsp)   (vftomi((vfsp)) != (struct cfs_mntinfo *)0)


/*
 * Used for identifying usage of "Control" object
 */
extern struct vnode *cfs_ctlvp;
#define	IS_CTL_VP(vp)		((vp) == cfs_ctlvp)
#define	IS_CTL_NAME(vp, name, l)(((vp) == vtomi((vp))->mi_rootvp)    \
				 && strncmp(name, CFS_CONTROL, l) == 0)

/* 
 * An enum to tell us whether something that will remove a reference
 * to a cnode was a downcall or not
 */
enum dc_status {
    IS_DOWNCALL = 6,
    NOT_DOWNCALL = 7
};

struct vattr;

/* Prototypes of functions exported within cfs */
extern int handleDownCal(int opcode, union cfs_downcalls *out);
extern struct cnode *makecfsnode(ViceFid *, struct mount *, short);
extern int cfscall(struct cfs_mntinfo *, int , int *, char *);
extern int  cfs_vmflush __P(());
extern void cfs_flush __P((enum dc_status));
extern void cfs_testflush __P(());
extern void cfs_save __P((struct cnode *));
extern int  cfs_vnodeopstats_init __P(());
extern int  cfs_kill __P((struct mount *, enum dc_status dcstat));
extern void cfs_unsave __P((struct cnode *));
extern int  getNewVnode __P((struct vnode **));
extern void print_vattr __P((struct vattr *));
extern void cfs_free __P((struct cnode *));
