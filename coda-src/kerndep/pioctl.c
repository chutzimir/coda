#ifndef _BLURB_
#define _BLURB_
/*

            Coda: an Experimental Distributed File System
                             Release 4.0

          Copyright (c) 1987-1996 Carnegie Mellon University
                         All Rights Reserved

Permission  to  use, copy, modify and distribute this software and its
documentation is hereby granted,  provided  that  both  the  copyright
notice  and  this  permission  notice  appear  in  all  copies  of the
software, derivative works or  modified  versions,  and  any  portions
thereof, and that both notices appear in supporting documentation, and
that credit is given to Carnegie Mellon University  in  all  documents
and publicity pertaining to direct or indirect use of this code or its
derivatives.

CODA IS AN EXPERIMENTAL SOFTWARE SYSTEM AND IS  KNOWN  TO  HAVE  BUGS,
SOME  OF  WHICH MAY HAVE SERIOUS CONSEQUENCES.  CARNEGIE MELLON ALLOWS
FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION.   CARNEGIE  MELLON
DISCLAIMS  ANY  LIABILITY  OF  ANY  KIND  FOR  ANY  DAMAGES WHATSOEVER
RESULTING DIRECTLY OR INDIRECTLY FROM THE USE OF THIS SOFTWARE  OR  OF
ANY DERIVATIVE WORK.

Carnegie  Mellon  encourages  users  of  this  software  to return any
improvements or extensions that  they  make,  and  to  grant  Carnegie
Mellon the rights to redistribute these changes without encumbrance.
*/

static char *rcsid = "$Header$";
#endif /*_BLURB_*/


#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <pioctl.h>

#ifdef __CYGWIN32__
#include <windows.h>
#endif

#ifdef __cplusplus
}
#endif __cplusplus

#ifdef __CYGWIN32__
int pioctl(const char *path, unsigned long com, 
	   struct ViceIoctl *vidata, int follow) 
{
    /* Pack <path, vidata, follow> into a PioctlData structure 
     * since ioctl takes only one data arg. 
     */
    struct PioctlData data;
    int code, res = 0;
    HANDLE hdev;
    DWORD bytesReturned;
    char *s;

    /* Must change the size field of the command to match 
       that of the new structure. */
    unsigned long cmd = (com & ~(PIOCPARM_MASK << 16)); /* mask out size  */
    int	size = ((com >> 16) & PIOCPARM_MASK) + sizeof(char *) + sizeof(int);
    cmd	|= (size & PIOCPARM_MASK) << 16;  /* or in corrected size */

    /*    if (index(path, '/')) return (-1); */
    s = index(path, ':');
    if (s) path = s+1;
    /*    if (!strncmp(path, "\\", 1)||!strncmp(path, "/", 1)) path++;  */
    printf("path : %s\n", path);
   
    data.cmd = cmd;
    data.path = path;
    data.follow = follow;
    data.vi = *vidata;
    /*   printf("insize: %i\n", data.vi.in_size);   */

    hdev = CreateFile("\\\\.\\CODADEV", 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (hdev == INVALID_HANDLE_VALUE){
	    printf("invalid handle to CODADEV error %i\n", GetLastError());
	    return(-1);
    }

    code = DeviceIoControl(hdev, 11, (LPVOID)&data, sizeof(struct PioctlData),
			   vidata->out, vidata->out_size, &bytesReturned, NULL);
   

    if (!code){
	    res = GetLastError()*(-1);
	    printf("DeviceIoControl failed with error %i\n", res*(-1));
    }

    CloseHandle(hdev);
    return res;
}
#else
int pioctl(const char *path, unsigned long com, 
	   struct ViceIoctl *vidata, int follow) 
{
    /* Pack <path, vidata, follow> into a PioctlData structure 
     * since ioctl takes only one data arg. 
     */
    struct PioctlData data;
    int code, fd;

    /* Must change the size field of the command to match 
       that of the new structure. */
    unsigned long cmd = (com & ~(PIOCPARM_MASK << 16)); /* mask out size  */
    int	size = ((com >> 16) & PIOCPARM_MASK) + sizeof(char *) + sizeof(int);

    cmd	|= (size & PIOCPARM_MASK) << 16;  /* or in corrected size */

    data.path = path;
    data.follow = follow;
    data.vi = *vidata;

    fd = open(CTL_FILE, O_RDONLY, 0);
    if (fd < 0) return(fd);

    code = ioctl(fd, cmd, &data);

    (void)close(fd);

    /* Return result of ioctl. */
    return(code);
}
#endif
