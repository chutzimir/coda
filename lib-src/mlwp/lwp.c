/* BLURB lgpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the  terms of the  GNU  Library General Public Licence  Version 2,  as
shown in the file LICENSE. The technical and financial contributors to
Coda are listed in the file CREDITS.

                        Additional copyrights

#*/

/*
                         IBM COPYRIGHT NOTICE

                          Copyright (C) 1986
             International Business Machines Corporation
                         All Rights Reserved

This  file  contains  some  code identical to or derived from the 1986
version of the Andrew File System ("AFS"), which is owned by  the  IBM
Corporation.   This  code is provided "AS IS" and IBM does not warrant
that it is free of infringement of  any  intellectual  rights  of  any
third  party.    IBM  disclaims  liability of any kind for any damages
whatsoever resulting directly or indirectly from use of this  software
or  of  any  derivative work.  Carnegie Mellon University has obtained
permission to  modify,  distribute and sublicense this code,  which is
based on Version 2  of  AFS  and  does  not  contain  the features and
enhancements that are part of  Version 3 of  AFS.  Version 3 of AFS is
commercially   available   and  supported  by   Transarc  Corporation,
Pittsburgh, PA.

*/

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "coda_assert.h"
#include <sys/time.h>
#ifdef __CYGWIN32__
#include <time.h>
#endif

#include <sys/types.h>
#include <sys/mman.h>

#include "lwp.h"
#include "lwp.private.h"

#define  ON	    1
#define  OFF	    0
#define  READY	    2
#define  WAITING    3
#define  DESTROYED  4
#define  QWAITING   5
#define  MAXINT     (~(1<<((sizeof(int)*8)-1)))
#define  MINSTACK   44
#define  MAX(a,b)   ((a) > (b) ? (a) : (b))
#define	 LWPANCHOR  (*lwp_init)
#define	 MAX_PRIORITIES	(LWP_MAX_PRIORITY+1)



struct QUEUE {
    PROCESS	head;
    int		count;
} runnable[MAX_PRIORITIES], blocked;

/* Invariant for runnable queues: The head of each queue points to the
currently running process if it is in that queue, or it points to the
next process in that queue that should run. */

/* Macro to force a re-schedule.  Strange name is historical */
#define Set_LWP_RC() savecontext(Dispatcher, &lwp_cpptr->context, NULL)

/* internal procedure declarations */
static void lwpremove(register PROCESS p, register struct QUEUE *q);
static void lwpinsert(register PROCESS p, register struct QUEUE *q);
static void lwpmove(PROCESS p, struct QUEUE *from, struct QUEUE *to);
static void Dispatcher();
static void Initialize_PCB (PROCESS temp, int priority, char *stack, int stacksize, PFIC ep, char *parm, char *name);
static int  Internal_Signal(char *event);
static void Abort_LWP(char *msg);
static void Exit_LWP();
static void Dump_One_Process (PROCESS pid, FILE *fp, int dofree);
static void Dump_Processes (int magic);
static void purge_dead_pcbs();
static void Delete_PCB(register PROCESS pid);
static void Free_PCB(PROCESS pid);
static void Dispose_of_Dead_PCB(PROCESS cur);

static void Create_Process_Part2 ();
static void Overflow_Complain ();
static void Initialize_Stack (char *stackptr, int stacksize);
static int  Stack_Used (register char *stackptr, int stacksize);
static int  InitializeProcessSupport();

#if defined(DJGPP) || defined(__CYGWIN32__)
typedef void *register_t;
#endif

/*----------------------------------------*/
/* Globals identical in  OLD and NEW lwps */
/*----------------------------------------*/

FILE *lwp_logfile = NULL;
signed char    lwp_debug;
FILE   *lwp_logfile;
int 	LWP_TraceProcesses = 0;
PROCESS	lwp_cpptr;
int lwp_nextindex;		    /* Next lwp index to assign */
static struct lwp_ctl *lwp_init = 0;
int	Cont_Sws;
struct timeval last_context_switch; /* used to find out how long a lwp was running */
struct timeval cont_sw_threshold; /* how long a lwp is allowed to run */
PROCESS cont_sw_id;		  /* id of thread setting the last_context_switch time */

/* The global Highest_runnable_priority is only needed in NEW lwp.
    But it gets set within a for_all_elts() instance in
    InternalSignal().  Causes Sun's CPP to choke.  Hence not placed
    under #ifdef OLDLWP.  */

/* XXX this parameter is not needed see comment above */
int	Highest_runnable_priority;	/* global variable for max priority */
struct timeval run_wait_threshold;

/*-----------------------------------------*/
/* Globals that differ in OLD and NEW lwps */
/*-----------------------------------------*/


