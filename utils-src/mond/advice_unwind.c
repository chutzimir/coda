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




/*
 *    Advice Vmon Daemon -- Data Spool Unwinder.
 */


#include "mondgen.h"
#include "mond.h"
#include "advice_parser.h"

#ifdef __cplusplus
extern "C" {
#endif __cplusplus

#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <libc.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <strings.h>
#include <mach.h>
#include "db.h"

#ifdef __cplusplus
}
#endif __cplusplus

#include <stdarg.h>
#include "util.h"
#include "vargs.h"
#include "datalog.h"

#define STREQ(a, b) (strcmp((a), (b)) == 0)
#define LOGNAME "UnwindAdviceLog"
#define LOCKNAME "/usr/mond/bin/UnwindLock"
		      
/* command line arguments */
  
/* unwindp is randomly assigned until RPC2_Init is idempotent */
char *DataBaseName = "codastats";         /* -db */
char *WorkingDir = "/vmon/log";           /* -wd */
int LogLevel = 0;                             /* -d */
bool removeOnDone = mfalse;                     /* -R/r */
bool doLog = mtrue;                            /* -L/l */

PRIVATE FILE *lockFile;
PRIVATE bool done = mfalse;
PRIVATE bool everError = mfalse;

extern int errno;

void Log_Done();

PRIVATE void ParseArgs(int, char*[]);
PRIVATE void SendData(char *);
PRIVATE void InitLog();
PRIVATE void GetDiscoQ(char *filename, bool *error);
PRIVATE void GetReconnQ(char *filename, bool *error);
PRIVATE int ScreenForData(struct direct *);
PRIVATE void ProcessEachUser();
PRIVATE void ProcessEachDataDirectory(char *dirname);
PRIVATE void GetFilesAndSpool(char *datadir);
PRIVATE int TestAndLock();
PRIVATE void RemoveLock();
PRIVATE void InitSignals();
PRIVATE void TermSignal();
PRIVATE void LogErrorPoint(int[]);
PRIVATE void zombie(int, int, struct sigcontext *);

enum adviceClass { adviceDiscoQ, adviceReconnQ, adviceClass_last };
#define DISCO_ID "DisconnectedMiss."
#define RECONN_ID "Reconnection."
#define DATADIR_COMPONENT ".questionnaires"
char *CodaUserDir = "/coda/usr";

FILE *LogFile = 0;
FILE *DataFile = 0;
PRIVATE struct sigcontext OldContext;

main (int argc, char *argv[])
{
    if (TestAndLock()) {
	fprintf(stderr,
		"Another unwind running or abandoned, please check\n");
	exit(-1);
    }
    ParseArgs(argc, argv);

    InitSignals();

    InitLog();

    if (chdir(CodaUserDir)) {
	RemoveLock();
	Die("Could not cd into %s",CodaUserDir);
    }
    if (InitDB(DataBaseName)) {
	RemoveLock();
	fprintf(stderr,"Could not connect to database %s",DataBaseName);
	exit(-1);
   }
    LogMsg(100,LogLevel,LogFile,"Enter ProcessEachUser");
    ProcessEachUser();
    RemoveLock();
    Log_Done();
}

PRIVATE void ParseArgs(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) 
    {
	if (STREQ(argv[i], "-db")) {		/* database */
	    DataBaseName = argv[++i];
	    continue;
	}
	if (STREQ(argv[i], "-wd")) {		/* working directory */
	    WorkingDir = argv[++i];
	    continue;
	}
	else if	(STREQ(argv[i], "-d")) {	/* log level */
	    LogLevel = atoi(argv[++i]);
	    continue;
	}
	else if	(STREQ(argv[i], "-R")) {	/* remove */
	    removeOnDone = mtrue;
	    continue;
	}
	else if	(STREQ(argv[i], "-r")) {	/* don't remove */
	    removeOnDone = mfalse;
	    continue;
	}
	else if	(STREQ(argv[i], "-L")) {	/* log */
	    doLog = mtrue;
	    continue;
	}
	else if	(STREQ(argv[i], "-l")) {	/* don't log */
	    doLog = mfalse;
	    continue;
	}
	printf("usage: myunwind [-db database] [-wd workingDir]\n");
	printf("              [-d logLevel] [-R | -r] [-L | -l]\n");
	RemoveLock();
	exit(1000);
    }
}

