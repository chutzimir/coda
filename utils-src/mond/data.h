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



#ifndef _DATA_H_
#define _DATA_H_

//
// data.h
//
// This file contains defns for the assorted data that mond catches
//  -- mostly very simple.  Just the container class + subclasses.
// It also contains the defn of the buffer pool class.
  
//
// First, the superclass of all data classes that encapsulate
// types that mond will put in it's bounded buffer...
//
// It consists only of a type field.

/* buffer pools */
/* TODO: think about being able to remove buffers from the pool */


class callCountArray;
class multiCallArray;
class vmon_data;
class session_data;
class comm_data;
class clientCall_data;
class clientMCall_data;
class clientRVM_data;
class vcb_data;
class miniCache_data;
class overflow_data;
class srvrCall_data;
class resEvent_data;
class srvOverflow_data;
class bufpool;

class callCountArray {
    long size;
    CallCountEntry *array;
public:
    callCountArray(void);
    ~callCountArray(void);
    void set(long,CallCountEntry*);
    long getSize(void) {return size;}
    CallCountEntry *getArray(void) {return array;}
};

class multiCallArray {
    long size;
    MultiCallEntry *array;
public:
    multiCallArray();
    ~multiCallArray();
    void set(long,MultiCallEntry*);
    long getSize(void) {return size;}
    MultiCallEntry *getArray(void) {return array;}
};
    

class vmon_data {
    vmon_data *next;
public:
    virtual void Report(void) =0;
    virtual dataClass Type(void) =0;
    virtual char *TypeName(void) =0;
    virtual void Release(void) =0;
    inline void SetNext(vmon_data* theNext) { next = theNext;  }
    inline void NotOnList(void) { next = NULL; }
    inline vmon_data *Next() { return next; }
};


class session_data : private vmon_data {
    VmonVenusId           Venus;
    VmonSessionId         Session;
    VolumeId              Volume;
    UserId                User;
    VmonAVSG              AVSG;
    unsigned long         StartTime;
    unsigned long         EndTime;
    unsigned long         CETime;
    VmonSessionEventArray Events;
    SessionStatistics     Stats;
    CacheStatistics	  CacheStats;
public:
    dataClass Type(void) { return SESSION; }
    char *TypeName(void) { return "Session";}
    void Report(void) { (void) ReportSession(&Venus, Session, Volume, User, &AVSG,
					     StartTime, EndTime, CETime, &Events, 
					     &Stats, &CacheStats); }
    void Release(void);
    void init(VmonVenusId*, VmonSessionId, VolumeId, UserId, VmonAVSG*,
	      unsigned long, unsigned long, unsigned long, long, 
	      VmonSessionEvent[], SessionStatistics*, CacheStatistics*);
    VmonSessionEventArray *theEvents() {return &Events;}
};


class comm_data : private vmon_data {
    VmonVenusId Venus;
    unsigned long ServerIPAddress;
    long SerialNumber;
    unsigned long Time;
    VmonCommEventType EvType;
public:
    inline dataClass Type(void) { return COMM; }
    char *TypeName(void) { return "CommEvent";}
    void Release(void);
    void Report(void) { (void) ReportCommEvent(&Venus, ServerIPAddress, 
					       SerialNumber, Time, EvType); }
    void init(VmonVenusId*, unsigned long, long, unsigned long, VmonCommEventType);
};

class clientCall_data : private vmon_data {
    VmonVenusId Venus;
    long Time;
    class callCountArray SrvCount;
public:
    inline dataClass Type(void) {return CLNTCALL;}
    char *TypeName(void) { return "Client Call";}
    void Release(void);
    void Report(void) {(void) ReportClntCall(&Venus, Time, SrvCount); }
    void init(VmonVenusId*,long,long,CallCountEntry*);
};

class clientMCall_data : private vmon_data {
    VmonVenusId Venus;
    long Time;
    class multiCallArray MSrvCount;
public:
    inline dataClass Type(void) {return CLNTMCALL;}
    char *TypeName(void) { return "Client Call";}
    void Release(void);
    void Report(void) {(void) ReportClntMCall(&Venus, Time, MSrvCount); }
    void init(VmonVenusId*,long,long,MultiCallEntry*);
};

class clientRVM_data : private vmon_data {
    VmonVenusId Venus;
    long Time;
    RvmStatistics Stats;
public:
    inline dataClass Type(void) {return CLNTRVM;}
    char *TypeName(void) { return "Client Call";}
    void Release(void);
    void Report(void) {(void) ReportClntRVM(&Venus, Time, &Stats); }
    void init(VmonVenusId*,long,RvmStatistics*);
};

class vcb_data : private vmon_data {
    VmonVenusId Venus;
    long VenusInit;
    long Time;
    VolumeId Volume;
    VCBStatistics Stats;
public:
    inline dataClass Type(void) {return VCB;}
    char *TypeName(void) { return "VCB Stats";}
    void Release(void);
    void Report(void) {(void) ReportVCB(&Venus, VenusInit, Time, Volume, &Stats); }
    void init(VmonVenusId*,long,long,VolumeId,VCBStatistics*);
};