int lwp_overflowAction = LWP_SOABORT;	/* Stack checking action */
int lwp_stackUseEnabled = TRUE;		/* Controls stack size counting */
int stack_offset;			/* Offset of stack field within pcb */


/*---------------------------------------*/
/* Routines identical in OLD and NEW lwp */
/*---------------------------------------*/

#ifndef timercmp
#define timercmp(tvp, uvp, cmp) \
        ((tvp)->tv_sec cmp (uvp)->tv_sec || \
         (tvp)->tv_sec == (uvp)->tv_sec && (tvp)->tv_usec cmp (uvp)->tv_usec)
#endif 

/* Iterator macro */
#define for_all_elts(var, q, body)\
	{\
	    PROCESS var, _NEXT_;\
	    int _I_;\
	    for (_I_=q.count, var = q.head; _I_>0; _I_--, var=_NEXT_) {\
		_NEXT_ = var -> next;\
		body\
	    }\
	}



/* removes PROCESS p from a QUEUE pointed at by q */
static void lwpremove(p, q)
    register PROCESS p;
    register struct QUEUE *q;
{
    /* Special test for only element on queue */
    if (q->count == 1)
	q -> head = NULL;
    else {
	/* Not only element, do normal remove */
	p -> next -> prev = p -> prev;
	p -> prev -> next = p -> next;
    }
    /* See if head pointing to this element */
    if (q->head == p) q -> head = p -> next;
    q->count--;
    p -> next = p -> prev = NULL;
}

static void lwpinsert(p, q)
    register PROCESS p;
    register struct QUEUE *q;
{
    if (q->head == NULL) {	/* Queue is empty */
	q -> head = p;
	p -> next = p -> prev = p;
    } else {			/* Regular insert */
	p -> prev = q -> head -> prev;
	q -> head -> prev -> next = p;
	q -> head -> prev = p;
	p -> next = q -> head;
    }
    q->count++;
}

/* Moves a PROCESS p from QUEUE "from" to QUEUE "to" */
static void lwpmove(p, from, to)
    PROCESS p;
    struct QUEUE *from;
    struct QUEUE *to;
{
    lwpremove(p, from);
    lwpinsert(p, to);
}

int LWP_TerminateProcessSupport()       /* terminate all LWP support */
{
    register int i;

    lwpdebug(0, "Entered Terminate_Process_Support");
    if (lwp_init == NULL) return LWP_EINIT;
    if (lwp_cpptr != LWPANCHOR.outerpid)
	/* terminate support not called from same process as the init process */
        Abort_LWP("Terminate_Process_Support invoked from wrong process!");
    /* free all space allocated */
    for (i=0; i<MAX_PRIORITIES; i++)
        for_all_elts(cur, runnable[i], { Free_PCB(cur);})
    for_all_elts(cur, blocked, { Free_PCB(cur);})
    free((char *)lwp_init);
    lwp_init = NULL;
    return LWP_SUCCESS;
}



int LWP_GetRock(int Tag, char **Value)
{
    /* Obtains the pointer Value associated with the rock Tag of this LWP.
       Returns:
            LWP_SUCCESS    if specified rock exists and Value has been filled
            LWP_EBADROCK   rock specified does not exist
    */
    register int i;
    register struct rock *ra;

    ra = lwp_cpptr->rlist;

    for (i = 0; i < lwp_cpptr->rused; i++)
        if (ra[i].tag == Tag) {
            *Value =  ra[i].value;
            return(LWP_SUCCESS);
	}
    return(LWP_EBADROCK);
}


int LWP_NewRock(int Tag, char *Value)
{
    /* Finds a free rock and sets its value to Value.
        Return codes:
                LWP_SUCCESS     Rock did not exist and a new one was used
                LWP_EBADROCK    Rock already exists.
                LWP_ENOROCKS    All rocks are in use.

        From the above semantics, you can only set a rock value once.
        This is specifically to prevent multiple users of the LWP
        package from accidentally using the same Tag value and
        clobbering others.  You can always use one level of
        indirection to obtain a rock whose contents can change.  */
    
    register int i;
    register struct rock *ra;   /* rock array */

    ra = lwp_cpptr->rlist;

    /* check if rock has been used before */
    for (i = 0; i < lwp_cpptr->rused; i++)
        if (ra[i].tag == Tag) return(LWP_EBADROCK);

    /* insert new rock in rock list and increment count of rocks */
    if (lwp_cpptr->rused < MAXROCKS) {
        ra[lwp_cpptr->rused].tag = Tag;
        ra[lwp_cpptr->rused].value = Value;
        lwp_cpptr->rused++;
        return(LWP_SUCCESS);
    }
    else return(LWP_ENOROCKS);
}

