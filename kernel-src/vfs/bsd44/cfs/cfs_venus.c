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


venus_open(void *mdp, ViceFid *fid, int flag,
	struct ucred *cred, struct proc *p,
/*out*/	dev_t *dev, ino_t *inode)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    int outSize = sizeof(struct outputArgs);
    int error;

    CFS_ALLOC(inp, struct inputArgs *, sizeof(struct inputArgs));
    outp = (struct outputArgs *) inp;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_OPEN, cred);
    
    inp->d.cfs_open.VFid = *fid;
    inp->d.cfs_open.flags = flag;

    error = cfscall(mdp, sizeof(struct inputArgs), &outSize, (char *)inp);
    if (!error)
    	error = outp->result;
    if (!error) {
	*dev =  outp->d.cfs_open.dev,
	*inode = outp->d.cfs_open.inode;
    }

    CFS_FREE(inp, sizeof(struct inputArgs));
    return error;
}

venus_close(void *mdp, ViceFid *fid, int flag,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    int outSize = sizeof(struct outputArgs);
    int error;

    CFS_ALLOC(inp, struct inputArgs *, sizeof(struct inputArgs));
    outp = (struct outputArgs *) inp;

    INIT_IN(inp, CFS_CLOSE, cred);

    inp->d.cfs_close.VFid = *fid;
    inp->d.cfs_close.flags = flag;
    

    error = cfscall(mdp, sizeof(struct inputArgs), &outSize, (char *)inp);
    if (!error) 
	error = outp->result;

    CFS_FREE(inp,sizeof(struct inputArgs));
    return error;
}

/*
 * These two calls will not exist!!!  The container file is read/written
 * directly.
 */
venus_read()
{
}

venus_write()
{
}

/*
 * This is a bit sad too.  The ioctl's are for the CONTROL file, not for
 * normal files.
 */
venus_ioctl(void *mdp, ViceFid *fid,
	int com, int flag, caddr_t data,
	struct ucred *cred, struct proc *p)
{
    int error, size;
    struct inputArgs *in = NULL;
    struct outputArgs *out;
    char *buf;
    register struct a {
	char *path;
	struct ViceIoctl vidata;
	int follow;
    } *iap = (struct a *)data;

    CFS_ALLOC(buf, char *, VC_MAXMSGSIZE);
    in = (struct inputArgs *)buf;
    out = (struct outputArgs *)buf;

    INIT_IN(in, CFS_IOCTL, cred);
    in->d.cfs_ioctl.VFid = *fid;

    /* Command was mutated by increasing its size field to reflect the  
     * path and follow args. We need to subtract that out before sending
     * the command to Venus.
     */
    in->d.cfs_ioctl.cmd = (com & ~(IOCPARM_MASK << 16));
    size = ((com >> 16) & IOCPARM_MASK) - sizeof(char *) - sizeof(int);
    in->d.cfs_ioctl.cmd |= (size & IOCPARM_MASK) <<	16;

    in->d.cfs_ioctl.rwflag = flag;
    in->d.cfs_ioctl.len = iap->vidata.in_size;
    in->d.cfs_ioctl.data = (char *)(VC_INSIZE(cfs_ioctl_in));

    error = copyin(iap->vidata.in, (char*)in + (int)in->d.cfs_ioctl.data, 
		   iap->vidata.in_size);
    if (error) {
	CFS_FREE(buf, VC_MAXMSGSIZE);
	return(error);
    }

    size = VC_MAXMSGSIZE;
    error = cfscall(mdp, VC_INSIZE(cfs_ioctl_in) + iap->vidata.in_size, &size, buf);

    if (!error)
    	error = out->result;

	/* Copy out the OUT buffer. */
    if (!error) {
	if (out->d.cfs_ioctl.len > iap->vidata.out_size) {
	    CFSDEBUG(CFS_IOCTL, myprintf(("return len %d <= request len %d\n",
				       out->d.cfs_ioctl.len, 
				       iap->vidata.out_size)); );
	    error = EINVAL;
	} else {
	    error = copyout((char *)out + (int)out->d.cfs_ioctl.data, 
			    iap->vidata.out, iap->vidata.out_size);
	}
    }

    CFS_FREE(buf, VC_MAXMSGSIZE);
    return error;
}

