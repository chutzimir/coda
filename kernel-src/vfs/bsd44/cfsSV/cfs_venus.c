#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/select.h>
#include <sys/ioctl.h>
/* for CNV_OFLAGS below */
#include <sys/fcntl.h>

#include <cfs/coda.h>
#include <cfs/cnode.h>
#include <cfs/cfs_venus.h>
#include <cfs/pioctl.h>

#define DECL_NO_IN(name) \
    struct cfs_in_hdr *inp; \
    struct name ## _out *outp; \
    union name * name ## _buf; \
    int name ## _size = sizeof (union name); \
    int Isize = sizeof (struct cfs_in_hdr); \
    int Osize = sizeof (struct name ## _out); \
    int error

#define DECL(name) \
    struct name ## _in *inp; \
    struct name ## _out *outp; \
    union name * name ## _buf; \
    int name ## _size = sizeof (union name); \
    int Isize = sizeof (struct name ## _in); \
    int Osize = sizeof (struct name ## _out); \
    int error

#define DECL_NO_OUT(name) \
    struct name ## _in *inp; \
    struct cfs_out_hdr *outp; \
    union name * name ## _buf; \
    int name ## _size = sizeof (union name); \
    int Isize = sizeof (struct name ## _in); \
    int Osize = sizeof (struct cfs_out_hdr); \
    int error

#define ALLOC(name) \
    CFS_ALLOC(name ## _buf, union name *, name ## _size); \
    inp = &name ## _buf->in; \
    outp = &name ## _buf->out

#define STRCPY(struc, name, len) \
    bcopy(name, (char *)inp + (int)inp->struc, len); \
    ((char*)inp + (int)inp->struc)[len++] = 0; \
    Isize += len

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

#define	CNV_OFLAG(to, from) 				\
    do { 						\
	  to = 0;					\
	  if (from & FREAD)   to |= C_READ; 		\
	  if (from & FWRITE)  to |= C_WRITE; 		\
	  if (from & O_TRUNC) to |= C_TRUNC; 		\
	  if (from & O_EXCL)  to |= C_EXCL; 		\
    } while (0)

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


int
venus_root(void *mdp,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid)
{
    DECL_NO_IN(cfs_root);		/* sets Isize & Osize */
    ALLOC(cfs_root);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(inp, CFS_ROOT, cred);  

    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error)
	*VFid = outp->VFid;

    CFS_FREE(cfs_root_buf, cfs_root_size);
    return error;
}

int
venus_open(void *mdp, ViceFid *fid, int flag,
	struct ucred *cred, struct proc *p,
/*out*/	dev_t *dev, ino_t *inode)
{
    int cflag;
    DECL(cfs_open);			/* sets Isize & Osize */
    ALLOC(cfs_open);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_OPEN, cred);
    inp->VFid = *fid;
    CNV_OFLAG(cflag, flag);
    inp->flags = cflag;

    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	*dev =  outp->dev;
	*inode = outp->inode;
    }

    CFS_FREE(cfs_open_buf, cfs_open_size);
    return error;
}

int
venus_close(void *mdp, ViceFid *fid, int flag,
	struct ucred *cred, struct proc *p)
{
    int cflag;
    DECL_NO_OUT(cfs_close);		/* sets Isize & Osize */
    ALLOC(cfs_close);			/* sets inp & outp */

    INIT_IN(&inp->ih, CFS_CLOSE, cred);
    inp->VFid = *fid;
    CNV_OFLAG(cflag, flag);
    inp->flags = cflag;

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_close_buf, cfs_close_size);
    return error;
}

/*
 * these two calls will not exist!!!  the container file is read/written
 * directly.
 */
void
venus_read(void)
{
}

void
venus_write(void)
{
}

/*
 * this is a bit sad too.  the ioctl's are for the control file, not for
 * normal files.
 */
int
venus_ioctl(void *mdp, ViceFid *fid,
	int com, int flag, caddr_t data,
	struct ucred *cred, struct proc *p)
{
    DECL(cfs_ioctl);			/* sets Isize & Osize */
    struct PioctlData *iap = (struct PioctlData *)data;
    int tmp;

    cfs_ioctl_size = VC_MAXMSGSIZE;
    ALLOC(cfs_ioctl);			/* sets inp & outp */

    INIT_IN(&inp->ih, CFS_IOCTL, cred);
    inp->VFid = *fid;

    /* command was mutated by increasing its size field to reflect the  
     * path and follow args. we need to subtract that out before sending
     * the command to venus.
     */
    inp->cmd = (com & ~(IOCPARM_MASK << 16));
    tmp = ((com >> 16) & IOCPARM_MASK) - sizeof (char *) - sizeof (int);
    inp->cmd |= (tmp & IOCPARM_MASK) <<	16;

    inp->rwflag = flag;
    inp->len = iap->vi.in_size;
    inp->data = (char *)(sizeof (struct cfs_ioctl_in));

    error = copyin(iap->vi.in, (char*)inp + (int)inp->data, 
		   iap->vi.in_size);
    if (error) {
	CFS_FREE(cfs_ioctl_buf, cfs_ioctl_size);
	return(error);
    }

    Osize = VC_MAXMSGSIZE;
    error = cfscall(mdp, Isize + iap->vi.in_size, &Osize, (char *)inp);

	/* copy out the out buffer. */
    if (!error) {
	if (outp->len > iap->vi.out_size) {
	    error = EINVAL;
	} else {
	    error = copyout((char *)outp + (int)outp->data, 
			    iap->vi.out, iap->vi.out_size);
	}
    }

    CFS_FREE(cfs_ioctl_buf, cfs_ioctl_size);
    return error;
}

int
venus_getattr(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	struct vattr *vap)
{
    DECL(cfs_getattr);			/* sets Isize & Osize */
    ALLOC(cfs_getattr);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_GETATTR, cred);
    inp->VFid = *fid;

    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	CNV_VV2V_ATTR(vap, &outp->attr);
    }

    CFS_FREE(cfs_getattr_buf, cfs_getattr_size);
    return error;
}

