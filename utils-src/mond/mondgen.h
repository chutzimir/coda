#ifndef _BLURB_
#define _BLURB_
/*
                           CODA FILE SYSTEM
                             Release 3.0
                      School of Computer Science
                      Carnegie Mellon University
                          Copyright 1987-95

This  code  may  be used outside Carnegie Mellon University only under
the terms of the Coda license.  All other use is illegal.
*/


static char *rcsid = "$Header$";
#endif /*_BLURB_*/





#ifndef _MOND_H_
#define _MOND_H_

/* requires lock.h */

#define	LOGFILE_PREFIX	    "mond.log"
#define DATAFILE_PREFIX     "mond.data"

enum bool { mfalse, mtrue };

typedef char* CONDITION;
typedef struct Lock MUTEX;

#define SIGCHAR 32

enum dataClass { SESSION, COMM, CLNTCALL, CLNTMCALL, CLNTRVM, 
		 VCB, ADVICE, MINICACHE, OVERFLOW, SRVCALL,
		 SRVRES, SRVOVRFLW, SRVRVMRES, 
		 IOTINFO, IOTSTAT, SUBTREE, REPAIR, RWSSTAT, dataClass_last_tag };

#define SESSION_TAG   1611899
#define COMM_TAG      2611899
#define CLNTCALL_TAG  3611899
#define CLNTMCALL_TAG 3711899
#define CLNTRVM_TAG   3811899
#define VCB_TAG	      4811899
#define ADVICE_TAG    3911899
#define MINICACHE_TAG 9611899
#define OVERFLOW_TAG  4611899
#define SRVCALL_TAG   5611899
#define SRVRES_TAG    6611899
#define SRVOVRFLW_TAG 7611899
#define SRVRVMRES_TAG 8611899
#define IOTINFO_TAG   1234567
#define IOTSTAT_TAG   2345678
#define	SUBTREE_TAG   3456789
#define REPAIR_TAG    4567890
#define RWSSTAT_TAG   5678901

/* this is the magic number that signifies the start of a collection
   set...
   It's probably a good idea to change it everytime you change the format
   of the data file.
   History:  755515, 
*/

#define MAGIC_NUMBER 425515
#define END_GUARD 679371

#define CLIENT_TABSIZE 512

#endif _MOND_H_
