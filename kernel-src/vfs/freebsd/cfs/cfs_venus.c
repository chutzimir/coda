#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/ioctl.h>

#include <cfs/cfs.h>
#include <cfs/cfsk.h>
#include <cfs/pioctl.h>

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
