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
                           none currently

#*/






enum Opcodes   {
		HELP=0,
		QUIT, LENGTHTEST, REBIND,
		ONEPING, MANYPINGS, 
		FETCHFILE, STOREFILE,
		DUMPTRACE, STATS, REMOTESTATS,
		BEGINREMOTEPROFILING, ENDREMOTEPROFILING,
		SETVMFILESIZE, SETREMOTEVMFILESIZE
		};
		
char *Opnames[]= {
		"Help",
		"Quit", "LengthTest", "Rebind",
		"OnePing", "ManyPings",
		"FetchFile", "StoreFile",
		"Dumptrace", "Stats", "RemoteStats",
		"BeginRemoteProfiling", "EndRemoteProfiling",
		"SetVMFileBuffer", "SetRemoteVMFileBuffer"
		};
		
		
	    