PRIVATE void SendData(char *file)
{
    DataFile = fopen(file, "r");
    bool error = mfalse;

    done = mfalse;
    everError = mfalse;
    
    if (DataFile == NULL)
    {
	LogMsg(0,LogLevel,LogFile,"Could not Open %s for reading",file);
	done = mtrue;
	error = mtrue;
    }
    
    int recordCounts[adviceClass_last];
    for (int i=0;i<adviceClass_last;i++)
	recordCounts[i] = 0;

    if (strncmp(file, DISCO_ID, strlen(DISCO_ID)) == 0) {
	GetDiscoQ(file, &error);
	recordCounts[adviceDiscoQ]++;    
    } else if (strncmp(file, RECONN_ID, strlen(RECONN_ID)) == 0) {
	GetReconnQ(file, &error);
	recordCounts[adviceReconnQ]++;
    } else {
	error = mtrue;
	LogMsg(0,LogLevel,LogFile,"Unknown type of file: %s", file);
	LogErrorPoint(recordCounts);
    }

    if (error == mtrue) {
	everError = mtrue;
	LogErrorPoint(recordCounts);
	error = mfalse;
    }

    fclose(DataFile);
    if (everError == mfalse) 
    {
	if (removeOnDone == mtrue)
	{
	    if (unlink(file))
		LogMsg(0,LogLevel,LogFile,"Could not unlink %s, but spooled it with no errors",
		       file);
	}
    } else
	LogMsg(0,LogLevel,LogFile,"Error spooling file %s",file);
}

PRIVATE void GetDiscoQ(char *filename, bool *error)
{
    DiscoMissQ q;
    int sum = 0;

    LogMsg(20,LogLevel,LogFile,"Processing: %s\n", filename);

    sum = ParseDisconQfile(filename, &q);

    if (sum == 0) 
	ReportDiscoQ(&q);
    else
	*error = mtrue;
}

PRIVATE void GetReconnQ(char *filename, bool *error)
{
    ReconnQ q; 
    int sum = 0;

    sum = ParseReconnQfile(filename, &q);

    LogMsg(20,LogLevel,LogFile,"Processing: %s\n", filename);

    if (sum == 0) 
	ReportReconnQ(&q);
    else
	*error = mtrue;
}


PRIVATE void InitLog() {
    char LogFilePath[256];	/* "WORKINGDIR/LOGFILE_PREFIX.MMDD" */
    {
	strcpy(LogFilePath, WorkingDir);
	strcat(LogFilePath, "/");
	strcat(LogFilePath, LOGNAME);
    }

    LogFile = fopen(LogFilePath, "a"); 
    if (LogFile == NULL) {
	fprintf(stderr, "LOGFILE (%s) initialization failed\n", LOGNAME);
	exit(-1);
    }

    struct timeval now;
    gettimeofday(&now, 0);
    char *s = ctime(&now.tv_sec);
    LogMsg(0,LogLevel,LogFile,"LOGFILE initialized with LogLevel = %d at %s",
	     LogLevel, ctime(&now.tv_sec));
    LogMsg(0,LogLevel,LogFile,"My pid is %d",getpid());
}

void Log_Done() {
    struct timeval now;
    gettimeofday(&now, 0);
    LogMsg(0, LogLevel, LogFile, "LOGFILE terminated at %s", ctime(&now.tv_sec));

    fclose(LogFile);
    LogFile = 0;
}

PRIVATE int ScreenForData(struct direct *de)
{
    int hitDiscoQ = 0;
    int hitReconnQ = 0;

    hitDiscoQ = !strncmp(DISCO_ID, de->d_name, strlen(DISCO_ID));
    hitReconnQ = !strncmp(RECONN_ID, de->d_name, strlen(RECONN_ID));

    return(hitDiscoQ || hitReconnQ);
}

PRIVATE int select_nodot(struct direct *de)
{
  if (strcmp(de->d_name, ".") == 0 || 
      strcmp(de->d_name, "..") == 0) 
    return(0);
  else
    return(1);
}

PRIVATE void ProcessEachUser() 
// PEU assumes we are cd'd into the directory containing home directories (e.g. /coda/usr)
// main() takes care of this!
{
    char currentUserDir[MAXPATHLEN];
    struct direct **nameList;
    int rc;

    int numdirs = scandir(".", &(nameList), (PFI)select_nodot, NULL);

    if (numdirs == 0) {
	LogMsg(0,LogLevel,LogFile,"No user directories in %s", CodaUserDir);
	return;    
    }

    for (int i=0; i<numdirs; i++) 
    {
	sprintf(currentUserDir, "%s/%s", nameList[i]->d_name, DATADIR_COMPONENT);
	if ((rc = chdir(currentUserDir)) == 0) {
	    LogMsg(0,LogLevel,LogFile,"Processing USER: %s", currentUserDir);
	    ProcessEachDataDirectory(currentUserDir);
	} else {
	    switch (errno) {
	        case ENOENT:
		    LogMsg(0,LogLevel,LogFile,"User %s --> No Data Directory",nameList[i]->d_name);
		    continue;
	        case EACCES:
		    LogMsg(0,LogLevel,LogFile, "No access rights for %s",currentUserDir);
		    continue;
	        default:
		    LogMsg(0,LogLevel,LogFile, "Could not cd into %s for unknown reason (errno=%d)", currentUserDir, errno);
		    continue;
	    }
        }
    }
}

