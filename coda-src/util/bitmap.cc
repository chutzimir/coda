/* BLURB gpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

#*/




/*
 * bitmap.c 
 * Created Feb 13, 1992	-- Puneet Kumar
 * Definition for the bitmap class 
 */
#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include "coda_string.h"
#include <unistd.h>
#include <stdlib.h>

#include <setjmp.h>
#ifdef __cplusplus
}
#endif

#include "util.h"
#include "rvmlib.h"
#include "bitmap.h"
extern char* hex(long, int =0);

void *bitmap::operator new(size_t size, int recable) {
    bitmap *x;

    if (recable) {
	x = (bitmap *)rvmlib_rec_malloc(sizeof(bitmap));
	CODA_ASSERT(x);
    }
    else {
	x = (bitmap *) malloc(sizeof(bitmap));
	CODA_ASSERT(x);
    }
    
    /* GROSS HACK BELOW; if this object was born via new, we
       are confident malloced is set.  But if object was
       allocated on stack, malloced is undefined.  We rely
       on the unlikeliness of the undefined value being exactly
       BITMAP_VIANEW for correctness.  Bletch!!! This would be much
       easier and cleaner if delete() took extra args, just like new()
       Obviously, I disagree with Stroustrup's polemic on this topic on
       pg 66 of the CARM (1st edition, 1991).
       
       Satya, 2/95
       
    */
    x->malloced = BITMAP_VIANEW;  
    return(x);
}


void bitmap::operator delete(void *deadobj, size_t size){
    /* Nothing to do; ~bitmap() has already done deallocation */
    }

bitmap::bitmap(int inputmapsize, int recable) {

    CODA_ASSERT(malloced != BITMAP_NOTVIANEW); /* ensure malloced is undefined if via stack! */
    if (malloced != BITMAP_VIANEW) malloced = BITMAP_NOTVIANEW; /* infer I must be on the stack */
    /* From this point on, malloced is definitely defined */

    recoverable = recable;
    if (recoverable)
	rvmlib_set_range(this, sizeof(bitmap));

    while (!(inputmapsize & 7)) 
	inputmapsize++;			/* must be a multiple of 8 */
    if (inputmapsize > 0) {
	mapsize = inputmapsize >> 3;
	if (recoverable) {
	    map = (char *)rvmlib_rec_malloc(mapsize);
	    CODA_ASSERT(map);
	    rvmlib_set_range(map, mapsize);
	}
	else
	    map = new char[mapsize];

	memset(map, 0, mapsize);
    }
    else {
	mapsize = 0;
	map = NULL;
    }
}

bitmap::~bitmap() {
    CODA_ASSERT(malloced == BITMAP_VIANEW); /* Temporary, until we discover if 
    				bitmaps are ever allocated on the stack
				Satya (6/5/95) */
				
    if (recoverable) {
	RVMLIB_REC_OBJECT(*this);
	if (map) 
	    rvmlib_rec_free(map);
    }
    else {
	if (map) 
	    delete[] map;
    }
    map = NULL;
    mapsize = 0;

    /* Finally, get rid of the object itself if it was allocated
       via new.  This should really have been in delete(), but can't
       test malloced there */
    if (malloced == BITMAP_VIANEW) {
	if (recoverable)
	    rvmlib_rec_free(this);
	else
	    free(this);
    }
}

void bitmap::Grow(int newsize) {
    if (newsize < (mapsize << 3)) return;
    while (!(newsize & 7)) 
	newsize++;		/* must be a multiple of 8 */

    int newmapsize = newsize >> 3;
    char *newmap;
    if (recoverable) {
	newmap = (char *)rvmlib_rec_malloc(newmapsize);
	CODA_ASSERT(newmap);
	rvmlib_set_range(newmap, newmapsize);
    }
    else {
	newmap = new char[newmapsize];
	CODA_ASSERT(newmap);
    }
    memset(newmap, 0, newmapsize);
    if (map) {
	CODA_ASSERT(mapsize > 0);
	memmove((void *)newmap, (const void *)map, mapsize);
	if (recoverable)
	    rvmlib_rec_free(map);
	else
	    delete[] map;
    }
    if (recoverable) 
	rvmlib_set_range(this, sizeof(bitmap));
    mapsize = newmapsize;
    map = newmap;
}

