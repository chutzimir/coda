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

#include "asr.h"

#if defined(__linux__) || defined(__NetBSD__)
#include <unistd.h>
#else
#include <libc.h>
#endif 
#include <sys/param.h>
#include <sys/dir.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>    
#include <vcrcommon.h>


#ifdef __cplusplus
}
#endif __cplusplus


#if LINUX || NetBSD

/* An implementation of path(3) which is a standard function in Mach OS
 * the behaviour is according to man page in Mach OS, which says,
 *
 *    The handling of most names is obvious, but several special
 *    cases exist.  The name "f", containing no slashes, is split
 *    into directory "." and filename "f".  The name "/" is direc-
 *    tory "/" and filename ".".  The path "" is directory "." and
 *    filename ".".
 *       -- manpage of path(3)
 */
#include <string.h>

void path(char *pathname, char *direc, char *file)
{
  char *maybebase, *tok;
  int num_char_to_be_rm;

  if (strlen(pathname)==0) {
    strcpy(direc, ".");
    strcpy(file, ".");
    return;
  }
  if (strchr(pathname, '/')==0) {
    strcpy(direc, ".");
    strcpy(file, pathname);
    return;
  } 
  if (strcmp(pathname, "/")==0) {
    strcpy(direc, "/");
    strcpy(file, ".");
    return;
  }
  strcpy(direc, pathname);
  maybebase = strtok(direc,"/");
  while (tok = strtok(0,"/")) 
    maybebase = tok;
  strcpy(file, maybebase);
  strcpy(direc, pathname);
  num_char_to_be_rm = strlen(file) + 
    (direc[strlen(pathname)-1]=='/' ? 1 : 0);/* any trailing slash ? */
  *(direc+strlen(pathname)-num_char_to_be_rm) = '\0';
    /* removing the component for file from direc */
  if (strlen(direc)==0) strcpy(direc,"."); /* this happen when pathname 
                                            * is "name/", for example */
  if (strlen(direc)>=2) /* don't do this if only '/' remains in direc */
    if (*(direc+strlen(direc)-1) == '/' )
      *(direc+strlen(direc)-1) = '\0'; 
       /* remove trailing slash in direc */
  return;
}

#endif


