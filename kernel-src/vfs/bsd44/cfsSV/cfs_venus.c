#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/ioctl.h>

#include <cfs/cfs.h>
#include <cfs/cfsk.h>
#include <cfs/pioctl.h>

enum vcexcl	{ NONEXCL, EXCL};		/* (non)excl create (create) */

/* NOTES:
	cfsk.h should not be here!!!
	(then proc.h is not necessary ?? maybe)

	The biggy, of course, is that I should use codacreds, but I don't
	want to break venus yet.
 */

venus_root(void *mdp,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid)
{
    struct cfs_in_hdr *inp;
    struct cfs_root_out *outp;
    union cfs_root *cfs_root_buf;
    int cfs_root_size = sizeof (union cfs_root);
    int Osize;
    int error;

    CFS_ALLOC(cfs_root_buf, union cfs_root *, cfs_root_size);
    inp = &cfs_root_buf->in;
    outp = &cfs_root_buf->out;

    /* send the open to venus. */
    INIT_IN(inp, CFS_ROOT, cred);  

    Osize = sizeof (struct cfs_root_out);
    error = cfscall(mdp, sizeof (struct cfs_in_hdr), &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*VFid = outp->VFid;
    }

    CFS_FREE(cfs_root_buf, cfs_root_size);
    return error;
}

venus_open(void *mdp, ViceFid *fid, int flag,
	struct ucred *cred, struct proc *p,
/*out*/	dev_t *dev, ino_t *inode)
{
    struct cfs_open_in *inp;
    struct cfs_open_out *outp;
    union cfs_open *cfs_open_buf;
    int cfs_open_size = sizeof (union cfs_open);
    int Osize;
    int error;

    CFS_ALLOC(cfs_open_buf, union cfs_open *, cfs_open_size);
    inp = &cfs_open_buf->in;
    outp = &cfs_open_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_OPEN, cred);
    inp->VFid = *fid;
    inp->flags = flag;

    Osize = sizeof (struct cfs_open_out);
    error = cfscall(mdp, sizeof (struct cfs_open_in), &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*dev =  outp->dev;
	*inode = outp->inode;
    }

    CFS_FREE(cfs_open_buf, cfs_open_size);
    return error;
}

venus_close(void *mdp, ViceFid *fid, int flag,
	struct ucred *cred, struct proc *p)
{
    struct cfs_close_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_close *cfs_close_buf;
    int cfs_close_size = sizeof (union cfs_close);
    int Osize;
    int error;

    CFS_ALLOC(cfs_close_buf, union cfs_close *, cfs_close_size);
    inp = &cfs_close_buf->in;
    outp = &cfs_close_buf->out;

    INIT_IN(&inp->ih, CFS_CLOSE, cred);
    inp->VFid = *fid;
    inp->flags = flag;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, sizeof (struct cfs_close_in), &Osize, (char *)inp);
    if (!error) 
	error = outp->result;

    CFS_FREE(cfs_close_buf, cfs_close_size);
    return error;
}

/*
 * these two calls will not exist!!!  the container file is read/written
 * directly.
 */
venus_read()
{
}

venus_write()
{
}

/*
 * this is a bit sad too.  the ioctl's are for the control file, not for
 * normal files.
 */
venus_ioctl(void *mdp, ViceFid *fid,
	int com, int flag, caddr_t data,
	struct ucred *cred, struct proc *p)
{
    struct cfs_ioctl_in *inp;
    struct cfs_ioctl_out *outp;
    union cfs_ioctl *cfs_ioctl_buf;
    int cfs_ioctl_size = sizeof (union cfs_ioctl);
    register struct a {
	char *path;
	struct ViceIoctl vidata;
	int follow;
    } *iap = (struct a *)data;
    int error;
    int Osize;
    int tmp;

    cfs_ioctl_size = VC_MAXMSGSIZE;
    CFS_ALLOC(cfs_ioctl_buf, union cfs_ioctl *, cfs_ioctl_size);
    inp = &cfs_ioctl_buf->in;
    outp = &cfs_ioctl_buf->out;

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
    inp->len = iap->vidata.in_size;
    inp->data = (char *)(sizeof (struct cfs_ioctl_in));

    error = copyin(iap->vidata.in, (char*)inp + (int)inp->data, 
		   iap->vidata.in_size);
    if (error) {
	CFS_FREE(cfs_ioctl_buf, cfs_ioctl_size);
	return(error);
    }

