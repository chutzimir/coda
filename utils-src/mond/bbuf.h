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



// bbuf1.h
//
// class and method defns for the bbuf

#ifndef _BBUF_H_
#define _BBUF_H_

enum BbufStatus { EBBUFMT, EBBUFFL, BBUFOK};

// parameterized types would be *so* much easier (grumble)

typedef vmon_data *bbuf_item;

class bbuf {
  bbuf_item *buf;
  int        bnd;
  int        head,tail,count;
  int        low_fuel_mark; // point at which you stop spooling out.;
  bool       dbg;
  MUTEX      lock;          // simple mutex scheme - maybe change later;
  CONDITION  low_fuel;      // signal spool out threads;
  CONDITION  full_tank;     // signal spool in threads;
  void       bbuf_error(char*,int);
 public:
  bool       full(void);
  bool       empty(void);
  BbufStatus insert(bbuf_item);
  BbufStatus remove(bbuf_item*);
//  void       print_it(void);
  void       debug(bool);
  void       flush_the_tank(void);
  bbuf(int, int =0);
  ~bbuf(void);
  inline void examine(void) {fprintf(stderr,"head %d, tail %d, count %d\n",
				     head,tail,count); }
};

#endif _BBUF_H_
