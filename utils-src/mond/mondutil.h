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


#ifndef _MONDUTIL_H_
#define _MONDUTIL_H_

extern void SetDate(void);
extern int DateChanged(void);
extern void InitRPC(int);
extern void InitSignals(void);
extern bbuf *Buff_Init(void);
extern void Log_Init(void);
extern void Log_Done(void);
extern void Data_Init(void);
extern void Data_Done(void);
extern void BrainSurgeon(void);
extern void PrintPinged(RPC2_Handle);
extern int CheckCVResult(RPC2_Handle,int,const char*,
			 const char*);
extern void LogEventArray(VmonSessionEventArray*);

#endif _MONDUTIL_H_