    Osize = VC_MAXMSGSIZE;
    error = cfscall(mdp, sizeof (struct cfs_ioctl_in) + iap->vidata.in_size, &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;

	/* copy out the out buffer. */
    if (!error) {
	if (outp->len > iap->vidata.out_size) {
	    error = EINVAL;
	} else {
	    error = copyout((char *)outp + (int)outp->data, 
			    iap->vidata.out, iap->vidata.out_size);
	}
    }

    CFS_FREE(cfs_ioctl_buf, cfs_ioctl_size);
    return error;
}

venus_getattr(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	struct vattr *vap)
{
    struct cfs_getattr_in *inp;
    struct cfs_getattr_out *outp;
    union cfs_getattr *cfs_getattr_buf;
    int cfs_getattr_size = sizeof (union cfs_getattr);
    int Osize;
    int error;

    CFS_ALLOC(cfs_getattr_buf, union cfs_getattr *, cfs_getattr_size);
    inp = &cfs_getattr_buf->in;
    outp = &cfs_getattr_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_GETATTR, cred);
    inp->VFid = *fid;

    Osize = sizeof (struct cfs_getattr_out);
    error = cfscall(mdp, sizeof (struct cfs_getattr_in), &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*vap = outp->attr;
    }

    CFS_FREE(cfs_getattr_buf, cfs_getattr_size);
    return error;
}

venus_setattr(void *mdp, ViceFid *fid, struct vattr *vap,
	struct ucred *cred, struct proc *p)
{
    struct cfs_setattr_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_setattr *cfs_setattr_buf;
    int cfs_setattr_size = sizeof (union cfs_setattr);
    int Osize;
    int error;

    CFS_ALLOC(cfs_setattr_buf, union cfs_setattr *, cfs_setattr_size);
    inp = &cfs_setattr_buf->in;
    outp = &cfs_setattr_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_SETATTR, cred);
    inp->VFid = *fid;
    inp->attr = *vap;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, sizeof (struct cfs_setattr_in), &Osize, (char *)inp);
    if (!error) 
	error = outp->result;

    CFS_FREE(cfs_setattr_buf, cfs_setattr_size);
    return error;
}

venus_access(void *mdp, ViceFid *fid, int mode,
	struct ucred *cred, struct proc *p)
{
    struct cfs_access_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_access *cfs_access_buf;
    int cfs_access_size = sizeof (union cfs_access);
    int Osize;
    int error;

    CFS_ALLOC(cfs_access_buf, union cfs_access *, cfs_access_size);
    inp = &cfs_access_buf->in;
    outp = &cfs_access_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_ACCESS, cred);
    inp->VFid = *fid;
    inp->flags = mode;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, sizeof (struct cfs_access_in), &Osize, (char *)inp);

    if (!error) 
	error = outp->result;

    CFS_FREE(cfs_access_buf, cfs_access_size);
    return error;
}

venus_readlink(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	char **str, int *len)
{
    struct cfs_readlink_in *inp;
    struct cfs_readlink_out *outp;
    union cfs_readlink *cfs_readlink_buf;
    int cfs_readlink_size = sizeof (union cfs_readlink);
    int Osize;
    int error;

    cfs_readlink_size += CFS_MAXPATHLEN;
    CFS_ALLOC(cfs_readlink_buf, union cfs_readlink *, cfs_readlink_size);
    inp = &cfs_readlink_buf->in;
    outp = &cfs_readlink_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_READLINK, cred);
    inp->VFid = *fid;

    Osize = CFS_MAXPATHLEN + sizeof (struct cfs_readlink_out);
    error = cfscall(mdp, sizeof (struct cfs_readlink_in), &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	    CFS_ALLOC(*str, char *, outp->count);
	    *len = outp->count;
	    bcopy((char *)outp + (int)outp->data, *str, *len);
    }

    CFS_FREE(cfs_readlink_buf, cfs_readlink_size);
    return error;
}