PRIVATE void ProcessEachDataDirectory(char *userDataDir) 
// PEDD assumes we are cd\'d into the user's directory that contains 
// data directories (e.g. /coda/usr/<foo>/.questionnaires
// PEU takes care of this!
{
    char currentDataSubdir[MAXPATHLEN];
    struct direct **nameList;
    int rc;

    int numdirs = scandir(".", &(nameList), (PFI)select_nodot, NULL);

    if (numdirs == 0) {
	LogMsg(20,LogLevel,LogFile,"No data subdirectories in %s", userDataDir);
	return;    
    }

    for (int i=0; i<numdirs; i++) 
    {
	if ((rc = chdir(nameList[i]->d_name)) == 0) {
	    sprintf(currentDataSubdir, "%s/%s", userDataDir, nameList[i]->d_name);
	    LogMsg(100,LogLevel,LogFile,"Processing datadir: %s",currentDataSubdir);
	    GetFilesAndSpool(currentDataSubdir);
	    if (chdir("..") != 0) {
		LogMsg(0,LogLevel,LogFile, "Could not cd to .. (errno=%d)",errno);
		exit(-1);
	    }
	} else {
	    switch (errno) {
	        case ENOTDIR:
		    LogMsg(20,LogLevel,LogFile, "Ignoring %s", nameList[i]->d_name);
		    continue;
	        case EACCES:
		    LogMsg(0,LogLevel,LogFile, "No access rights for %s", nameList[i]->d_name);
		    continue;
	        default:
		    LogMsg(0,LogLevel,LogFile, "Could not cd into %s for unknown reason (errno=%d)", nameList[i]->d_name, errno);
		    continue;
	    }
        }
    }
}

PRIVATE void GetFilesAndSpool(char *datadir)
// GFAS assumes we are cd'd into the data subdirectory (main does this)
{
    struct direct **nameList;
    int numfiles = scandir(".",&(nameList),
			   (PFI)ScreenForData,NULL);
    if (numfiles == 0) {
	LogMsg(20,LogLevel,LogFile,"No data to spool in directory %s",datadir);
	return;
    }
    LogMsg(100,LogLevel,LogFile,"GFAS: Found %d data files",numfiles);

    for (int i=0; i<numfiles; i++)
    {
	LogMsg(100,LogLevel,LogFile,"GFAS: Sending %s/%s",datadir,nameList[i]->d_name);
	SendData(nameList[i]->d_name);
    }
//    UpdateDB();
}

PRIVATE int TestAndLock()
{
    struct stat buf;
    if (stat(LOCKNAME,&buf) == 0)
	return 1;
    if (errno != ENOENT) {
	fprintf(stderr,"Problem checking lock %s (%d)\n",
	       LOCKNAME,errno);
	return 1;
    }
    lockFile = fopen(LOCKNAME,"w");
    if (lockFile == NULL) {
	fprintf(stderr,"Could not open lock file %s (%d)\n",
	       LOCKNAME,errno);
	return 1;
    }
    fprintf(lockFile,"%d\n",getpid());
    fclose(lockFile);
    return 0;
}

PRIVATE void RemoveLock() {
    if (unlink(LOCKNAME) != 0)
	LogMsg(0,LogLevel,LogFile,"Could not remove lock %s (%d)\n",
	       LOCKNAME,errno);
}

PRIVATE void InitSignals() {
    (void)signal(SIGTERM, (void (*)(int))TermSignal);
    signal(SIGTRAP, (void (*)(int))zombie);
    signal(SIGILL,  (void (*)(int))zombie);
    signal(SIGBUS,  (void (*)(int))zombie);
    signal(SIGSEGV, (void (*)(int))zombie);
    signal(SIGFPE,  (void (*)(int))zombie);  // software exception
}

PRIVATE void zombie(int sig, int code, struct sigcontext *scp) {
    bcopy(scp, &OldContext, (int)sizeof(struct sigcontext));
    LogMsg(0, 0, LogFile,  "****** INTERRUPTED BY SIGNAL %d CODE %d ******", sig, code);
    LogMsg(0, 0, LogFile,  "****** Aborting outstanding transactions, stand by...");
    
    LogMsg(0, 0, LogFile, "To debug via gdb: attach %d, setcontext OldContext", getpid());
    LogMsg(0, 0, LogFile, "Becoming a zombie now ........");
    task_suspend(task_self());
}

PRIVATE void TermSignal() {
    LogMsg(0,LogLevel,LogFile,"Term signal caught, finishing current record");
    // set up things for the unwinder to end after this record
    // to avoid death in the midst of a transaction.
    done = mtrue;
    everError = mtrue;
    return;
}

PRIVATE void LogErrorPoint(int recordCounts[]) {
    int total=0;
    for (int i=0;i<dataClass_last_tag;i++)
	total+=recordCounts[i];
    LogMsg(0,0,LogFile,"Error encountered after processing %d records",
	   total);
    LogMsg(10,LogLevel,LogFile,
	   "\tDisconnected Cache Miss Questionnaires:       %d",recordCounts[adviceDiscoQ]);
    LogMsg(10,LogLevel,LogFile,
	   "\tReconnection Questionnaires:                  %d",recordCounts[adviceReconnQ]);
}
