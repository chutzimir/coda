#ifndef _BLURB_
#define _BLURB_
/*

     RVM: an Experimental Recoverable Virtual Memory Package
			Release 1.3

       Copyright (c) 1990-1994 Carnegie Mellon University
                      All Rights Reserved.

Permission  to use, copy, modify and distribute this software and
its documentation is hereby granted (including for commercial  or
for-profit use), provided that both the copyright notice and this
permission  notice  appear  in  all  copies  of   the   software,
derivative  works or modified versions, and any portions thereof,
and that both notices appear  in  supporting  documentation,  and
that  credit  is  given  to  Carnegie  Mellon  University  in all
publications reporting on direct or indirect use of this code  or
its derivatives.

RVM  IS  AN  EXPERIMENTAL  SOFTWARE  PACKAGE AND IS KNOWN TO HAVE
BUGS, SOME OF WHICH MAY  HAVE  SERIOUS  CONSEQUENCES.    CARNEGIE
MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION.
CARNEGIE MELLON DISCLAIMS ANY  LIABILITY  OF  ANY  KIND  FOR  ANY
DAMAGES  WHATSOEVER RESULTING DIRECTLY OR INDIRECTLY FROM THE USE
OF THIS SOFTWARE OR OF ANY DERIVATIVE WORK.

Carnegie Mellon encourages (but does not require) users  of  this
software to return any improvements or extensions that they make,
and to grant Carnegie Mellon the  rights  to  redistribute  these
changes  without  encumbrance.   Such improvements and extensions
should be returned to Software.Distribution@cs.cmu.edu.

*/

static char *rcsid = "$Header$";
#endif _BLURB_

#include <stdlib.h>
#include <stdio.h>
#include <rvm.h>
#include <rds.h>
#include <rds_private.h>
#include <rvm_segment.h>
    
              /************** NOTE: ***************/
/* we create our own transactions in the following routines, even
 * though there is a tid in the interface. This might result in unreferenced
 * objects if the thread which malloc'ed the object decides to abort. These
 * routines have no way of knowing if such an event happens. Also, if the user
 * free's then aborts, could have pointers into the free list objects.
 *
 * Interface has been changed so that if a user feels confident that his
 * transactions are serialized, (the above problem won't arise), he can pass
 * in a non-null tid ptr and it will be used. So if you aren't sure, make the
 * tidptr is zero!
 */


/* Global pointer to the start of the heap, but only accessed via heap macros
 * and routines. Should never be directly addressed by outside code.
 */
    
heap_header_t *RecoverableHeapStartAddress;

/*
 * Global pointer to last address in heap region. Make it a block ptr to
 * avoid casts in comparisons.
 */
free_block_t *RecoverableHeapHighAddress;
rvm_region_def_t *RegionDefs;
long             NRegionDefs;
rvm_bool_t       rds_testsw = rvm_false;   /* switch to allow special
                                              test modes */
/*
 * Global lock for the heap. See comment in rds_private.h.
 */

/* 
 * Cannot statically initialize locks in pthreads.  So, we also need a flag
 * for double initialization.
 */
#ifndef RVM_USEPT
RVM_MUTEX	   heap_lock = MUTEX_INITIALIZER;
#else
RVM_MUTEX          heap_lock;
static rvm_bool_t  inited = rvm_false;
#endif

/* Let the application know the starting address of the heap. This is
 * EFFECTIVELY idempotent since it will fail in rvm_load_segment without
 * modifying any structures if it has already been called.
 */
int
rds_load_heap(DevName, DevLength, static_addr, err)
     char 	  *DevName;
     rvm_offset_t DevLength;
     char 	  **static_addr; /* Start of region holding statics */
     int	  *err;
{
    rvm_return_t     rvmret;
    
    /* Map in the appropriate structures by calling Rvm_Load_Segment. */
    rvmret = rvm_load_segment(DevName, DevLength, NULL,
                              &NRegionDefs, &RegionDefs);
    if (rvmret != RVM_SUCCESS) {
	    printf("Error rvm_load_segment returns %d\n", rvmret);
	    (*err) = (int) rvmret;
	    return -1;
    }

    if (NRegionDefs != 2) {
	free(RegionDefs);
	(*err) = EBAD_SEGMENT_HDR;
	return -1;
    }
    
    (*static_addr) = (char *)RegionDefs[1].vmaddr;

    rds_start_heap(RegionDefs[0].vmaddr, err);

    return 0;
}

/*
 * Provide an interface which doesn't know about the segment layout.
 */

int
rds_start_heap(startAddr, err)
     char *startAddr;
     int *err;
{
    unsigned long    heap_hdr_len;

    /* If we are pthreaded, we need to init the heap lock.  Must only
       do this once! */
#ifdef RVM_USEPT
    if (inited == rvm_false) {
	inited = rvm_true;
	mutex_init(&heap_lock);
    }
#endif
    RecoverableHeapStartAddress = (heap_header_t *)startAddr;

    /* Match version stamps */
    if (strcmp(RDS_HEAP_VERSION, RDS_VERSION_STAMP) != 0) {
	*err = EHEAP_VERSION_SKEW;
        return -1;
    }

    heap_hdr_len = sizeof(heap_header_t) + RDS_NLISTS * sizeof(free_list_t);
    RecoverableHeapHighAddress = (free_block_t *)
	((char *)RecoverableHeapStartAddress +
	    ((RDS_HEAPLENGTH - heap_hdr_len)/ RDS_CHUNK_SIZE) * RDS_CHUNK_SIZE +
		heap_hdr_len);

    *err = SUCCESS;
    return -1;
}