venus_fsync(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p)
{
    struct cfs_fsync_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_fsync *cfs_fsync_buf;
    int cfs_fsync_size = sizeof (union cfs_fsync);
    int Osize;
    int error;

    CFS_ALLOC(cfs_fsync_buf, union cfs_fsync *, cfs_fsync_size);
    inp = &cfs_fsync_buf->in;
    outp = &cfs_fsync_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_FSYNC, cred);
    inp->VFid = *fid;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, sizeof (struct cfs_fsync_in), &Osize, (char *)inp);
    if (!error) 
	error = outp->result;

    CFS_FREE(cfs_fsync_buf, cfs_fsync_size);
    return error;
}

venus_lookup(void *mdp, ViceFid *fid,
    	char *nm, int len,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, int *vtype)
{
    struct cfs_lookup_in *inp;
    struct cfs_lookup_out *outp;
    union cfs_lookup *cfs_lookup_buf;
    int cfs_lookup_size = sizeof (union cfs_lookup);
    int Isize, Osize;
    int error;

    cfs_lookup_size += len + 1;
    CFS_ALLOC(cfs_lookup_buf, union cfs_lookup *, cfs_lookup_size);
    inp = &cfs_lookup_buf->in;
    outp = &cfs_lookup_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_LOOKUP, cred);
    inp->VFid = *fid;

    Isize = sizeof (struct cfs_lookup_in);
    inp->name = (char *)Isize;

    strncpy((char *)inp + (int)inp->name, nm, len);
    ((char*)inp + (int)inp->name)[len++] = 0;
    Isize += len;

    Osize = sizeof (struct cfs_lookup_out);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*VFid = outp->VFid;
	*vtype = outp->vtype;
    }

    CFS_FREE(cfs_lookup_buf, cfs_lookup_size);
    return error;
}

#if	0
#define DECL(name) \
    struct name ## _in *inp; \
    struct name ## _out *outp; \
    union name * name ## _buf; \
    int name ## _size = sizeof (union name); \
    int Isize, Osize; \
    int error;

#define ALLOC(name) \
    CFS_ALLOC(name ## _buf, union name *, name ## _size); \
    inp = &name ## _buf->in; \
    outp = &name ## _buf->out;

#define STRCPY(struc, name, len, inc) \
    strncpy((char *)inp + (int)inp->struc, name, len); \
    ((char*)inp + (int)inp->struc)[len++] = 0; \
    inc += len;

venus_lookup(void *mdp, ViceFid *fid,
    	char *nm, int len,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, int *vtype)
{
    DECL(cfs_lookup);

    cfs_lookup_size += len + 1;
    ALLOC(cfs_lookup);

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_LOOKUP, cred);
    inp->VFid = *fid;

    Isize = sizeof (struct cfs_lookup_in);
    inp->name = (char *)Isize;

    STRCPY(name, nm, len, Isize);

    Osize = sizeof (struct cfs_lookup_out);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*VFid = outp->VFid;
	*vtype = outp->vtype;
    }

    CFS_FREE(cfs_lookup_buf, cfs_lookup_size);
    return error;
}
#endif

venus_create(void *mdp, ViceFid *fid,
    	char *nm, int len, enum vcexcl exclusive, int mode, struct vattr *va,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, struct vattr *attr)
{
    struct cfs_create_in *inp;
    struct cfs_create_out *outp;
    union cfs_create *cfs_create_buf;
    int cfs_create_size = sizeof (union cfs_create);
    int Isize, Osize;
    int error;

    cfs_create_size += len + 1;
    CFS_ALLOC(cfs_create_buf, union cfs_create *, cfs_create_size);
    inp = &cfs_create_buf->in;
    outp = &cfs_create_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_CREATE, cred);
    inp->VFid = *fid;
    inp->excl = exclusive;
    inp->mode = mode;
    inp->attr = *va;

    Isize = sizeof (struct cfs_create_in);
    inp->name = (char *)Isize;

    strncpy((char*)inp + (int)inp->name, nm, len);
    ((char*)inp + (int)inp->name)[len++] = 0;
    Isize += len;

    Osize = sizeof (struct cfs_create_out);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*VFid = outp->VFid;
	*attr = outp->attr;
    }

    CFS_FREE(cfs_create_buf, cfs_create_size);
    return error;
}

venus_remove(void *mdp, ViceFid *fid,
        char *nm, int len,
	struct ucred *cred, struct proc *p)
{
    struct cfs_remove_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_remove *cfs_remove_buf;
    int cfs_remove_size = sizeof (union cfs_remove);
    int Isize, Osize;
    int error;