venus_getattr(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	struct vattr *vap)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    int size;
    int error;

    CFS_ALLOC(inp, struct inputArgs *, sizeof(struct inputArgs));
    outp = (struct outputArgs *) inp;

    /* send the open to venus. */
    INIT_IN(inp, CFS_GETATTR, cred);
    
    inp->d.cfs_getattr.VFid = *fid;
    size = VC_OUTSIZE(cfs_getattr_out);

    error = cfscall(mdp, VC_INSIZE(cfs_getattr_in), &size, (char *)inp);
    if (!error)
    	error = outp->result;
    if (!error) {
	*vap = outp->d.cfs_getattr.attr;
    }

    CFS_FREE(inp, sizeof(struct inputArgs));
    return error;
}

venus_setattr(void *mdp, ViceFid *fid, struct vattr *vap,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    int size;
    int error;

    CFS_ALLOC(inp, struct inputArgs *, sizeof(struct inputArgs));
    outp = (struct outputArgs *) inp;

    /* send the open to venus. */
    INIT_IN(inp, CFS_SETATTR, cred);
    
    inp->d.cfs_setattr.VFid = *fid;
    inp->d.cfs_setattr.attr = *vap;
    size = VC_OUT_NO_DATA;        

    error = cfscall(mdp, VC_INSIZE(cfs_setattr_in), &size, (char *)inp);
    if (!error) 
	error = outp->result;

    CFS_FREE(inp, sizeof(struct inputArgs));
    return error;
}

venus_access(void *mdp, ViceFid *fid, int mode,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    int size;
    int error;

    CFS_ALLOC(inp, struct inputArgs *, sizeof(struct inputArgs));
    outp = (struct outputArgs *) inp;

    /* send the open to venus. */
    INIT_IN(inp, CFS_ACCESS, cred);
    inp->d.cfs_access.VFid = *fid;
    inp->d.cfs_access.flags = mode;
    size = VC_OUT_NO_DATA;

    error = cfscall(mdp, VC_INSIZE(cfs_access_in), &size, (char *)inp);
    if (!error) 
	error = outp->result;

    CFS_FREE(inp, sizeof(struct inputArgs));
    return error;
}

venus_readlink(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p,
/*out*/	char **str, int *len)
{
    struct inputArgs *inp;
    struct outputArgs *outp;
    char *buf=NULL; /*[CFS_MAXPATHLEN + VC_INSIZE(cfs_readlink_in)];*/
    int size;
    int error;

    CFS_ALLOC(buf, char*, CFS_MAXPATHLEN + VC_INSIZE(cfs_readlink_in));
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* send the open to venus. */
    INIT_IN(inp, CFS_READLINK, cred);

    inp->d.cfs_readlink.VFid = *fid;
    size = CFS_MAXPATHLEN + VC_OUTSIZE(cfs_readlink_out);

    error = cfscall(mdp, VC_INSIZE(cfs_readlink_in), &size, (char *)inp);
    if (!error)
    	error = outp->result;
    if (!error) {
	    CFS_ALLOC(*str, char *, outp->d.cfs_readlink.count);
	    *len = outp->d.cfs_readlink.count;
	    bcopy((char *)outp + (int)outp->d.cfs_readlink.data, *str, *len);
    }

    CFS_FREE(buf, CFS_MAXPATHLEN + VC_INSIZE(cfs_readlink_in));
    return error;
}

venus_fsync(void *mdp, ViceFid *fid,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    int size;
    int error;

    CFS_ALLOC(inp, struct inputArgs *, sizeof(struct inputArgs));
    outp = (struct outputArgs *) inp;

    /* send the open to venus. */
    INIT_IN(inp, CFS_FSYNC, cred);
    inp->d.cfs_access.VFid = *fid;
    size = VC_INSIZE(cfs_fsync_in);

    error = cfscall(mdp, VC_INSIZE(cfs_fsync_in), &size, (char *)inp);
    if (!error) 
	error = outp->result;

    CFS_FREE(inp, sizeof(struct inputArgs));
    return error;
}

venus_lookup(void *mdp, ViceFid *fid,
    	char *nm, int len,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, int *vtype)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf = NULL; /*[VC_INSIZE(cfs_lookup_in) + CFS_MAXNAMLEN + 1];*/
    int error = 0;
    int size;

    CFS_ALLOC(buf, char *, (VC_INSIZE(cfs_lookup_in) + CFS_MAXNAMLEN + 1));
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_LOOKUP, cred);

    inp->d.cfs_lookup.VFid = *fid;
    size = VC_INSIZE(cfs_lookup_in);
    inp->d.cfs_lookup.name = (char *)size;

    strncpy((char *)inp + (int)inp->d.cfs_lookup.name, nm, len);
    size += len;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;
    if (!error) {
	*VFid = outp->d.cfs_lookup.VFid;
	*vtype = outp->d.cfs_lookup.vtype;
    }

    CFS_FREE(inp, (VC_INSIZE(cfs_lookup_in) + CFS_MAXNAMLEN + 1));
    return error;
}

