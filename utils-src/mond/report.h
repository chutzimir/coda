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


#ifndef _REPORT_H_
#define _REPORT_H_

/* I'm not sure why these are necessary all of a sudden, but... */
typedef class callCountArray;
typedef class multiCallArray;
typedef class Histogram;

extern long ReportSession(VmonVenusId*, VmonSessionId, VolumeId, UserId, 
			  VmonAVSG*, RPC2_Unsigned, RPC2_Unsigned, RPC2_Unsigned, 
			  VmonSessionEventArray*, SessionStatistics*, CacheStatistics*);

extern long ReportCommEvent(VmonVenusId *, RPC2_Unsigned, long, 
			    RPC2_Unsigned, VmonCommEventType);

extern long ReportClntCall(VmonVenusId*, long, class callCountArray*);

extern long ReportClntMCall(VmonVenusId*, long, class multiCallArray*);

extern long ReportClntRVM(VmonVenusId*, long, RvmStatistics*);

extern long ReportVCB(VmonVenusId*, long, long, VolumeId, VCBStatistics*);

extern long ReportAdviceCall(VmonVenusId*, long, UserId, AdviceStatistics*,
			     unsigned long, AdviceCalls*, unsigned long, AdviceResults*);

extern long ReportMiniCacheCall(VmonVenusId*, long, unsigned long, VmonMiniCacheStat*,
				unsigned long, VmonMiniCacheStat*);

extern long ReportOverflow(VmonVenusId *, RPC2_Unsigned, RPC2_Unsigned, 
			   RPC2_Integer, RPC2_Unsigned, RPC2_Unsigned, 
			   RPC2_Integer);

extern long ReportSrvrCall(SmonViceId*,unsigned long, class callCountArray*, 
			   class callCountArray*, class callCountArray*, 
			   class callCountArray*, class multiCallArray*, 
			   SmonStatistics*);

extern long ReportResEvent(SmonViceId*,RPC2_Unsigned, VolumeId, RPC2_Integer, 
			   RPC2_Integer, RPC2_Integer, RPC2_Integer, ResOpEntry[]);

extern long ReportRvmResEvent(SmonViceId, unsigned long, unsigned long, FileResStats,
			      DirResStats, class Histogram*, class Histogram*, 
			      ResConflictStats, class Histogram*, class Histogram*, 
			      ResLogStats, class Histogram*, class Histogram*);

extern long ReportSrvOverflow(SmonViceId *, unsigned long, unsigned long, 
			      unsigned long, long);

extern long ReportIotInfoCall(VmonVenusId *, IOT_INFO *, RPC2_Integer, RPC2_String);

extern long ReportIotStatsCall(VmonVenusId *, RPC2_Integer, IOT_STAT *);

extern long ReportSubtreeCall(VmonVenusId *, RPC2_Integer, LocalSubtreeStats *);

extern long ReportRepairCall(VmonVenusId *, RPC2_Integer, RepairSessionStats *);

extern long ReportRwsStatsCall(VmonVenusId *, RPC2_Integer, ReadWriteSharingStats *);

#endif _REPORT_H_