int
venus_setattr(void *mdp, ViceFid *fid, struct vattr *vap,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_setattr);		/* sets Isize & Osize */
    ALLOC(cfs_setattr);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_SETATTR, cred);
    inp->VFid = *fid;
    CNV_V2VV_ATTR(&inp->attr, vap);

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_setattr_buf, cfs_setattr_size);
    return error;
}

int
venus_access(void *mdp, ViceFid *fid, int mode,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_access);		/* sets Isize & Osize */
    ALLOC(cfs_access);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_ACCESS, cred);
    inp->VFid = *fid;
#ifdef	NetBSD1_3
    inp->flags = mode<<6;
#else
    inp->flags = mode;
#endif

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_access_buf, cfs_access_size);
    return error;
}

int
venus_readlink(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	char **str, int *len)
{
    DECL(cfs_readlink);			/* sets Isize & Osize */
    cfs_readlink_size += CFS_MAXPATHLEN;
    ALLOC(cfs_readlink);		/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_READLINK, cred);
    inp->VFid = *fid;

    Osize += CFS_MAXPATHLEN;
    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	    CFS_ALLOC(*str, char *, outp->count);
	    *len = outp->count;
	    bcopy((char *)outp + (int)outp->data, *str, *len);
    }

    CFS_FREE(cfs_readlink_buf, cfs_readlink_size);
    return error;
}

int
venus_fsync(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_fsync);		/* sets Isize & Osize */
    ALLOC(cfs_fsync);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_FSYNC, cred);
    inp->VFid = *fid;

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_fsync_buf, cfs_fsync_size);
    return error;
}

int
venus_lookup(void *mdp, ViceFid *fid,
    	const char *nm, int len,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, int *vtype)
{
    DECL(cfs_lookup);			/* sets Isize & Osize */
    cfs_lookup_size += len + 1;
    ALLOC(cfs_lookup);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_LOOKUP, cred);
    inp->VFid = *fid;

    inp->name = (char *)Isize;
    STRCPY(name, nm, len);		/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	*VFid = outp->VFid;
	*vtype = outp->vtype;
    }

    CFS_FREE(cfs_lookup_buf, cfs_lookup_size);
    return error;
}

int
venus_create(void *mdp, ViceFid *fid,
    	const char *nm, int len, int exclusive, int mode, struct vattr *va,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, struct vattr *attr)
{
    DECL(cfs_create);			/* sets Isize & Osize */
    cfs_create_size += len + 1;
    ALLOC(cfs_create);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_CREATE, cred);
    inp->VFid = *fid;
    inp->excl = exclusive ? C_EXCL : 0;
    inp->mode = mode;
    CNV_V2VV_ATTR(&inp->attr, va);

    inp->name = (char *)Isize;
    STRCPY(name, nm, len);		/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	*VFid = outp->VFid;
	CNV_VV2V_ATTR(attr, &outp->attr);
    }

    CFS_FREE(cfs_create_buf, cfs_create_size);
    return error;
}

int
venus_remove(void *mdp, ViceFid *fid,
        const char *nm, int len,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_remove);		/* sets Isize & Osize */
    cfs_remove_size += len + 1;
    ALLOC(cfs_remove);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_REMOVE, cred);
    inp->VFid = *fid;

    inp->name = (char *)Isize;
    STRCPY(name, nm, len);		/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_remove_buf, cfs_remove_size);
    return error;
}