venus_create(void *mdp, ViceFid *fid,
    	char *nm, enum vcexcl exclusive, int mode, struct vattr *va,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, struct vattr *attr)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf = NULL; /*[VC_INSIZE(cfs_lookup_in) + CFS_MAXNAMLEN + 1];*/
    int error = 0;
    int len;
    int size;

    CFS_ALLOC(buf, char *, (VC_INSIZE(cfs_create_in) + CFS_MAXNAMLEN));
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_CREATE, cred);

    inp->d.cfs_create.VFid = *fid;
    inp->d.cfs_create.excl = exclusive;
    inp->d.cfs_create.mode = mode;
    inp->d.cfs_create.attr = *va;

    size = VC_INSIZE(cfs_create_in);
    inp->d.cfs_create.name = (char *)size;

    len = strlen(nm) + 1;
    strncpy((char*)inp + (int)inp->d.cfs_create.name, nm, len);
    size += len;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;
    if (!error) {
	*VFid = outp->d.cfs_create.VFid;
	*attr = outp->d.cfs_create.attr;
    }

    CFS_FREE(buf, (VC_INSIZE(cfs_create_in) + CFS_MAXNAMLEN));
    return error;
}

venus_remove(void *mdp, ViceFid *fid,
        char *nm,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf=NULL; /*[CFS_MAXNAMLEN + sizeof(struct inputArgs)];*/
    int error = 0;
    int len;
    int size;

    CFS_ALLOC(buf, char *, CFS_MAXNAMLEN + sizeof(struct inputArgs));
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_REMOVE, cred);

    inp->d.cfs_remove.VFid = *fid;
    inp->d.cfs_remove.name = (char *)(VC_INSIZE(cfs_remove_in));

    len = strlen(nm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_remove.name, nm, len);
    len += VC_INSIZE(cfs_remove_in);
    size = len;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(buf, CFS_MAXNAMLEN + sizeof(struct inputArgs));
    return error;
}

venus_link(void *mdp, ViceFid *fid, ViceFid *tfid,
        char *nm,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf=NULL; /*[CFS_MAXNAMLEN + VC_INSIZE(cfs_link_in)];*/
    int error = 0;
    int len;
    int size;

    CFS_ALLOC(buf, char *, CFS_MAXNAMLEN + sizeof(struct inputArgs));
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_LINK, cred);

    inp->d.cfs_link.sourceFid = *fid;
    inp->d.cfs_link.destFid = *tfid;

    inp->d.cfs_link.tname = (char *)(VC_INSIZE(cfs_link_in));

    len = strlen(nm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_link.tname, nm, len);
    size = VC_INSIZE(cfs_link_in) + len;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(buf, CFS_MAXNAMLEN + sizeof(struct inputArgs));
    return error;
}

venus_rename(void *mdp, ViceFid *fid, ViceFid *tfid,
        char *nm, char *tnm,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf=NULL; /*[VC_INSIZE(cfs_rename_in) + 2 * CFS_MAXNAMLEN + 8];*/
    int error = 0;
    int len;
    int size;

    CFS_ALLOC(buf, char *, VC_INSIZE(cfs_rename_in) + 2 * CFS_MAXNAMLEN + 8);
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_RENAME, cred);

    inp->d.cfs_rename.sourceFid = *fid;
    inp->d.cfs_rename.destFid = *tfid;

    size = VC_INSIZE(cfs_rename_in);	
    inp->d.cfs_rename.srcname = (char*)size;
    len = strlen(nm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_rename.srcname, nm, len);
    size += len;

    inp->d.cfs_rename.destname = (char *)size;
    len = strlen(tnm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_rename.destname, tnm, len);
    size += len;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(buf, VC_INSIZE(cfs_rename_in) + 2 * CFS_MAXNAMLEN + 8);
    return error;
}