    cfs_remove_size += len + 1;
    CFS_ALLOC(cfs_remove_buf, union cfs_remove *, cfs_remove_size);
    inp = &cfs_remove_buf->in;
    outp = &cfs_remove_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_REMOVE, cred);
    inp->VFid = *fid;

    Isize = sizeof (struct cfs_remove_in);
    inp->name = (char *)Isize;

    strncpy((char *)inp + (int)inp->name, nm, len);
    ((char*)inp + (int)inp->name)[len++] = 0;
    Isize += len;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(cfs_remove_buf, cfs_remove_size);
    return error;
}

venus_link(void *mdp, ViceFid *fid, ViceFid *tfid,
        char *nm, int len,
	struct ucred *cred, struct proc *p)
{
    struct cfs_link_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_link *cfs_link_buf;
    int cfs_link_size = sizeof (union cfs_link);
    int Isize, Osize;
    int error;

    cfs_link_size += len + 1;
    CFS_ALLOC(cfs_link_buf, union cfs_link *, cfs_link_size);
    inp = &cfs_link_buf->in;
    outp = &cfs_link_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_LINK, cred);
    inp->sourceFid = *fid;
    inp->destFid = *tfid;

    Isize = sizeof (struct cfs_link_in);
    inp->tname = (char *)Isize;

    strncpy((char *)inp + (int)inp->tname, nm, len);
    ((char*)inp + (int)inp->tname)[len++] = 0;
    Isize += len;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(cfs_link_buf, cfs_link_size);
    return error;
}

venus_rename(void *mdp, ViceFid *fid, ViceFid *tfid,
        char *nm, int len, char *tnm, int tlen,
	struct ucred *cred, struct proc *p)
{
    struct cfs_rename_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_rename *cfs_rename_buf;
    int cfs_rename_size = sizeof (union cfs_rename);
    int Isize, Osize;
    int error;

    cfs_rename_size += len + 1 + tlen + 1;
    CFS_ALLOC(cfs_rename_buf, union cfs_rename *, cfs_rename_size);
    inp = &cfs_rename_buf->in;
    outp = &cfs_rename_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_RENAME, cred);
    inp->sourceFid = *fid;
    inp->destFid = *tfid;

    Isize = sizeof (struct cfs_rename_in);	
    inp->srcname = (char*)Isize;

    strncpy((char *)inp + (int)inp->srcname, nm, len);
    ((char*)inp + (int)inp->srcname)[len++] = 0;
    Isize += len;

    inp->destname = (char *)Isize;
    strncpy((char *)inp + (int)inp->destname, tnm, tlen);
    ((char*)inp + (int)inp->destname)[tlen++] = 0;
    Isize += tlen;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(cfs_rename_buf, cfs_rename_size);
    return error;
}

venus_mkdir(void *mdp, ViceFid *fid,
    	char *nm, int len, struct vattr *va,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, struct vattr *ova)
{
    struct cfs_mkdir_in *inp;
    struct cfs_mkdir_out *outp;
    union cfs_mkdir *cfs_mkdir_buf;
    int cfs_mkdir_size = sizeof (union cfs_mkdir);
    int Isize, Osize;
    int error;

    cfs_mkdir_size += len + 1;
    CFS_ALLOC(cfs_mkdir_buf, union cfs_mkdir *, cfs_mkdir_size);
    inp = &cfs_mkdir_buf->in;
    outp = &cfs_mkdir_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_MKDIR, cred);
    inp->VFid = *fid;
    inp->attr = *va;

    Isize = sizeof (struct cfs_mkdir_in);
    inp->name = (char *)Isize;

    strncpy((char *)inp + (int)inp->name, nm, len);
    ((char*)inp + (int)inp->name)[len++] = 0;
    Isize += len;

    Osize = sizeof (struct cfs_mkdir_out);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*VFid = outp->VFid;
	*ova = outp->attr;
    }

    CFS_FREE(cfs_mkdir_buf, cfs_mkdir_size);
    return error;
}

venus_rmdir(void *mdp, ViceFid *fid,
    	char *nm, int len,
	struct ucred *cred, struct proc *p)
{
    struct cfs_rmdir_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_rmdir *cfs_rmdir_buf;
    int cfs_rmdir_size = sizeof (union cfs_rmdir);
    int Isize, Osize;
    int error;