static void Dispose_of_Dead_PCB(PROCESS cur)
{

  lwpdebug(0, "Entered Dispose_of_Dead_PCB");
  Delete_PCB(cur);
  Free_PCB(cur);
}

int LWP_CurrentProcess(PROCESS *pid)
{
    lwpdebug(0, "Entered LWP_CurrentProcess");
    if (lwp_init) {
            *pid = lwp_cpptr;
            return LWP_SUCCESS;
    } else
        return LWP_EINIT;
}

PROCESS LWP_ThisProcess()
{
    lwpdebug(0, "Entered LWP_ThisProcess");
    if (lwp_init) {
            return lwp_cpptr;
    } else
	    return NULL;
}


void LWP_SetLog(FILE *file, int level)
{
	if ( file ) 
		lwp_logfile = file;
	lwp_debug = level;
}
	

int LWP_GetProcessPriority(PROCESS pid, int *priority)
{
    lwpdebug(0, "Entered Get_Process_Priority");
    if (lwp_init) {
	*priority = pid -> priority;
	return 0;
    } else
	return LWP_EINIT;
}

int LWP_WaitProcess(char *event)
{
    char *tempev[2];

    lwpdebug(0, "Entered Wait_Process");
    if (event == NULL) return LWP_EBADEVENT;
    tempev[0] = event;
    tempev[1] = NULL;
    return LWP_MwaitProcess(1, tempev);
}

static void Delete_PCB(register PROCESS pid)
{
    lwpdebug(0, "Entered Delete_PCB");
    lwpremove(pid, (pid->blockflag || pid->status==WAITING || pid->status==DESTROYED
		 ? &blocked
		 : &runnable[pid->priority]));
    LWPANCHOR.processcnt--;
}

static void purge_dead_pcbs()
{
    for_all_elts(cur, blocked, { if (cur->status == DESTROYED) Dispose_of_Dead_PCB(cur); })
}

static void Exit_LWP()
{
    exit (-1);
}


#define FREE_STACKS 1
#define DONT_FREE   0

static void Dump_Processes(int magic)
{
    /* This function is too easily mistaken for LWP_PrintProcesses (I did it a
     * couple of times already) --JH */
    if (magic != 0xdeadbeef) return;

    if (lwp_init) {
	register int i;
	for (i=0; i<MAX_PRIORITIES; i++)
	    for_all_elts(x, runnable[i], {
		fprintf(lwp_logfile, "[Priority %d]\n", i);
		Dump_One_Process(x, lwp_logfile, FREE_STACKS);
	    })
	for_all_elts(x, blocked, { Dump_One_Process(x, lwp_logfile, FREE_STACKS); })
    } else
	fprintf(lwp_logfile, "***LWP: LWP support not initialized\n");
}

void LWP_Print_Processes()
{
    if (lwp_init) {
	register int i;
	for (i=0; i<MAX_PRIORITIES; i++)
	    for_all_elts(x, runnable[i], {
		fprintf(lwp_logfile, "[Priority %d]\n", i);
		Dump_One_Process(x, lwp_logfile, DONT_FREE);
	    })
	for_all_elts(x, blocked, { Dump_One_Process(x, lwp_logfile, DONT_FREE); })
    } else
	fprintf(lwp_logfile, "***LWP: LWP support not initialized\n");
}

char *LWP_Name()
{
    return(lwp_cpptr->name);    
}

int LWP_Index()
{
    return(lwp_cpptr->index);
}

int LWP_HighestIndex()
{
    return(lwp_nextindex-1);
}

static int IsGreater(struct timeval *t1, struct timeval *t2) 
{
    if (t1->tv_sec > t2->tv_sec) 
	return (1);
    else if (t1->tv_sec < t2->tv_sec) 
	return (0);
    else if (t1->tv_usec > t2->tv_usec) 
	return(1);
    else 
	return(0);
}

static void CheckWorkTime(PROCESS currentThread, PROCESS nextThread) 
{
    struct timeval current;
    struct timeval worktime;

    if (!cont_sw_threshold.tv_sec  && !cont_sw_threshold.tv_usec) return;

    if ((last_context_switch.tv_sec != 0) && 
	(cont_sw_id == currentThread)) {
	gettimeofday(&current, NULL);
	worktime.tv_sec = current.tv_sec;
	worktime.tv_usec = current.tv_usec;
	if (worktime.tv_usec < last_context_switch.tv_usec) {
	    worktime.tv_usec += 1000000;
	    worktime.tv_sec -= 1;
	}
	worktime.tv_sec -= last_context_switch.tv_sec;
	worktime.tv_usec -= last_context_switch.tv_usec;

	if (IsGreater(&worktime, &cont_sw_threshold)) {
	    struct tm *lt = localtime((const time_t *)&current.tv_sec);
	    fprintf(stderr, "[ %02d:%02d:%02d ] ***LWP %s(%p) took too much cpu %d secs %6d usecs\n", 
		    lt->tm_hour, lt->tm_min, lt->tm_sec, 
		    currentThread->name, currentThread, (int)worktime.tv_sec, (int)worktime.tv_usec);
	    fflush(stderr);	    
	}
	last_context_switch.tv_sec = current.tv_sec;
	last_context_switch.tv_usec = current.tv_usec;
    }
    else gettimeofday(&last_context_switch, NULL);
    cont_sw_id = nextThread;
}