class advice_data : private vmon_data {
    VmonVenusId Venus;
    long Time;
    UserId User;
    AdviceStatistics Stats;
    unsigned long Call_Size;
    AdviceCalls *Call_Stats;
    unsigned long Result_Size;
    AdviceResults *Result_Stats;
public:
    advice_data();
    ~advice_data();
    inline dataClass Type(void) {return ADVICE;}
    char *TypeName(void) { return "Client Advice Stats";}
    void Release(void);
    void Report(void)
    {
	(void) ReportAdviceCall(&Venus, Time, User, &Stats, 
		Call_Size, Call_Stats, Result_Size, Result_Stats);
    }
    void init(VmonVenusId*, long, UserId, AdviceStatistics*, unsigned long, AdviceCalls[], unsigned long, AdviceResults[]);
};


class miniCache_data : private vmon_data {
    VmonVenusId Venus;
    long Time;
    unsigned long VN_Size;
    VmonMiniCacheStat *VN_Stats;
    unsigned long VFS_Size;
    VmonMiniCacheStat *VFS_Stats;
public:
    miniCache_data();
    ~miniCache_data();
    inline dataClass Type(void) {return MINICACHE;}
    char *TypeName(void) {return "Client MiniCache Stats";}
    void Release(void);
    void Report(void) 
    {
	  (void) ReportMiniCacheCall(&Venus, Time, VN_Size,
	  VN_Stats, VFS_Size, VFS_Stats);
    }
    void init(VmonVenusId*,long,unsigned long,VmonMiniCacheStat[],
	      unsigned long,VmonMiniCacheStat[]);
};

class overflow_data : private vmon_data {
    VmonVenusId Venus;
    unsigned long VMStartTime;
    unsigned long VMEndTime;
    long VMCount;
    unsigned long RVMStartTime;
    unsigned long RVMEndTime;
    long RVMCount;
public:
    inline dataClass Type(void) { return OVERFLOW; }
    char *TypeName(void) { return "Client Overflow";}
    void Release(void);
    void Report(void) { (void) ReportOverflow(&Venus, VMStartTime, VMEndTime, VMCount,
					      RVMStartTime, RVMEndTime, RVMCount); }
    void init(VmonVenusId*, unsigned long, unsigned long, long,
	      unsigned long, unsigned long, long);
};

enum countArrayType {CALLBACK, RESOLVE, SMON, VOLD, MULTICAST};

class srvrCall_data : private vmon_data {
    SmonViceId Vice;
    unsigned long Time;
    class callCountArray CBCount;
    class callCountArray ResCount;
    class callCountArray SmonCount;
    class callCountArray VolDCount;
    class multiCallArray MultiCount;
    SmonStatistics Stats;
public:
    dataClass Type(void) { return SRVCALL; }
    char *TypeName(void) { return "Server Call";}
    void Release(void);
    void Report(void) { (void) ReportSrvrCall(&Vice, Time, CBCount, ResCount, 
					      SmonCount, VolDCount, MultiCount, &Stats); }
    void init(SmonViceId*,unsigned long, long, CallCountEntry*, long,
	      CallCountEntry*, long, CallCountEntry*, long, CallCountEntry*,
	      long, MultiCallEntry*, SmonStatistics*);
};

class resEvent_data : private vmon_data {
    SmonViceId Vice;
    unsigned long Time;
    VolumeId Volid;
    long HighWaterMark;
    long AllocNumber;
    long DeallocNumber;
    long ResOp_size;
    ResOpEntry *ResOp;
public:
    dataClass Type(void) {return SRVRES;}
    char *TypeName(void) { return "Res Event";}
    void Release(void);
    void Report(void) { (void) ReportResEvent(&Vice, Time, Volid, HighWaterMark,
					 AllocNumber, DeallocNumber,
					 ResOp_size, ResOp); }
    void init(SmonViceId*, RPC2_Unsigned, VolumeId, RPC2_Integer,
	      RPC2_Integer, RPC2_Integer, RPC2_Integer, ResOpEntry[]);
    resEvent_data(void);
    //no destructor is needed
};

class srvOverflow_data : private vmon_data {
    SmonViceId Vice;
    unsigned long Time;
    unsigned long StartTime;
    unsigned long EndTime;
    long Count;
public:
    dataClass Type(void) {return SRVOVRFLW; }
    char *TypeName(void) { return "Server Overflow";}
    void Release(void);
    void Report(void) { (void) ReportSrvOverflow(&Vice,Time,StartTime,
						 EndTime, Count); }
    void init(SmonViceId*,RPC2_Unsigned,RPC2_Unsigned,RPC2_Unsigned,
	      RPC2_Integer);
};

