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


#ifndef VERSION_H
#define VERSION_H

class connection_entry : public olink {
    RPC2_Handle myCid;
    long        myClientType;
public:
    connection_entry(RPC2_Handle, long);
    long client_type(void) {return myClientType;}
    RPC2_Handle cid(void) {return myCid;}
    void print();
    void print(FILE *);
    void print(int);
    void print(int,FILE*);
};

class connection_table {
    friend long MondEstablishConn(RPC2_Handle, unsigned long, 
				  long, long, SpareEntry[]);
    ohashtab *table;
public:
    connection_entry *GetConnection(RPC2_Handle);
    int ConnectionValid(RPC2_Handle, long);
    int RemoveConnection(RPC2_Handle);
    void LogConnections(int,FILE*);
    int PurgeConnections(void);
    connection_table(int =1024);
    ~connection_table(void);
};

char *HostNameOfConn(RPC2_Handle);
extern connection_table *ClientTable;

#endif VERSION_H