int
venus_link(void *mdp, ViceFid *fid, ViceFid *tfid,
        const char *nm, int len,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_link);		/* sets Isize & Osize */
    cfs_link_size += len + 1;
    ALLOC(cfs_link);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_LINK, cred);
    inp->sourceFid = *fid;
    inp->destFid = *tfid;

    inp->tname = (char *)Isize;
    STRCPY(tname, nm, len);		/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_link_buf, cfs_link_size);
    return error;
}

int
venus_rename(void *mdp, ViceFid *fid, ViceFid *tfid,
        const char *nm, int len, const char *tnm, int tlen,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_rename);		/* sets Isize & Osize */
    cfs_rename_size += len + 1 + tlen + 1;
    ALLOC(cfs_rename);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_RENAME, cred);
    inp->sourceFid = *fid;
    inp->destFid = *tfid;

    inp->srcname = (char*)Isize;
    STRCPY(srcname, nm, len);		/* increments Isize */

    inp->destname = (char *)Isize;
    STRCPY(destname, tnm, tlen);	/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_rename_buf, cfs_rename_size);
    return error;
}

int
venus_mkdir(void *mdp, ViceFid *fid,
    	const char *nm, int len, struct vattr *va,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, struct vattr *ova)
{
    DECL(cfs_mkdir);			/* sets Isize & Osize */
    cfs_mkdir_size += len + 1;
    ALLOC(cfs_mkdir);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_MKDIR, cred);
    inp->VFid = *fid;
    CNV_V2VV_ATTR(&inp->attr, va);

    inp->name = (char *)Isize;
    STRCPY(name, nm, len);		/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	*VFid = outp->VFid;
	CNV_VV2V_ATTR(ova, &outp->attr);
    }

    CFS_FREE(cfs_mkdir_buf, cfs_mkdir_size);
    return error;
}

int
venus_rmdir(void *mdp, ViceFid *fid,
    	const char *nm, int len,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_rmdir);		/* sets Isize & Osize */
    cfs_rmdir_size += len + 1;
    ALLOC(cfs_rmdir);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_RMDIR, cred);
    inp->VFid = *fid;

    inp->name = (char *)Isize;
    STRCPY(name, nm, len);		/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_rmdir_buf, cfs_rmdir_size);
    return error;
}

int
venus_symlink(void *mdp, ViceFid *fid,
        const char *lnm, int llen, const char *nm, int len, struct vattr *va,
	struct ucred *cred, struct proc *p)
{
    DECL_NO_OUT(cfs_symlink);		/* sets Isize & Osize */
    cfs_symlink_size += llen + 1 + len + 1;
    ALLOC(cfs_symlink);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_SYMLINK, cred);
    inp->VFid = *fid;
    CNV_V2VV_ATTR(&inp->attr, va);

    inp->srcname =(char*)Isize;
    STRCPY(srcname, lnm, llen);		/* increments Isize */

    inp->tname = (char *)Isize;
    STRCPY(tname, nm, len);		/* increments Isize */

    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    CFS_FREE(cfs_symlink_buf, cfs_symlink_size);
    return error;
}

int
venus_readdir(void *mdp, ViceFid *fid,
    	int count, int offset,
	struct ucred *cred, struct proc *p,
/*out*/	char *buffer, int *len)
{
    DECL(cfs_readdir);			/* sets Isize & Osize */
    cfs_readdir_size = VC_MAXMSGSIZE;
    ALLOC(cfs_readdir);			/* sets inp & outp */

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_READDIR, cred);
    inp->VFid = *fid;
    inp->count = count;
    inp->offset = offset;

    Osize = VC_MAXMSGSIZE;
    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	bcopy((char *)outp + (int)outp->data, buffer, outp->size);
	*len = outp->size;
    }

    CFS_FREE(cfs_readdir_buf, cfs_readdir_size);
    return error;
}

int
venus_fhtovp(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, int *vtype)
{
    DECL(cfs_vget);			/* sets Isize & Osize */
    ALLOC(cfs_vget);			/* sets inp & outp */

    /* Send the open to Venus. */
    INIT_IN(&inp->ih, CFS_VGET, cred);
    inp->VFid = *fid;

    error = cfscall(mdp, Isize, &Osize, (char *)inp);
    if (!error) {
	*VFid = outp->VFid;
	*vtype = outp->vtype;
    }

    CFS_FREE(cfs_vget_buf, cfs_vget_size);
    return error;
}