class iotInfo_data : private vmon_data {
    VmonVenusId	Venus;
    IOT_INFO Info;
    RPC2_Integer AppNameLen;
    RPC2_String AppName;
public:
    iotInfo_data();
    ~iotInfo_data();
    inline dataClass Type(void) {return IOTINFO;}
    char *TypeName(void) { return "Client IOT Info";}
    void Release(void);
    void Report(void)
    {
	(void) ReportIotInfoCall(&Venus, &Info, AppNameLen, AppName);
    }
    void init(VmonVenusId *, IOT_INFO *, RPC2_Integer, RPC2_String);
};

class iotStat_data : private vmon_data {
    VmonVenusId	Venus;
    RPC2_Integer Time;
    IOT_STAT Stats;
public:
    iotStat_data();
    ~iotStat_data();
    inline dataClass Type(void) {return IOTSTAT;}
    char *TypeName(void) { return "Client IOT Stat";}
    void Release(void);
    void Report(void)
    {
	(void) ReportIotStatsCall(&Venus, Time, &Stats);
    }
    void init(VmonVenusId *, RPC2_Integer, IOT_STAT *);
};

class subtree_data : private vmon_data {
    VmonVenusId	Venus;
    RPC2_Integer Time;
    LocalSubtreeStats Stats;
public:
    subtree_data();
    ~subtree_data();
    inline dataClass Type(void) {return SUBTREE;}
    char *TypeName(void) { return "Client Localized Subtree Stats";}
    void Release(void);
    void Report(void)
    {
	(void) ReportSubtreeCall(&Venus, Time, &Stats);
    }
    void init(VmonVenusId*, RPC2_Integer, LocalSubtreeStats *);
};

class repair_data : private vmon_data {
    VmonVenusId	Venus;
    RPC2_Integer Time;
    RepairSessionStats Stats;
public:
    repair_data();
    ~repair_data();
    inline dataClass Type(void) {return REPAIR;}
    char *TypeName(void) { return "Client Local/Global Repair Session Stats";}
    void Release(void);
    void Report(void)
    {
	(void) ReportRepairCall(&Venus, Time, &Stats);
    }
    void init(VmonVenusId*, RPC2_Integer, RepairSessionStats *);
};

class rwsStat_data : private vmon_data {
    VmonVenusId	Venus;
    RPC2_Integer Time;
    ReadWriteSharingStats Stats;
public:
    rwsStat_data();
    ~rwsStat_data();
    inline dataClass Type(void) {return RWSSTAT;}
    char *TypeName(void) { return "Client RWS Stat";}
    void Release(void);
    void Report(void)
    {
	(void) ReportRwsStatsCall(&Venus, Time, &Stats);
    }
    void init(VmonVenusId *, RPC2_Integer, ReadWriteSharingStats *);
};

class bufpool {
    MUTEX       lock;
    vmon_data   *Pool;
    dataClass   type;
public:
    bufpool(dataClass);
    vmon_data *getSlot(void);
    void putSlot(vmon_data*);
};

struct Histogram {
    long size;
    HistoElem *buckets;

    Histogram();
    ~Histogram();
    void set(long _size, HistoElem _buckets[]);
};

class rvmResEvent_data : private vmon_data {
    SmonViceId Vice;
    unsigned long Time;
    unsigned long VolID;
    FileResStats FileRes;
    DirResStats DirRes;
    Histogram LogSizeHisto;
    Histogram LogMaxHisto;
    ResConflictStats Conflicts;
    Histogram SuccHierHist;
    Histogram FailHierHist;
    ResLogStats ResLog;
    Histogram VarLogHisto;
    Histogram LogSize;
public:
    dataClass Type(void) {return SRVRVMRES;}
    char *TypeName(void) { return "Rvm Resolution Stats";}
    void Release(void);
    void Report(void) 
    { (void) ReportRvmResEvent(Vice, Time, VolID, FileRes, DirRes,
			       LogSizeHisto, LogMaxHisto, Conflicts,
			       SuccHierHist, FailHierHist, ResLog,
			       VarLogHisto, LogSize); }
    void init(SmonViceId, unsigned long, unsigned long, FileResStats,
	      DirResStats, long, HistoElem[], long, HistoElem[], 
	      ResConflictStats, long, HistoElem[], long, HistoElem[], 
	      ResLogStats, long, HistoElem[], long, HistoElem[]);
    /* use the default ctor */
    //rvmResEvent_data(void);
    //no destructor is needed
};

// extern'ed bufpools

extern bufpool session_pool;
extern bufpool comm_pool;
extern bufpool clientCall_pool;
extern bufpool clientMCall_pool;
extern bufpool clientRVM_pool;
extern bufpool vcb_pool;
extern bufpool advice_pool;
extern bufpool miniCache_pool;
extern bufpool overflow_pool;
extern bufpool srvrCall_pool;
extern bufpool resEvent_pool;
extern bufpool rvmResEvent_pool;
extern bufpool srvOverflow_pool;
extern bufpool iotInfo_pool;
extern bufpool iotStat_pool;
extern bufpool subtree_pool;
extern bufpool repair_pool;
extern bufpool rwsStat_pool;

#endif _DATA_H_
