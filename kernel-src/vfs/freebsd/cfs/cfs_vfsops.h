/* cfs_plat.h: platform-specific support for the CFS driver */

/********************** Lookup */

#define DO_LOOKUP(d, s, f, pvpp, vpp, proc, ndp, error) \
do {                                                    \
    NDINIT((ndp),LOOKUP,(f),(s),(d),(proc));            \
    (error) = namei(ndp);                               \
    *(vpp) = (ndp)->ni_vp;                              \
} while (0)

/********************** Mach flags/types that are missing */

enum vcexcl	{ NONEXCL, EXCL};		/* (non)excl create (create) */

#define FTRUNC       O_TRUNC
#define FCREAT       O_CREAT
#define FEXCL        O_EXCL

/*************** Per process information */
#define GLOBAL_PROC     curproc
/* WARNING 
 * These macros assume the presence of a process pointer p!
 * And, they're wrong to do so.  Phhht.
 */
#define Process_pid     (p ? p->p_pid : -1)
#define Process_pgid    (p ? p->p_pgid : -1)

/******************* Accounting flags */

#define DUMPING_CORE    (p && (p->p_acflag & ACORE))


/*************** User Credentials */

#define CodaCred ucred
#ifdef _KERNEL
#define GLOBAL_CRED  p->p_cred->pc_ucred  /* type (ucred *) */

#define COPY_CRED_TO_CODACRED(in, ident)                \
do {                                                    \
    if (ident != NOCRED) {                              \
	(in)->cred = *(ident);                          \
    } else {                                            \
	bzero(&((in)->cred),sizeof(struct CodaCred));   \
	(in)->cred.cr_uid = -1;                         \
	(in)->cred.cr_gid = -1;                         \
    }                                                   \
} while (0)

/* 
 * what's the name/type of the vnodeop vector?
 */
extern int (**cfs_vnodeop_p)();
#endif /* _KERNEL */

#ifdef _KERNEL 
/* returns a cnode, not a vnode! */
#define	CNODE_NEXT(cp)	((cp)->c_next)

/*************** cnodes/vnodes. */

/* Cnode reference count */
/* XXX - is this right? */
#define CNODE_COUNT(cp)    CTOV(cp)->v_usecount

/* NetBSD vnodes don't have any Pager info in them ('cause there are
   no external pagers, duh!) */
#define VNODE_VM_INFO_INIT(vp)         /* MT */

extern int print_hold_release;

/* NetBSD wants roots returned locked/ref'd.  Mach wants them vref'd */

#define CFS_ROOT_REF(vp) \
do {                     \
    vref(vp);         \
    VOP_LOCK(vp);         \
} while (0)

/* NetBSD wants lookups returned locked.  Mach doesn't */

#define LOOKUP_LOCK(vp)     VOP_LOCK(vp);

/*************** struct vattr differences */

/*************** Conditions. */
/* Why are these here?  I don't know */
typedef char  CONDITION;


/*************** allocation. */

#define CFS_ALLOC(ptr, cast, size)                                        \
do {                                                                      \
    ptr = (cast)malloc((unsigned long) size, M_CFS, M_WAITOK);            \
    if (ptr == 0) {                                                       \
	panic("kernel malloc returns 0 at %s:%d\n", __FILE__, __LINE__);  \
    }                                                                     \
} while (0)


#define CFS_FREE(ptr, size)  free((ptr),M_CFS)


/****************** Prototypes missing from NetBSD includes */

extern int uiomove  __P((caddr_t, int, struct uio *));

#endif /* _KERNEL */