    cfs_rmdir_size += len + 1;
    CFS_ALLOC(cfs_rmdir_buf, union cfs_rmdir *, cfs_rmdir_size);
    inp = &cfs_rmdir_buf->in;
    outp = &cfs_rmdir_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_RMDIR, cred);
    inp->VFid = *fid;

    Isize = sizeof (struct cfs_rmdir_in);
    inp->name = (char *)Isize;

    strncpy((char *)inp + (int)inp->name, nm, len);
    ((char*)inp + (int)inp->name)[len++] = 0;
    Isize += len;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(cfs_rmdir_buf, cfs_rmdir_size);
    return error;
}

venus_symlink(void *mdp, ViceFid *fid,
        char *lnm, int llen, char *nm, int len, struct vattr *va,
	struct ucred *cred, struct proc *p)
{
    struct cfs_symlink_in *inp;
    struct cfs_out_hdr *outp;
    union cfs_symlink *cfs_symlink_buf;
    int cfs_symlink_size = sizeof (union cfs_symlink);
    int Isize, Osize;
    int error;

    cfs_symlink_size += llen + 1 + len + 1;
    CFS_ALLOC(cfs_symlink_buf, union cfs_symlink *, cfs_symlink_size);
    inp = &cfs_symlink_buf->in;
    outp = &cfs_symlink_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_SYMLINK, cred);
    inp->VFid = *fid;
    inp->attr = *va;

    Isize = sizeof (struct cfs_symlink_in);
    inp->srcname =(char*)Isize;

    strncpy((char *)inp + (int)inp->srcname, lnm, llen);
    ((char*)inp + (int)inp->srcname)[llen++] = 0;
    Isize += llen;

    inp->tname = (char *)Isize;
    strncpy((char *)inp + (int)inp->tname, nm, len);
    ((char*)inp + (int)inp->tname)[len++] = 0;
    Isize += len;

    Osize = sizeof (struct cfs_out_hdr);
    error = cfscall(mdp, Isize, &Osize, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(cfs_symlink_buf, cfs_symlink_size);
    return error;
}

venus_readdir(void *mdp, ViceFid *fid,
    	int count, int offset,
	struct ucred *cred, struct proc *p,
/*out*/	char *buffer, int *len)
{
    struct cfs_readdir_in *inp;
    struct cfs_readdir_out *outp;
    union cfs_readdir *cfs_readdir_buf;
    int cfs_readdir_size = sizeof (union cfs_readdir);
    int Osize;
    int error;

    cfs_readdir_size = VC_MAXMSGSIZE;
    CFS_ALLOC(cfs_readdir_buf, union cfs_readdir *, cfs_readdir_size);
    inp = &cfs_readdir_buf->in;
    outp = &cfs_readdir_buf->out;

    /* send the open to venus. */
    INIT_IN(&inp->ih, CFS_READDIR, cred);
    inp->VFid = *fid;
    inp->count = count;
    inp->offset = offset;

    Osize = VC_MAXMSGSIZE;
    error = cfscall(mdp, sizeof (struct cfs_readdir_in), &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	bcopy((char *)outp + (int)outp->data, buffer, outp->size);
	*len = outp->size;
    }

    CFS_FREE(cfs_readdir_buf, cfs_readdir_size);
    return error;
}

venus_fhtovp(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, int *vtype)
{
    struct cfs_vget_in *inp;
    struct cfs_vget_out *outp;
    union cfs_vget *cfs_vget_buf;
    int cfs_vget_size = sizeof (union cfs_vget);
    int Osize;
    int error;

    CFS_ALLOC(cfs_vget_buf, union cfs_vget *, cfs_vget_size);
    inp = &cfs_vget_buf->in;
    outp = &cfs_vget_buf->out;

    /* Send the open to Venus. */
    INIT_IN(&inp->ih, CFS_VGET, cred);
    inp->VFid = *fid;

    Osize = sizeof (struct cfs_vget_out);
    error = cfscall(mdp, sizeof (struct cfs_vget_in), &Osize, (char *)inp);

    if (!error)
    	error = outp->oh.result;
    if (!error) {
	*VFid = outp->VFid;
	*vtype = outp->vtype;
    }

    CFS_FREE(cfs_vget_buf, cfs_vget_size);
    return error;
}
