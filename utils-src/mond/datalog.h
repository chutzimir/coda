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


#ifndef _DATALOG_H_
#define _DATALOG_H_

extern long ScanPastMagicNumber(long *);

extern int ReadSessionRecord(VmonVenusId*, VmonSessionId*, VolumeId*, UserId*, VmonAVSG*,
			     RPC2_Unsigned*, RPC2_Unsigned*, RPC2_Unsigned*,
			     VmonSessionEventArray*, SessionStatistics*, CacheStatistics*);
extern int ReadCommRecord(VmonVenusId *, RPC2_Unsigned *, RPC2_Integer *, RPC2_Unsigned *,
			  VmonCommEventType *);

extern int ReadClientCall(VmonVenusId *, long *, unsigned long *, 
			  CallCountEntry **);


extern int ReadClientMCall(VmonVenusId *, long *, 
                          unsigned long *, MultiCallEntry **);

extern int ReadClientRVM(VmonVenusId *, long *, RvmStatistics *);

extern int ReadVCB(VmonVenusId *, long *, long *, VolumeId *, VCBStatistics *);

extern int ReadAdviceCall(VmonVenusId *, long *, UserId *, AdviceStatistics *, 
			  unsigned long *, AdviceCalls **, 
		   	  unsigned long *, AdviceResults **);

extern int ReadMiniCacheCall(VmonVenusId*,
			     long*,
			     unsigned long*,
			     VmonMiniCacheStat**,
			     unsigned long*,
			     VmonMiniCacheStat**);

extern int ReadOverflow(VmonVenusId *, RPC2_Unsigned *, RPC2_Unsigned *, RPC2_Unsigned *,
		 RPC2_Unsigned *, RPC2_Unsigned *, RPC2_Unsigned *);
extern int ReadSrvCall(SmonViceId *, unsigned long *, unsigned long *,	CallCountEntry **,
		unsigned long *, CallCountEntry **, unsigned long *,
		CallCountEntry **, unsigned long *, CallCountEntry **, unsigned long *,
		MultiCallEntry **, SmonStatistics *);
extern int ReadResEvent(SmonViceId *, unsigned long *, unsigned long *,
		 long *, long *, long *, unsigned long *, ResOpEntry **);

extern int ReadRvmResEvent(SmonViceId*, unsigned long*, unsigned long*,
			   FileResStats*, DirResStats*, long*, HistoElem**,
			   long*, HistoElem**,
			   ResConflictStats*, long*, HistoElem**, long*,
			   HistoElem**, ResLogStats*, long*, HistoElem**,
			   long*, HistoElem**);

extern int ReadSrvOverflow(SmonViceId *, unsigned long *, unsigned long *,
		    unsigned long *,long *);

extern int ReadIotInfoCall(VmonVenusId *, IOT_INFO *, RPC2_Integer *, RPC2_String *);

extern int ReadIotStatsCall(VmonVenusId *, RPC2_Integer *, IOT_STAT *);

extern int ReadSubtreeCall(VmonVenusId *, RPC2_Integer *, LocalSubtreeStats *);

extern int ReadRepairCall(VmonVenusId *, RPC2_Integer *, RepairSessionStats *);

extern int ReadRwsStatsCall(VmonVenusId *, RPC2_Integer *, ReadWriteSharingStats *);

extern void RemoveCountArray(unsigned long, CallCountEntry*);
extern void RemoveMultiArray(unsigned long, MultiCallEntry*);

#endif _DATALOG_H_