static void CheckRunWaitTime(PROCESS thread) 
{
    struct timeval current;
    struct timeval waittime;

    if (!timerisset(&run_wait_threshold)) return;
    /* timer can be null during process creation. */
    if (!timerisset(&thread->lastReady)) return;

    gettimeofday(&current, NULL);
    waittime.tv_sec = current.tv_sec;
    waittime.tv_usec = current.tv_usec;
    if (waittime.tv_usec < thread->lastReady.tv_usec) {
	waittime.tv_usec += 1000000;
	waittime.tv_sec -= 1;
    }
    waittime.tv_sec -= thread->lastReady.tv_sec;
    waittime.tv_usec -= thread->lastReady.tv_usec;

    if (timercmp(&waittime, &run_wait_threshold, >)) {
	struct tm *lt = localtime((const time_t *)&current.tv_sec);
	fprintf(stderr, "[ %02d:%02d:%02d ] ***LWP %s(%p) run-wait too long %d secs %6d usecs\n", 
		lt->tm_hour, lt->tm_min, lt->tm_sec, 
		thread->name, thread, (int)waittime.tv_sec, (int)waittime.tv_usec);
	fflush(stderr);	    
    }
    timerclear(&thread->lastReady);
}

/*------------------------------------------*/
/* Routines that differ in OLD and NEW lwps */
/*------------------------------------------*/

/* A process calls this routine to wait until somebody signals it.
 * LWP_QWait removes the calling process from the runnable queue
 * and makes the process sleep until some other process signals via LWP_QSignal.
 */
int LWP_QWait()
{
    register PROCESS tp;
    (tp=lwp_cpptr) -> status = QWAITING;
    lwpremove(tp, &runnable[tp->priority]);
    timerclear(&tp->lastReady);
    Set_LWP_RC();
    return LWP_SUCCESS;
}


/* signal the PROCESS pid - by adding it to the runnable queue */
int LWP_QSignal(register PROCESS pid)
{

    if (pid->status == QWAITING) {
        lwpdebug(0, "LWP_Qsignal: %s is going to QSignal %s\n", 
		 lwp_cpptr->name, pid->name);
	pid->status = READY;
	lwpinsert(pid, &runnable[pid->priority]);
	lwpdebug(0, "LWP_QSignal: Just inserted %s into runnable queue\n", pid->name);
	gettimeofday(&pid->lastReady, 0);
	return LWP_SUCCESS;	
    }
    else return LWP_ENOWAIT;
}

#ifdef __linux__
char *lwp_stackbase = (char *)0x15000000;
#endif
#ifdef __BSD44__
char *lwp_stackbase = (char *)0x45000000;
#endif