int bitmap::GetFreeIndex()
{
    int j;
    for (int offset = 0; offset < mapsize; offset++){
	if ((~map[offset]) & ALLOCMASK){
	    /* atleast one bit is zero */
	    /* set the bit  and return index */
	    unsigned char availbits = ~map[offset];
	    for (j = 0; j < 8; j++){
		if ((HIGHBIT >> j) & availbits){
		    /* jth bit is available */
		    if (recoverable)
			rvmlib_set_range(&map[offset], sizeof(char));
		    map[offset] |= (128 >> j);
		    break;
		}
	    }
	    CODA_ASSERT(j < 8);
	    return((offset << 3) + j);
	}
    }
    return(-1);		/* no free slot */
}

void bitmap::SetIndex(int index) {
    int offset = index >> 3;	/* the byte offset into bitmap */
    int bitoffset = index & 7;
    CODA_ASSERT(offset < mapsize);

    if (recoverable) rvmlib_set_range(&map[offset], sizeof(char));

    /* make sure bit is not set */
    if ((~map[offset]) & (1 << (7-bitoffset))) 
	map[offset] |= (1 << (7 - bitoffset));
}

void bitmap::FreeIndex(int index) {
    int offset = index >> 3;	/* the byte offset into bitmap */
    int bitoffset = index & 7;
    CODA_ASSERT(offset < mapsize);

    if (recoverable) rvmlib_set_range(&map[offset], sizeof(char));

    /* make sure bit is set */
    if (map[offset] & (1 << (7-bitoffset)))
	map[offset] &= ~(1 << (7 - bitoffset));
}

int bitmap::Value(int index) {
    if (index > (mapsize << 3)) return(-1);
    int offset = index >> 3;
    int bitoffset = index & 7;
    
    return(map[offset] & (1 << (7 - bitoffset)));
}

int bitmap::Count() {
    int count = 0;
    for (int i = 0; i < mapsize; i++) 
	for (int j = 0; j < 8; j++) 
	    if (map[i] & (1 << j)) 
		count++;
    return(count);
}

int bitmap::Size() {
    return (mapsize << 3);
}
void bitmap::purge() {
    if (recoverable) {
	RVMLIB_REC_OBJECT(*this);
	rvmlib_rec_free(map);
    }
    else {
	if (map)
	    delete[] map;
    }
    map = NULL;
    mapsize = 0;
}

void bitmap::operator=(bitmap& b) {
    if (mapsize != b.mapsize) {
	/* deallocate existing map entry */
	if (map) {
	    if (recoverable) 
		rvmlib_rec_free(map);
	    else
		delete[] map;
	}
	
	/* allocate new map */
	if (recoverable) {
	    RVMLIB_REC_OBJECT(*this);
	    map = (char *)rvmlib_rec_malloc(b.mapsize);
	    CODA_ASSERT(map);
	    rvmlib_set_range(map, mapsize);
	}
	else {
	    map = new char[mapsize];
	    CODA_ASSERT(map);
	}
    }
    else {
	/* use space of old map itself */
	if (recoverable) 
	    rvmlib_set_range(map, mapsize);
    }
    
    memmove((void *)map, (const void *)b.map, mapsize);
}
int bitmap::operator!=(bitmap& b) {
    if (mapsize != b.mapsize) 	
	return (1);
    for (int i = 0; i < mapsize; i++)
	if (map[i] != b.map[i]) return(1);
    return(0);
}

void bitmap::print() {
    print(stderr);
}
void bitmap::print(FILE *fp) {
    print(fileno(fp));
}

void bitmap::print(int fd) {
    char buf[512];
    sprintf(buf, "mapsize %d\n map:\n\0", mapsize);
    write(fd, buf, strlen(buf));
    for (int i = 0; i < mapsize; i++) {
	unsigned long l = (unsigned long) map[i];
	l = l & 0x000000ff;
	sprintf(buf, "0x%x \0",l);
	write(fd, buf, strlen(buf));
    }
    sprintf(buf, "\n");
    write(fd, buf, strlen(buf));
}

#ifdef notdef
ostream &operator<<(ostream& s, bitmap *b) {
    s << "mapsize = " << b->mapsize << '\n';
    s << "bitmap = " ;
    for (int i = 0; i < b->mapsize; i++) {
	long l = (long)(b->map[i]);
	s << hex(l, 2);
    }
    s << '\n';
    return(s);
}
#endif