venus_mkdir(void *mdp, ViceFid *fid,
    	char *nm, struct vattr *va,
	struct ucred *cred, struct proc *p,
/*out*/	ViceFid *VFid, struct vattr *ova)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf=NULL; /*[CFS_MAXNAMLEN + VC_INSIZE(cfs_mkdir_in)];*/
    int error = 0;
    int len;
    int size;

    CFS_ALLOC(buf, char *, CFS_MAXNAMLEN + VC_INSIZE(cfs_mkdir_in));
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_MKDIR, cred);

    inp->d.cfs_mkdir.VFid = *fid;
    inp->d.cfs_mkdir.attr = *va;

    inp->d.cfs_mkdir.name = (char *)(VC_INSIZE(cfs_mkdir_in));
    len = strlen(nm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_mkdir.name, nm, len);
    size = VC_INSIZE(cfs_mkdir_in) + len;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;
    if (!error) {
	*VFid = outp->d.cfs_mkdir.VFid;
	*ova = outp->d.cfs_mkdir.attr;
    }

    CFS_FREE(buf, CFS_MAXNAMLEN + VC_INSIZE(cfs_mkdir_in));
    return error;
}

venus_rmdir(void *mdp, ViceFid *fid,
    	char *nm,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf=NULL;		/*[CFS_MAXNAMLEN + VC_INSIZE(cfs_rmdir_in)];*/
    int error = 0;
    int size;

    CFS_ALLOC(buf, char *, CFS_MAXNAMLEN + VC_INSIZE(cfs_rmdir_in));
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_RMDIR, cred);

    inp->d.cfs_rmdir.VFid = *fid;
    inp->d.cfs_rmdir.name = (char *)(VC_INSIZE(cfs_rmdir_in));

    size = strlen(nm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_rmdir.name, nm, size);
    size = VC_INSIZE(cfs_rmdir_in) + size;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(buf, CFS_MAXNAMLEN + VC_INSIZE(cfs_rmdir_in));
    return error;
}

venus_symlink(void *mdp, ViceFid *fid,
        char *lnm, char *nm, struct vattr *va,
	struct ucred *cred, struct proc *p)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf = NULL; /*[sizeof(struct inputArgs) + CFS_MAXPATHLEN + CFS_MAXNAMLEN + 8];*/
    int error = 0;
    int len;
    int size;

    CFS_ALLOC(buf, char *, sizeof(struct inputArgs) + CFS_MAXPATHLEN + CFS_MAXNAMLEN + 8);
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* Send the open to Venus. */
    INIT_IN(inp, CFS_SYMLINK, cred);

    inp->d.cfs_symlink.VFid = *fid;
    inp->d.cfs_symlink.attr = *va;

    size = VC_INSIZE(cfs_symlink_in);
    inp->d.cfs_symlink.srcname =(char*)size;

    len = strlen(lnm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_symlink.srcname, lnm, len);
    size += len;

    inp->d.cfs_symlink.tname = (char *)size;
    len = strlen(nm) + 1;
    strncpy((char *)inp + (int)inp->d.cfs_symlink.tname, nm, len);
    size += len;

    error = cfscall(mdp, size, &size, (char *)inp);

    if (!error)
    	error = outp->result;

    CFS_FREE(buf, sizeof(struct inputArgs) + CFS_MAXPATHLEN + CFS_MAXNAMLEN + 8);
    return error;
}

venus_readdir(void *mdp, ViceFid *fid,
    	int count, int offset,
	struct ucred *cred, struct proc *p,
/*out*/	char *buffer, int *len)
{
    struct inputArgs *inp = NULL;
    struct outputArgs *outp;
    char *buf=NULL;
    int size;
    int error;

    CFS_ALLOC(buf, char *, VC_MAXMSGSIZE);
    inp = (struct inputArgs *) buf;
    outp = (struct outputArgs *) buf;

    /* send the open to venus. */
    INIT_IN(inp, CFS_READDIR, cred);
    
    inp->d.cfs_readdir.VFid = *fid;
    inp->d.cfs_readdir.count = count;
    inp->d.cfs_readdir.offset = offset;
    size = VC_MAXMSGSIZE;

    error = cfscall(mdp, VC_INSIZE(cfs_readdir_in), &size, (char *)inp);
    if (!error)
    	error = outp->result;
    if (!error) {
	bcopy((char *)outp + (int)outp->d.cfs_readdir.data, buffer, outp->d.cfs_readdir.size);
	*len = outp->d.cfs_readdir.size;
    }

    CFS_FREE(buf, VC_MAXMSGSIZE);
    return error;
}