int LWP_CreateProcess(PFIC ep, int stacksize, int priority, char *parm, 
		      char *name, PROCESS *pid)
{

    PROCESS temp, temp2;
    char *stackptr;
    int pagesize;

    lwpdebug(0, "Entered LWP_CreateProcess");
    /* Throw away all dead process control blocks */
    purge_dead_pcbs();
    if (lwp_init) {
	temp = (PROCESS) malloc (sizeof (struct lwp_pcb));
	if (temp == NULL) {
	    Set_LWP_RC();
	    return LWP_ENOMEM;
	}
	if (stacksize < MINSTACK)
	    stacksize = 1000;
	else
	    stacksize = 4 * ((stacksize+3) / 4);
#if defined(__linux__) || defined(__BSD44__)
	pagesize = getpagesize();
	stackptr = (char *) mmap(lwp_stackbase, stacksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if ( stackptr == MAP_FAILED ) {
		perror("stack: ");
		CODA_ASSERT(0);
	}
#if 0
	sprintf(msg, "STACK: from %p to %p, for %s\n", lwp_stackbase, lwp_stackbase + stacksize, name);
	write(2, msg, strlen(msg));
#endif
	lwp_stackbase += ((stacksize/pagesize) + 2) * pagesize;
#else
	stackptr = (char *) malloc(stacksize);
#endif
	if (stackptr == NULL) {
	    Set_LWP_RC();
	    return LWP_ENOMEM;
        }
	if (priority < 0 || priority >= MAX_PRIORITIES) {
	    Set_LWP_RC();
	    return LWP_EBADPRI;
	}
 	Initialize_Stack(stackptr, stacksize);
	Initialize_PCB(temp, priority, stackptr, stacksize, ep, parm, name);
	lwpinsert(temp, &runnable[priority]);
	gettimeofday(&temp->lastReady, 0);
	temp2 = lwp_cpptr;

	if (PRE_Block != 0) 
		Abort_LWP("PRE_Block not 0");

	/* Gross hack: beware! */
	PRE_Block = 1;
	lwp_cpptr = temp;
	savecontext(Create_Process_Part2, &temp2->context, stackptr+stacksize-STACK_PAD);
	/* End of gross hack */

	Set_LWP_RC();
	*pid = temp;
	return 0;
    } else
	    return LWP_EINIT;
}



int LWP_DestroyProcess(PROCESS pid)
{
	PROCESS temp;

	lwpdebug(0, "Entered Destroy_Process");
	if (lwp_init) {
		if (lwp_cpptr != pid) {
			Dispose_of_Dead_PCB(pid);
			Set_LWP_RC();
		} else {
			pid -> status = DESTROYED;
			lwpmove(pid, &runnable[pid->priority], &blocked);
			temp = lwp_cpptr;
			savecontext(Dispatcher, &(temp -> context),
				    &(LWPANCHOR.dsptchstack[(sizeof LWPANCHOR.dsptchstack)-STACK_PAD]));
		}

		return LWP_SUCCESS;
	} else
		return LWP_EINIT;
}

/* explicit voluntary preemption */
int LWP_DispatchProcess()
{
	lwpdebug(0, "Entered Dispatch_Process");
	if (lwp_init) {
		Set_LWP_RC();
		return LWP_SUCCESS;
	} else
		return LWP_EINIT;
}


/* New initialization procedure; checks header and library versions
   Use this instead of LWP_InitializeProcessSupport() First argument
   should always be LWP_VERSION.  */

int LWP_Init(int version, int priority, PROCESS *pid)
{

    lwp_logfile = stderr;
    if (version != LWP_VERSION) {
	    fprintf(stderr, "**** FATAL ERROR: LWP VERSION MISMATCH ****\n");
	    exit(-1);
    } else 
	    return(InitializeProcessSupport(priority, pid));    
}


/* Used to be externally visible as  LWP_InitializeProcessSupport() */
static int InitializeProcessSupport(int priority, PROCESS *pid)
{
	PROCESS temp;
	struct lwp_pcb dummy;
	register int i;
	
	lwpdebug(0, "Entered InitializeProcessSupport");
	if (lwp_init != NULL) 
		return LWP_SUCCESS;

	/* Set up offset for stack checking -- do this as soon as possible */
	stack_offset = (char *) &dummy.stack - (char *) &dummy;

	if (priority >= MAX_PRIORITIES) 
		return LWP_EBADPRI;
	for (i=0; i<MAX_PRIORITIES; i++) {
		runnable[i].head = NULL;
		runnable[i].count = 0;
	}
	blocked.head = NULL;
	blocked.count = 0;
	lwp_init = (struct lwp_ctl *) malloc(sizeof(struct lwp_ctl));
	temp = (PROCESS) malloc(sizeof(struct lwp_pcb));
	if (lwp_init == NULL || temp == NULL)
		Abort_LWP("Insufficient Storage to Initialize LWP Support");
	LWPANCHOR.processcnt = 1;
	LWPANCHOR.outerpid = temp;
	LWPANCHOR.outersp = NULL;
	Initialize_PCB(temp, priority, NULL, 0, NULL, NULL, "Main Process");
	lwpinsert(temp, &runnable[priority]);
	gettimeofday(&temp->lastReady, 0);
	savecontext(Dispatcher, &temp->context, NULL);
	LWPANCHOR.outersp = temp -> context.topstack;
	Set_LWP_RC();
	*pid = temp;
	return LWP_SUCCESS;
}

int LWP_INTERNALSIGNAL(char *event, int yield)
{
	lwpdebug(0, "Entered LWP_SignalProcess");
	if (lwp_init) {
		int rc;
		rc = Internal_Signal(event);
	if (yield) 
		Set_LWP_RC();
	return rc;
	} else
		return LWP_EINIT;
}

/* wait on m of n events */
int LWP_MwaitProcess(int wcount, char **evlist)
{
	register int ecount, i;

	lwpdebug(0, "Entered Mwait_Process [waitcnt = %d]", wcount);
	if (evlist == NULL) {
		Set_LWP_RC();
		return LWP_EBADCOUNT;
	}
	for (ecount = 0; evlist[ecount] != NULL; ecount++) ;
	if (ecount == 0) {
		Set_LWP_RC();
		return LWP_EBADCOUNT;
	}
	if (lwp_init) {
		if (wcount>ecount || wcount<0) {
			Set_LWP_RC();
			return LWP_EBADCOUNT;
		}
		if (ecount > lwp_cpptr->eventlistsize) {
			lwp_cpptr->eventlist = 
				(char **)realloc((char *)lwp_cpptr->eventlist, 
						 ecount*sizeof(char *));
			lwp_cpptr->eventlistsize = ecount;
		}
		for (i=0; i<ecount; i++) 
			lwp_cpptr -> eventlist[i] = evlist[i];
		if (wcount > 0) {
			lwp_cpptr -> status = WAITING;
			lwpmove(lwp_cpptr, &runnable[lwp_cpptr->priority], &blocked);
			timerclear(&lwp_cpptr->lastReady);
		}
		lwp_cpptr -> wakevent = 0;
		lwp_cpptr -> waitcnt = wcount;
		lwp_cpptr -> eventcnt = ecount;
		Set_LWP_RC();
		return LWP_SUCCESS;
	}
	return LWP_EINIT;
}


int LWP_StackUsed(PROCESS pid, int *max, int *used)
{
    *max = pid -> stacksize;
    *used = Stack_Used(pid->stack, *max);
    if (*used == 0)
	return LWP_NO_STACK;
    return LWP_SUCCESS;
}

static void Abort_LWP(char *msg)
{
    struct lwp_context tempcontext;

    lwpdebug(0, "Entered Abort_LWP");
    printf("***LWP Abort: %s\n", msg);
    Dump_Processes(0xdeadbeef);
    if (LWPANCHOR.outersp == NULL)
	    Exit_LWP();
    else
	    savecontext(Exit_LWP, &tempcontext, LWPANCHOR.outersp);
}

static void Create_Process_Part2()
{
    PROCESS temp;
    lwpdebug(0, "Entered Create_Process_Part2");
    temp = lwp_cpptr;		/* Get current process id */
    savecontext(Dispatcher, &temp->context, NULL);
    (*temp->ep)(temp->parm);
    LWP_DestroyProcess(temp);
}

#if defined(i386)
/* set lwp_trace_depth to < 0 to trace the complete stack.
 * set it to > 0 to set the maximum trace depth. */
static int lwp_trace_depth=-1;

/* Stack crawling bits */

/* register file */
struct regfile {
    register_t   edi;
    register_t   esi;
    register_t   ebp;
    register_t   unused;
    register_t   ebx;
    register_t   edx;
    register_t   ecx;
    register_t   eax;
};

static void Trace_Swapped_Stack(top, fp, depth, name)
    caddr_t top;
    FILE    *fp;
    int     depth;
    char    *name;
{
    struct regfile  reg;
    register_t      ip;
    register_t      esp;
    
    /* Set current stack pointer to top */
    esp = (register_t)top;

    /* Simulate a POPA */
    reg = *(struct regfile *)esp;
    esp += sizeof(struct regfile);

    /* Pop the return address (RET) */
    ip = *(register_t *)esp;
    esp+=sizeof(register_t);

    /* 
     * We are now at the bottom of the frame: 
     * esp = end of frame.
     * ebp = beginning of frame.
     * ip = caller of savecontext 
     */
    while (depth--) {
	fprintf(fp,"\tStack: %s - 0x%x\n", name, ip);

        /* make sure we don't segfault while going to the next frame --JH */
        if (reg.ebp == 0) break;

	if (depth) {
	    /* LEAVE */
	    esp = reg.ebp;
	    reg.ebp = *(register_t *)esp;
	    esp += sizeof(register_t);
	    /* RET */
	    ip = *(register_t *)esp;
	    esp += sizeof(register_t);
	}
    }
}
#endif


static void Dump_One_Process(pid, fp, dofree)
    PROCESS pid;
    FILE    *fp;
    int     dofree;
{
    int i;

    fprintf(fp,"***LWP: Process Control Block at %p\n", pid);
    fprintf(fp,"***LWP: Name: %s\n", pid->name);
    if (pid->ep != NULL)
	fprintf(fp,"***LWP: Initial entry point: %p\n", pid->ep);
    if (pid->blockflag) fprintf(fp,"BLOCKED and ");
    switch (pid->status) {
	case READY:	fprintf(fp,"READY");     break;
	case WAITING:	fprintf(fp,"WAITING");   break;
	case DESTROYED:	fprintf(fp,"DESTROYED"); break;
	default:	fprintf(fp,"unknown");
	}
    fprintf(fp, "\n");
    fprintf(fp,"***LWP: Priority: %d \t\tInitial parameter: %p\n",
	    pid->priority, pid->parm);

    if (pid->stacksize != 0) {
	fprintf(fp,"***LWP: Stacksize: %d \tStack base address: %p\n",
		pid->stacksize, pid->stack);
	fprintf(fp,"***LWP: HWM stack usage: ");
	fprintf(fp,"%d\n", Stack_Used(pid->stack,pid->stacksize));
	if (dofree == FREE_STACKS) {
#if defined(__linux__) || defined(__BSD44__)
	    munmap(pid->stack, pid->stacksize);
#else
	    free (pid->stack);
#endif
	    }
	}
    fprintf(fp,"***LWP: Current Stack Pointer: %p\n", pid->context.topstack);

    /* Add others here as needed */
#if defined(i386)
    if (lwp_cpptr == pid) {
	    fprintf(fp, "\tCURRENTLY RUNNING\n");
    } else {
	    Trace_Swapped_Stack(pid->context.topstack, fp, 
				lwp_trace_depth, pid->name);
    }
#endif    

    if (pid->eventcnt > 0) {
	fprintf(fp,"***LWP: Number of events outstanding: %d\n", pid->waitcnt);
	fprintf(fp,"***LWP: Event id list:");
	for (i=0;i<pid->eventcnt;i++)
	    fprintf(fp," %p", pid->eventlist[i]);
	fprintf(fp,"\n");
    }
    if (pid->wakevent>0)
	fprintf(fp,"***LWP: Number of last wakeup event: %d\n", pid->wakevent);

    fprintf(fp,"==========================================\n");
}

static void Dispatcher()		/* Lightweight process dispatcher */
{
    register int i;
    static int dispatch_count = 0;
    PROCESS old_cpptr;

    if (LWP_TraceProcesses > 0) {
	for (i=0; i<MAX_PRIORITIES; i++) {
	    printf("[Priority %d, runnable (%d):", i, runnable[i].count);
	    for_all_elts(p, runnable[i], {
		printf(" \"%s\"", p->name);
	    })
	    puts("]");
    	}
	printf("[Blocked (%d):", blocked.count);
	for_all_elts(p, blocked, {
	    printf(" \"%s\"", p->name);
	})
	puts("]");
    }

    /* Check for stack overflowif this lwp has a stack.  Check for
       the guard word at the front of the stack being damaged and
       for the stack pointer being below the front of the stack.
       WARNING!  This code assumes that stacks grow downward. */
    if (lwp_cpptr != NULL && lwp_cpptr->stack != NULL
	    && (lwp_cpptr->stackcheck != *(long *)(lwp_cpptr->stack)
		|| lwp_cpptr->context.topstack < lwp_cpptr->stack)) {
	switch (lwp_overflowAction) {
	    case LWP_SOQUIET:
		break;
	    case LWP_SOABORT:
		Overflow_Complain();
		abort ();
	    case LWP_SOMESSAGE:
	    default:
		Overflow_Complain();
		lwp_overflowAction = LWP_SOQUIET;
		break;
	}
	}
    /* Move head of current runnable queue forward if current LWP is still in it. */
    if (lwp_cpptr != NULL && lwp_cpptr == runnable[lwp_cpptr->priority].head) 
	runnable[lwp_cpptr->priority].head = runnable[lwp_cpptr->priority].head -> next;

    /* Find highest priority with runnable processes. */
    for (i=MAX_PRIORITIES-1; i>=0; i--)
	if (runnable[i].head != NULL) break;
    if (i < 0) Abort_LWP("No READY processes");

    if (LWP_TraceProcesses > 0)
	printf("Dispatch %d [PCB at %p] \"%s\"\n", 
         ++dispatch_count, runnable[i].head, runnable[i].head->name);
    if (PRE_Block != 1) 
	    Abort_LWP("PRE_Block not 1");
    old_cpptr = lwp_cpptr;
    if (old_cpptr)
        gettimeofday(&old_cpptr->lastReady, 0);	/* back in queue */
    lwp_cpptr = runnable[i].head;
    Cont_Sws++; /* number of context switches, for statistics */

    /* check time to context switch */
    if (cont_sw_threshold.tv_sec || cont_sw_threshold.tv_usec) 
	CheckWorkTime(old_cpptr, lwp_cpptr);

    /* check time waiting to run */
    if (timerisset(&run_wait_threshold))
	CheckRunWaitTime(lwp_cpptr);

    returnto(&lwp_cpptr->context);

}


static void Free_PCB(PROCESS pid)
{
    lwpdebug(0, "Entered Free_PCB");

    if (pid -> stack != NULL) {
	lwpdebug(0, "HWM stack usage: %d, [PCB at %p]",
		   Stack_Used(pid->stack,pid->stacksize), pid);
#if defined(__linux__) || defined(__BSD44__)
	munmap(pid->stack, pid->stacksize);
#else
	free(pid -> stack);
#endif
    }

    if (pid->eventlist != NULL)  free((char *)pid->eventlist);
    free((char *)pid);
}	

static void Initialize_PCB(PROCESS temp, int priority, char *stack, 
			   int stacksize, PFIC ep, char *parm, char *name)
{
	register int i = 0;

	lwpdebug(0, "Entered Initialize_PCB");
	if (name != NULL)
		while (((temp -> name[i] = name[i]) != '\0') && (i < 31)) 
			i++;
	temp -> name[31] = '\0';
	temp -> status = READY;
	temp -> eventlist = (char **)malloc(EVINITSIZE*sizeof(char *));
	temp -> eventlistsize = EVINITSIZE;
	temp -> eventcnt = 0;
	temp -> wakevent = 0;
	temp -> waitcnt = 0;
	temp -> blockflag = 0;
	temp -> iomgrRequest = 0;
	temp -> priority = priority;
	temp -> index = lwp_nextindex++;
	temp -> ep = ep;
	temp -> parm = parm;
	temp -> misc = NULL;	/* currently unused */
	temp -> next = NULL;
	temp -> prev = NULL;
	temp -> rused = 0;
	temp -> level = 1;		/* non-preemptable */
	timerclear(&temp->lastReady);

	temp -> stack = stack;
	temp -> stacksize = stacksize;
	if (temp -> stack != NULL)
		temp -> stackcheck = *(long *) (temp -> stack);

	lwpdebug(0, "Leaving Initialize_PCB\n");
}


static int Internal_Signal(char *event)
{
    int rc = LWP_ENOWAIT;
    int i;

    lwpdebug(0, "Entered Internal_Signal [event id %p]", event);
    if (!lwp_init) return LWP_EINIT;
    if (event == NULL) return LWP_EBADEVENT;

    for_all_elts(temp, blocked, {     /* for all pcb's on the blocked q */
        if (temp->status == WAITING)
	{
            for (i=0; i < temp->eventcnt; i++)
	    { /* check each event in list */
                if (temp -> eventlist[i] == event)
		{
                    temp -> eventlist[i] = NULL;
                    rc = LWP_SUCCESS;
                    /* reduce waitcnt by 1 for the signal */
                    /* if wcount reaches 0 then make the process runnable */
                    if (--temp->waitcnt == 0)
		    {
                        temp -> status = READY;
                        temp -> wakevent = i+1;
                        lwpmove(temp, &blocked, &runnable[temp->priority]);
			gettimeofday(&temp->lastReady, 0);
			/* update Highest_runnable_priority

			   BOGUS ALERT: this  assignment is needed only in 
			   NEW lwp.  But we are within a for_all_elts() macro, and the
			   Sun cpp chokes if we #ifdef the following assignment.
			   Doing this in OLD lwp is innocuous anyway.
			*/
			Highest_runnable_priority = MAX(Highest_runnable_priority, temp->priority);
                        break;
		    }
		}
	    }
	}
    })
    return rc;
}    


static void Initialize_Stack(char *stackptr, int stacksize)
{
/* This can be any unlikely pattern except 0x00010203 or the reverse. */
#define STACKMAGIC	0xBADBADBA
    register int i;

    lwpdebug(0, "Entered Initialize_Stack");
    if (lwp_stackUseEnabled)
	for (i=0; i<stacksize; i++)
	    stackptr[i] = i &0xff;
    else
	*(long *)stackptr = STACKMAGIC;
}

static int Stack_Used(stackptr, stacksize)
    register char *stackptr;
    int stacksize;
{
    register int    i;

    if (*(long *) stackptr == STACKMAGIC)
	return 0;
    else {
	for (i = 0; i < stacksize; i++)
	    if ((unsigned char) stackptr[i] != (i & 0xff))
		return (stacksize - i);
	return 0;
    }
}

/* Complain of a stack overflow to stderr without using stdio. */
static void Overflow_Complain()
{
    char *msg1 = "LWP: stack overflow in process ";
    char *msg2 = "!\n";
    write (2, msg1, strlen(msg1));
    write (2, lwp_cpptr->name, strlen(lwp_cpptr->name));
    write (2, msg2, strlen(msg2));
}


/*  The following documents the Assembler interfaces used by old LWP: 

savecontext(int (*ep)(), struct lwp_context *savearea, char *sp)


    Stub for Assembler routine that will
    save the current SP value in the passed
    context savearea and call the function
    whose entry point is in ep.  If the sp
    parameter is NULL, the current stack is
    used, otherwise sp becomes the new stack
    pointer.

returnto(struct lwp_context *savearea);

    Stub for Assembler routine that will
    restore context from a passed savearea
    and return to the restored C frame.
*/


