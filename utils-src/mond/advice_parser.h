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


#ifndef _ADVICE_PARSER_H_
#define _ADVICE_PARSER_H_

typedef struct {
    int hostid;
    int uid;
    int venusmajorversion;
    int venusminorversion;
    int advicemonitorversion;
    int adsrv_version;
    int admon_version;
    int q_version;
    int disco_time;
    int cachemiss_time;
    int fid_volume;
    int fid_vnode;
    int fid_uniquifier;
    int practice_session;
    int expected_affect;
    int comment_id;
} DiscoMissQ;

typedef struct {
    int hostid;
    int uid;
    int venusmajorversion;
    int venusminorversion;
    int advicemonitorversion;
    int adsrv_version;
    int admon_version;
    int q_version;
    int volume_id;
    int cml_count;
    int disco_time;
    int reconn_time;
    int walk_time;
    int reboots;
    int hits;
    int misses;
    int unique_hits;
    int unique_misses;
    int not_referenced;
    int awareofdisco;
    int voluntary;
    int practicedisco;
    int codacon;
    int sluggish;
    int observed_miss;
    int known_other_comments;
    int suspected_other_comments;
    int nopreparation;
    int hoard_walk;
    int num_pseudo_disco;
    int num_practice_disco;
    int prep_comments;
    int overall_impression;
    int final_comments;
} ReconnQ;

extern int ParseDisconQfile(char *, DiscoMissQ *);
extern int ParseReconnQfile(char *, ReconnQ *);

#endif _ADVICE_PARSER_H_
