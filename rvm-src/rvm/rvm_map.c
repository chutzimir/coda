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

static char *rcsid = "/afs/cs/project/coda-rvb/cvs/src/coda-4.0.1/rvm-src/rvm/rvm_map.c,v 1.2 1997/01/07 18:44:47 rvb Exp";
#endif _BLURB_

/*
*
*                   RVM Mapping and Unmapping
*
*/

#ifdef __STDC__
#include <stdlib.h>
#else
#include <libc.h>
#endif
#include <sys/file.h>
#ifdef	__MACH__
#include <sysent.h>
#endif
#if defined(hpux) || defined(__hpux)
#include <hp_bsd.h>
#endif /* hpux */
#include <unistd.h>
#include "rvm_private.h"

/* global variables */

extern log_t        *default_log;       /* default log descriptor ptr */
extern int          errno;              /* kernel error number */
extern rvm_bool_t   rvm_no_update;      /* no segment or log update if true */
extern char         *rvm_errmsg;        /* internal error message buffer */

/* root of segment list and region tree */
list_entry_t        seg_root;           /* global segment list */
rw_lock_t           seg_root_lock;      /* lock for segment list header & links */
                                           
rw_lock_t           region_tree_lock;   /* lock for region tree */
tree_root_t         region_tree;        /* root of mapped region tree */

list_entry_t        page_list;          /* list of usable pages */
RVM_MUTEX           page_list_lock;     /* lock for usable page list */
rvm_length_t        page_size;          /* system page size */
rvm_length_t        page_mask;          /* mask for rounding down to page size */

/* locals */
static long         seg_code = 1;       /* segment short names */
static RVM_MUTEX    seg_code_lock;      /* lock for short names generator */
list_entry_t        page_list;          /* list of usable pages */
RVM_MUTEX           page_list_lock;     /* lock for usable page list */
/* basic page, segment lists and region tree initialization */
void init_map_roots()
    {
    init_list_header(&seg_root,seg_id);
    init_rw_lock(&seg_root_lock);
    init_rw_lock(&region_tree_lock);
    init_tree_root(&region_tree);
    mutex_init(&seg_code_lock);

    /* get page size */
    page_size = (rvm_length_t)getpagesize();
    page_mask = ~(page_size - 1);
    mutex_init(&page_list_lock);
    init_list_header(&page_list,free_page_id);
    }

/* check validity of rvm_region record & ptr */
rvm_return_t bad_region(rvm_region)
    rvm_region_t    *rvm_region;
    {
    if (rvm_region == NULL)
        return RVM_EREGION;
    if (rvm_region->struct_id != rvm_region_id)
        return RVM_EREGION;

    if (rvm_region->data_dev != NULL)
        if (strlen(rvm_region->data_dev) > (MAXPATHLEN-1))
            return RVM_ENAME_TOO_LONG;

    return RVM_SUCCESS;
    }

#ifdef MACH                             /* begin Mach-specific declarations */
#define PAGE_ALLOC_DEFINED
#include <mach.h>

/* Mach page allocator/deallocator */
char *page_alloc(len)
    rvm_length_t    len;                /* bytes to allocate */
    {
    char            *vmaddr;            /* allocated address return */
    kern_return_t   rtn;                /* kernel return value */

    rtn = vm_allocate(task_self_,(vm_address_t *)&vmaddr,
                      (vm_size_t)len,TRUE);
    switch (rtn)
        {
      case KERN_SUCCESS:    return vmaddr;
      case KERN_NO_SPACE:   return NULL;
      default:              ASSERT(rvm_false); /* error */
        }
    /* We must return before here: silence compiler warnings */
    ASSERT(rvm_false);
    return NULL;
    }

void page_free(vmaddr,length)
    char            *vmaddr;            /* base address of region */
    rvm_length_t    length;             /* length of region */
    {
    kern_return_t   rtn;                /* kernel return value */


    rtn = vm_deallocate(task_self_,(vm_address_t)vmaddr,
                      (vm_size_t)length);
    switch (rtn)
        {
      case KERN_SUCCESS:    return;
      default:              ASSERT(rvm_false); /* error */
        }
    }
/* Mach virtual memory addressability checker
   returns true if region addressable
*/
static rvm_bool_t mem_chk(vmaddr,length)
    char            *vmaddr;            /* base address of region */
    rvm_length_t    length;             /* length of region */
    {
    long            i;
    char            *chk_addr;          /* allocation addres */
    kern_return_t   rtn;                /* kernel return value */

    /* try to allocate each page of region
       -- error if can allocate */
    chk_addr = vmaddr;
    length = ROUND_TO_PAGE_SIZE(length);
    for (i=0; i<(length/page_size);i++)
        {
        rtn = vm_allocate(task_self_,(vm_address_t *)&chk_addr,
                          (vm_size_t)page_size,FALSE);
        if (rtn == KERN_SUCCESS)
            {
            /* oops... deallocate the page */
            rtn = vm_deallocate(task_self_,(vm_address_t)chk_addr,
                                page_size);
            ASSERT(rtn == KERN_SUCCESS);
            return rvm_false;               /* not addressable */
            }
        ASSERT(rtn != KERN_INVALID_ADDRESS);
        chk_addr = RVM_ADD_LENGTH_TO_ADDR(chk_addr,page_size);
        }

    return rvm_true;                    /* all's well */
    }

#endif                                 /* end of Mach-specific declarations */

#if __NetBSD__ || LINUX               /* begin NetBSD-specific declarations */
#define PAGE_ALLOC_DEFINED 
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

/*
 * Page table management code
 *
 * This code is used by the page allocation code in RVM to track what
 * regions of memory have been allocated for use in the persistent heap.
 *
 * In the original Mach specific code, this was gotten for "free" via
 * a hack which called vm_allocate to reallocate the block in question.
 * if the reallocation failed, the block had been allocated. if it
 * succeeded, the block had not been allocated (and, since we had just
 * allocated it, we quickly reallocated it and wiped the egg off of our
 * faces).
 *
 * The original NetBSD port of this attempted to take advantage of the
 * fact that if mmap() is called with the MAP_FIXED flag, it would
 * attempt to allocate exactly the region of memory in question. Supposedly,
 * if the region was already allocated, this mmap() call would fail.
 *
 * This solution turns out to be NOT CORRECT. Not only does NetBSD not
 * perform in this fashion (it will deallocated whatever was there beforehand,
 * silently), but there is another complication. If the application has
 * allocated memory in that space, it could cause an erroneous result from
 * the mem_chk() function. Since mmap() (if it behaved as originally beleived)
 * would not be able to allocate the space, it would assume it is a mapped
 * region. But, since it ISN'T a mapped region, just an allocated region,
 * the result is incorrect.
 *
 * One more factor which complicates adding what would otherwise be a
 * fairly straightforward list of allocated regions is that there are
 * two places in RVM where memory is allocated. One is in the RVM
 * library (page_alloc() and page_free(), both in rvm_map.c), and the
 * other is in the SEG segment loader library (allocate_vm() and
 * deallocate_vm(), both in rvm_segutil.c).
 *
 * --tilt, Nov 19 1996
 */

/* This is a doubly-linked list of allocated regions of memory. The regions
   are stored in increasing order, so that once you have passed the area
   where a questionable region has been stored, you can stop looking. */
rvm_page_entry_t *rvm_allocations      = NULL; /* allocated pages */
rvm_page_entry_t *rvm_allocations_tail = NULL; /* tail of list */

/*
 * rvm_register_page -- registers a page as being allocated.
 *                      returns rvm_true if the page is successfully registered
 *                      returns rvm_false is the page is already allocated
 *
 * TODO: should add optimization which coalesces page records.
 *       should round end up to be at end of page boundary.
 */
rvm_bool_t rvm_register_page(char *vmaddr, rvm_length_t length)
{
    rvm_page_entry_t *bookmark, *entry;
    char *end = vmaddr + length - 1;

    if(rvm_allocations == NULL) {
	/* There are no other allocated pages, so this is the trivial case */
	entry = (rvm_page_entry_t *) malloc(sizeof(rvm_page_entry_t));
	ASSERT(entry != NULL);
	entry->start    = vmaddr;
	entry->end      = end;
	entry->prev     = NULL;	 /* indicates beginning of list */
	entry->next     = NULL;	 /* indicates end of list */
	rvm_allocations      = entry; /* set this to be head of list */
	rvm_allocations_tail = entry; /* also set it to be the tail */
	return(rvm_true);
    }

    /* XXX check if tail is before this region for "quick" verification */

    /* search through the rvm_allocations list to find either
       a) where this should go, or b) a region which has already been
       registered which contains this region. */

    bookmark = rvm_allocations;
    while(bookmark != NULL) {
	/* check for various bad conditions: */

	/* case one: the start of the new region falls within
	   a previously allocated region */
	if( (bookmark->start <= vmaddr) && (vmaddr <= bookmark->end) ) {
	    printf("Case one.\n");
	    return(rvm_false);
	}

	/* case two: the end of the new region falls within
	   a previously allocated region */
	if ( (bookmark->start <= end)   && (end    <= bookmark->end) ) {
	    printf("Case two.\n");
	    return(rvm_false);
	}

	/* case three: the new region spans a currently allocated region
	   (n.b.: the case where the new region is contained within a
	          currently allocated region is handled by case one) */
	if ( (vmaddr <= bookmark->start) && (bookmark->end <= end) ) {
	    printf("Case three.\n");
	    return(rvm_false);
	}

	/* check to see if we're at the right place to insert this page.
	   we can do this by seeing if the end of the new region is
	   before the beginning of this one. if so, insert the new
	   region before the one we're currently looking at. */
	if(end < bookmark->start) {
	    entry = (rvm_page_entry_t *) malloc(sizeof(rvm_page_entry_t));
	    ASSERT(entry != NULL);
	    entry->start    = vmaddr;
	    entry->end      = end;
	    entry->prev     = bookmark->prev; /* insert the new entry */
	    entry->next     = bookmark;	      /* between bookmark and */
	    if (bookmark->prev != NULL)
		bookmark->prev->next = entry;
	    else
		/* bookmark must be the head of the list */
		rvm_allocations = entry;
	    bookmark->prev  = entry;          /* the entry before bookmark */
	    return(rvm_true);
	}

	/* if we're at the end, and we haven't tripped yet, we should
	   put the entry at the end */
	if(bookmark->next == NULL) {
	    entry = (rvm_page_entry_t *) malloc(sizeof(rvm_page_entry_t));
	    ASSERT(entry != NULL);
	    entry->start    = vmaddr;
	    entry->end      = end;
	    entry->prev     = bookmark;       /* insert the new entry */
	    entry->next     = NULL;	      /* after bookmark */
	    bookmark->next  = entry;
	    rvm_allocations_tail = entry;     /* set the new tail */
	    return(rvm_true);
	} else {
	    bookmark = bookmark->next;
	}
    } /* end while */

    /* we shouldn't be able to get here. */
    ASSERT(rvm_false);
    return(rvm_false);
}

/*
 * rvm_unregister_page -- removes a previously registered page from the
 *                        list of registered pages. returns true if the page is
 *                        successfully unregistered; returns false if the
 *                        page was not previously allocated.
 */
rvm_bool_t rvm_unregister_page(char *vmaddr, rvm_length_t length)
{
    rvm_page_entry_t *entry, *previous_entry, *next_entry;

    entry = find_page_entry(vmaddr);
    if(entry == NULL)
	return(rvm_false);

    if ( (entry->start != vmaddr) ||
         (entry->end   != (vmaddr + length - 1)) ) {
	/* this isn't an exact match.
	   as long we don't do coalescing of region entries,
	   this means we should return false */
	return(rvm_false);
    }

    /* if entry != NULL, we've found the page we're unregistering.
       remove it from the list. */
    previous_entry = entry->prev;
    next_entry     = entry->next;

    /* set the entries before and after this one skip over this one */
    if(previous_entry == NULL) {
	/* this is at the beginning of the list of allocated pages */
	rvm_allocations = next_entry;
    } else {
	previous_entry->next = next_entry;
    }

    if(next_entry != NULL)
	next_entry->prev = previous_entry;

    /* free this entry */
    free(entry);

    return(rvm_true);
}

/*
 * find_page_entry -- this returns the first entry which contains
 *                    the beginning of the requested region.
 *                    these somewhat peculiar semantics allow
 *                    us to support both rvm_unregister_page and
 *                    chk_mem, which need slightly different things.
 */
rvm_page_entry_t *find_page_entry(char *vmaddr)
{
    rvm_page_entry_t *bookmark;

    bookmark = rvm_allocations;

    while(bookmark != NULL) {
	if( (bookmark->start <= vmaddr) && (vmaddr <= bookmark->end) ) {
	    return(bookmark);
	}

	bookmark = bookmark->next;
    }

    return(NULL);
}

/*
 * The following code has matching functions in the Mach and default
 * compilations.
 */

/* NetBSD page allocator */
char *page_alloc(len)
    rvm_length_t    len;
    {
    char           *vmaddr;
    
    vmaddr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE,
		  -1, 0);
    if (vmaddr == (char *)-1) 
        {
	if (errno == ENOMEM) 
            {
	    vmaddr = NULL;
	    } 
	else 
	    {
	    ASSERT(rvm_false);  /* Unknown error condition */
   	    }
        }

    /* modified by tilt, Nov 19 1996.
       When we allocate a page (or range of pages) we register
       it in an internal table we're keeping around to keep
       track of pages. (The previous solution was to try to
       re-allocate the page, and see if it fails, which is
       not only wrong [since we don't if it's allocated, or
       actually allocated in the RVM heap!!], but doesn't
       work with mmap()). */
    if (rvm_register_page(vmaddr, len) == rvm_false)
        {
	ASSERT(rvm_false);	/* Registering shouldn't have failed */
	}

    return vmaddr;
    }

/* NetBSD page deallocator */
void page_free(vmaddr, length)
    char            *vmaddr;
    rvm_length_t     length;
    {
	if (munmap(vmaddr, length)) {
	    ASSERT(0); /* should never fail */
	}

	if (rvm_unregister_page(vmaddr, length) == rvm_false) {
	    ASSERT(0); /* should never fail */
	}
    }

/*
 * mem_chk -- verifies that the memory region in question
 *            is actually addressable as part of RVM.
 *            this means either that it is on the list,
 *            or it is wholly contained by one or more list entries.
 */
rvm_bool_t mem_chk(char *vmaddr, rvm_length_t length)
{
    rvm_page_entry_t *entry;
    char *start = vmaddr;
    char *end   = vmaddr + length - 1;

    while(rvm_true) {
	entry = find_page_entry(start);
	if(entry == NULL)
	    return(rvm_false);

	if(end <= entry->end)
	    return(rvm_true);

	start = entry->end + 1;	/* XXX possible problems with
				       pages that aren't fully
				       allocated. burn that
				       bridge when we get to it. */
    }

    ASSERT(rvm_false);
    return(rvm_false);		/* shouldn't be able to get here */
}

#endif /* __NetBSD__ */

#ifndef PAGE_ALLOC_DEFINED              /* begin generic Unix declarations */
/* Unix page_list allocation */
static rvm_length_t get_page(length)
    rvm_length_t    length;
    {
    free_page_t     *page;              /* ptr to free page */
    free_page_t     *bestfit;           /* ptr to best fitting block */

    CRITICAL(page_list_lock,            /* begin page_list_lock crit sec */
        {
        page = (free_page_t *)page_list.nextentry;
        bestfit = NULL;
        while (page != (free_page_t *)&page_list)
            {
            if (page->len >= length)
                {
                if (bestfit == NULL)
                    bestfit = page;
                else
                    if (page->len <= bestfit->len)
                        bestfit = page;
                if (page->len == length) break; /* exact fit */
                }
            page = (free_page_t *)page->links.nextentry;
            }

        if (bestfit != NULL)
            {
            if (bestfit->len == length) /* return whole block */
                (void)move_list_entry(&page_list,NULL,
                                      (list_entry_t *)bestfit);
            else
                {                       /* trim tail off and return */
                bestfit->len = bestfit->len - length;
                bestfit = (free_page_t *)((rvm_length_t)bestfit
                                           + bestfit->len);
                }
            }
        });                             /* end page_list_lock crit sec */

    return (rvm_length_t)bestfit;
    }
void page_free(base,length)
    char            *base;
    rvm_length_t    length;
    {
    free_page_t     *cell;

    /* construct list entry from page */
    cell = (free_page_t *)base;
    cell->links.struct_id = free_page_id;
    cell->links.is_hdr = rvm_false;
    cell->len = length;

    /* put on free list */
    CRITICAL(page_list_lock,            /* begin page_list_lock crit sec */
        {
        (void)move_list_entry(NULL,&page_list,
                              (list_entry_t *)cell);
        });                             /* end page_list_lock crit sec */
    }
/* Unix page-aligned space allocator */
char *page_alloc(len)
    rvm_length_t    len;                /* bytes to allocate */
    {
    rvm_length_t    base,oldbase;       /* base of memory allocated */
    rvm_length_t    pad,limit;          /* padding for page alignment */

    /* see if there is a region already allocated */
    if ((base = get_page(len)) != NULL)
        goto test_exit;

    /* round up present break pt to page size & allocate */
    oldbase = (rvm_length_t)sbrk(0);
    pad = page_size - (oldbase & ~page_mask);
    base = (rvm_length_t)sbrk((int)(len+pad));
    if (base == -1) return NULL;        /* out of space */
    if (base == oldbase)                /* got what we expected? */
        {
        base = base+pad;                /* yes, return 1st page addr */
        goto test_exit;
        }
    
    /* test if got enough anyway by extracting 1st page addr
       and testing if in allocated region */
    oldbase = base;
    limit = base+pad+len;
    if ((base=(limit & page_mask) - len) >= oldbase)
        goto test_exit;
    
    /* try to get remainder of last page */
    pad = page_size - (limit & ~page_mask);
    base = (rvm_length_t)sbrk((int)(pad));
    if (base == -1) return NULL;        /* out of space */
    if (base == limit)                  /* got remainder? */
        {
        base = (base+pad)-len;          /* yes, return 1st page addr */
        goto test_exit;
        }
    
    /* save previous allocation and reallocate with certianty */
    page_free((char *)((oldbase&page_mask)+page_size),len-page_size);
    base = (rvm_length_t)sbrk((int)(len+page_size-1));
    if (base == -1) return NULL;        /* out of space */
    base = (base+page_size-1) & page_mask;

test_exit:
    ASSERT((base&page_mask) == base);
    return (char *)base;
    }
/* Unix virtual memory addressability checker
   returns true if region addressable
*/
static rvm_bool_t mem_chk(vmaddr,length)
    char            *vmaddr;            /* base address of region */
    rvm_length_t    length;             /* length of region */
    {
    if (RVM_ADD_LENGTH_TO_ADDR(vmaddr,length) > (char *)sbrk(0))
        return rvm_false;               /* not addressable */

    return rvm_true;                    /* all's well */
    }

#endif                                  /* end of generic Unix declarations */
/* segment short name generator */
static long make_seg_code()
    {
    long            retval;

    CRITICAL(seg_code_lock,            /* begin seg_code_lock crit sec */
        { 
                                        /* probably indivisible on CISC */
        retval = seg_code++;            /* machines, but we can't RISC it, */
                                        /* so we lock it... */

        });                             /* end seg_code_lock crit sec */

    return retval;
    }
/* open segment device and set device characteristics */
long open_seg_dev(seg,dev_length)
    seg_t           *seg;               /* segment descriptor */
    rvm_offset_t    *dev_length;        /* optional device length */
    {
    rvm_length_t    flags = O_RDWR;     /* device open flags */
    long            retval;

    if (rvm_no_update) flags = O_RDONLY;
    if ((retval=open_dev(&seg->dev,flags,0)) < 0)
        return retval;
    if ((retval=set_dev_char(&seg->dev,dev_length)) < 0)
        close_dev(&seg->dev);

    return retval;
    }

long close_seg_dev(seg)
    seg_t           *seg;               /* segment descriptor */
    {

    return close_dev(&seg->dev);

    }
/* close segment devices at termination time */
rvm_return_t close_all_segs()
    {
    seg_t           *seg;               /* segment desriptor */
    rvm_return_t    retval=RVM_SUCCESS; /* return value */

    RW_CRITICAL(seg_root_lock,w,        /* begin seg_root_lock crit section */
        {
        FOR_ENTRIES_OF(seg_root,seg_t,seg)
            {
            CRITICAL(seg->dev_lock,     /* begin seg->dev_lock crit section */
                {
                if (close_seg_dev(seg) < 0)
                    retval = RVM_EIO;
                });                     /* end seg->dev_lock crit section */
            if (retval != RVM_SUCCESS)
                break;
            }
        });                             /* end seg_root_lock crit section */

    return retval;
    }
/* segment lookup via device name */
seg_t *seg_lookup(dev_name,retval)
    char            *dev_name;          /* segment device name */
    rvm_return_t    *retval;
    {
    char            full_name[MAXPATHLEN+1];
    seg_t           *seg = NULL;

    /* get full path name for segment device */
    (void)make_full_name(dev_name,full_name,retval);
    if (*retval != RVM_SUCCESS)
        return NULL;

    /* search segment list for full_name */
    RW_CRITICAL(seg_root_lock,r,        /* begin seg_root_lock crit section */
        {
        FOR_ENTRIES_OF(seg_root,seg_t,seg)
            if (!strcmp(seg->dev.name,full_name))
                break;                  /* found */
        });                             /* end seg_root_lock crit section */

    if (!seg->links.is_hdr)
        return seg;                     /* return found seg descriptor */
    else
        return NULL;
    }
/* enter segment short name definition in log */
rvm_return_t define_seg(log,seg)
    log_t           *log;               /* log descriptor */
    seg_t           *seg;               /* segment descriptor */
    {
    log_seg_t       *log_seg;           /* special log segment entry */
    log_special_t   *special;           /* allocation for log_seg */
    long            name_len;           /* byte length of segment name */
    rvm_return_t    retval;             /* return code */

    /* make segment definition record */
    name_len = strlen(seg->dev.name);
    special=make_log_special(log_seg_id,name_len+1);
    if (special == NULL)
        return RVM_ENO_MEMORY;          /* can't get descriptor */

    /* complete record and enter in log */
    log_seg = &special->special.log_seg;
    log_seg->seg_code = seg->seg_code;
    log_seg->num_bytes = seg->dev.num_bytes;
    log_seg->name_len = name_len;
    (void)strcpy(log_seg->name,seg->dev.name);
    if ((retval=queue_special(log,special)) != RVM_SUCCESS)
        free_log_special(log_seg);

    return retval;
    }
/* write new segment dictionary entries for all segments */
rvm_return_t define_all_segs(log)
    log_t           *log;
    {
    seg_t           *seg;               /* segment descriptor */
    rvm_return_t    retval = RVM_SUCCESS; /* return value */
    
    RW_CRITICAL(seg_root_lock,r,        /* begin seg_root_lock crit sec */
        {
        FOR_ENTRIES_OF(seg_root,seg_t,seg)
            {
            if ((retval=define_seg(log,seg)) != RVM_SUCCESS)
                break;
            }
        });                             /* end seg_root_lock crit sec */

    return retval;
    }
/* segment builder */
static seg_t *build_seg(rvm_region,log,retval)
    rvm_region_t    *rvm_region;        /* segment's region descriptor */
    log_t           *log;               /* log descriptor */
    rvm_return_t    *retval;            /* ptr to return code */
    {
    seg_t           *seg;               /* new segment descriptor */

    /* build segment descriptor */
    seg = make_seg(rvm_region->data_dev,retval);
    if (*retval != RVM_SUCCESS)
        goto err_exit;

    /* open device and set characteristics */
    seg->log = log;
    log->ref_cnt += 1;
    if (open_seg_dev(seg,&rvm_region->dev_length) < 0)
        {
        *retval = RVM_EIO;
        goto err_exit;
        }

    /* raw devices require length */
    if ((seg->dev.raw_io) &&
        (RVM_OFFSET_EQL_ZERO(seg->dev.num_bytes)))
        {
        *retval = RVM_ENOT_MAPPED;
        goto err_exit;
        }

    /* define short name for log & queue log entry */
    seg->seg_code = make_seg_code();
    if ((*retval=define_seg(log,seg)) != RVM_SUCCESS)
        goto err_exit;

    /* put segment on segment list */
    RW_CRITICAL(seg_root_lock,w,        /* begin seg_root_lock crit sec */
        {
        (void)move_list_entry(NULL,&seg_root,seg);
        });                             /* end seg_root_lock crit sec */
    return seg;

err_exit:
    log->ref_cnt -= 1;                  /* log seg dict entry not */
    if (seg != NULL) free_seg(seg);     /* deallocated since the seg_code is */
    return NULL;                        /* unique -- to the log, it's just like */
    }                                   /* a segment used read-only  */
/* device region conflict comparator */
long dev_partial_include(base1,end1,base2,end2)
    rvm_offset_t    *base1,*end1;
    rvm_offset_t    *base2,*end2;
    {
    if (RVM_OFFSET_GEQ(*base1,*end2))
        return 1;                       /* region1 above region2 */
    if (RVM_OFFSET_LEQ(*end1,*base2))
        return -1;                      /* region1 below region2 */

    return 0;                           /* regions at least partially overlap */
    }

/* device region within other region comparator */
long dev_total_include(base1,end1,base2,end2)
    rvm_offset_t    *base1,*end1;
    rvm_offset_t    *base2,*end2;
    {
    if ((RVM_OFFSET_GEQ(*base1,*base2) && RVM_OFFSET_LEQ(*base1,*end2))
        &&
        (RVM_OFFSET_GEQ(*end1,*base2) && RVM_OFFSET_LEQ(*end1,*end2))
        ) return 0;                     /* region1 included in region2 */
    if (RVM_OFFSET_LSS(*base1,*base2))
        return -1;                      /* region1 below region2, may overlap */                                          

    return 1;                           /* region1 above region2, may overlap */
    }
/* vm range conflict comparator */
long mem_partial_include(tnode1,tnode2)
    tree_node_t     *tnode1;            /* range1 */
    tree_node_t     *tnode2;            /* range2 */
    {
    rvm_length_t    addr1;              /* start of range 1 */
    rvm_length_t    addr2;              /* start of range 2 */
    rvm_length_t    end1;               /* end of range1 */
    rvm_length_t    end2;               /* end of range2 */

    /* rebind types and compute end points */
    addr1 = (rvm_length_t)(((mem_region_t *)tnode1)->vmaddr);
    addr2 = (rvm_length_t)(((mem_region_t *)tnode2)->vmaddr);
    end1 = addr1 + ((mem_region_t *)tnode1)->length;
    end2 = addr2 + ((mem_region_t *)tnode2)->length;

    if (addr1 >= end2) return 1;        /* range1 above range2 */
    if (end1 <= addr2) return -1;       /* range1 below range2 */
    return 0;                           /* ranges at least partially overlap */
    }

/* vm range within other range comparator */
long mem_total_include(tnode1,tnode2)
    tree_node_t     *tnode1;            /* range1 */
    tree_node_t     *tnode2;            /* range2 */
    {
    rvm_length_t    addr1;              /* start of range 1 */
    rvm_length_t    addr2;              /* start of range 2 */
    rvm_length_t    end1;               /* end of range1 */
    rvm_length_t    end2;               /* end of range2 */

    /* rebind types and compute end points */
    addr1 = (rvm_length_t)(((mem_region_t *)tnode1)->vmaddr);
    addr2 = (rvm_length_t)(((mem_region_t *)tnode2)->vmaddr);
    end1 = addr1 + ((mem_region_t *)tnode1)->length;
    end2 = addr2 + ((mem_region_t *)tnode2)->length;

    if (((addr1 >= addr2) && (addr1 < end2)) &&
        ((end1 > addr2) && (end1 <= end2))
        ) return 0;                     /* range1 included in range2 */
    if (end1 < addr2) return -1;        /* range1 below range2, may overlap */
    return 1;                           /* range1 above range2, may overlap */
    }
/* find and lock a region record iff vm range
   entirely within a single mapped region
   -- region tree is left lock if mode = w
   -- used by transaction functions and unmap
*/
region_t *find_whole_range(dest,length,mode)
    char            *dest;
    rvm_length_t    length;
    rw_lock_mode_t  mode;               /* lock mode for region descriptor */
    {
    mem_region_t    range;              /* dummy node for lookup */
    mem_region_t    *node;              /* ptr to node found */
    region_t        *region = NULL;     /* ptr to region for found node */

    range.vmaddr = dest;
    range.length = length;
    range.links.node.struct_id = mem_region_id;

    RW_CRITICAL(region_tree_lock,mode,  /* begin region_tree_lock crit sect */
        {
        node = (mem_region_t *)tree_lookup(&region_tree,
                                          (tree_node_t *)&range,
                                          mem_total_include);
        if (node != NULL)
            {
            region = node->region;
            if (region != NULL)
                {                       /* begin region_lock crit sect */
                rw_lock(&region->region_lock,mode); /* (ended by caller) */
                if (mode == w)          /* retain region_tree_lock */
                    return region;      /* caller will unlock */
                }
            }
        });                             /* end region_tree_lock crit sect */
    
    return region;
    }
/* find and lock a region record if vm range is at least partially
   within a single mapped region; return code for inclusion
*/
region_t *find_partial_range(dest,length,code)
    char            *dest;
    rvm_length_t    length;
    long            *code;
    {
    mem_region_t    range;              /* dummy node for lookup */
    mem_region_t    *node;              /* ptr to node found */
    region_t        *region = NULL;     /* ptr to region for found node */

    range.vmaddr = dest;
    range.length = length;
    range.links.node.struct_id = mem_region_id;

    RW_CRITICAL(region_tree_lock,r,     /* begin region_tree_lock crit sect */
        {
        node = (mem_region_t *)tree_lookup(&region_tree,
                                          (tree_node_t *)&range,
                                          mem_partial_include);
        if (node != NULL)
            {
            region = node->region;
            ASSERT(region != NULL);

            /* begin region_lock crit sect (ended by caller) */
            rw_lock(&region->region_lock,r);
            *code = mem_total_include((tree_node_t *)&range,
                                      (tree_node_t *)node);
            }
        });                             /* end region_tree_lock crit sect */
    
    return region;
    }
/* apply mapping options, compute region size, and round to page size */
static rvm_return_t round_region(rvm_region,seg)
    rvm_region_t    *rvm_region;        /* user region specs [in/out] */
    seg_t           *seg;               /* segment descriptor */
    {
    rvm_offset_t    big_len;

    /* see if region within segment */
    if (RVM_OFFSET_GTR(rvm_region->offset,seg->dev.num_bytes))
        return RVM_EOFFSET;
    big_len = RVM_ADD_LENGTH_TO_OFFSET(rvm_region->offset,
                                       rvm_region->length);
    if (RVM_OFFSET_LSS(big_len,rvm_region->offset))
        return RVM_EOFFSET;             /* overflow */

    /* round offset, length up and down to integral page size */
    big_len = RVM_LENGTH_TO_OFFSET(ROUND_TO_PAGE_SIZE(
                  RVM_OFFSET_TO_LENGTH(big_len)));
    rvm_region->offset = RVM_MK_OFFSET(
        RVM_OFFSET_HIGH_BITS_TO_LENGTH(rvm_region->offset),
        CHOP_TO_PAGE_SIZE(RVM_OFFSET_TO_LENGTH(rvm_region->offset)));

    /* see if at end of segment */
    if ((rvm_region->length == 0)
        || RVM_OFFSET_GTR(big_len,seg->dev.num_bytes))
        big_len = seg->dev.num_bytes;

    /* calculate actual length to map (only 32 bit lengths for now) */
    big_len = RVM_SUB_OFFSETS(big_len,rvm_region->offset);
    if (RVM_OFFSET_HIGH_BITS_TO_LENGTH(big_len) != 0)
        return RVM_ERANGE;
    rvm_region->length = RVM_OFFSET_TO_LENGTH(big_len);
    
    /* check page aligned buffer or allocate virtual memory region */
    if (rvm_region->vmaddr != NULL)
        {
        if (rvm_region->vmaddr != (char *)
                             CHOP_TO_PAGE_SIZE(rvm_region->vmaddr))
            return RVM_ERANGE;          /* buffer not page aligned */
        if (!mem_chk(rvm_region->vmaddr,rvm_region->length))
            return RVM_ERANGE;          /* buffer not within task's vm */
        }
    else
        {
        rvm_region->vmaddr =
            page_alloc(ROUND_TO_PAGE_SIZE(rvm_region->length));
        if (rvm_region->vmaddr == NULL) return RVM_ENO_MEMORY;
        }

    return RVM_SUCCESS;
    }
/* validate region and construct descriptors */
static rvm_return_t establish_range(rvm_region,region,mem_region,seg)
    rvm_region_t    *rvm_region;        /* user request region descriptor */
    region_t        **region;           /* internal region descriptor [out]*/
    mem_region_t    **mem_region;       /* region tree descriptor [out] */
    seg_t           *seg;               /* segment ptr */
    {
    mem_region_t    *mem_node;
    region_t        *new_region;
    rvm_return_t    retval;

    /* get exact region size, address */
    *region = NULL; *mem_region = NULL;
    if ((retval=round_region(rvm_region,seg)) != RVM_SUCCESS)
        return retval;

    /* build new region descriptor */
    *region = new_region = make_region();
    if (new_region == NULL) return RVM_ENO_MEMORY;
    new_region->no_copy = rvm_region->no_copy;
    new_region->offset = rvm_region->offset;
    new_region->end_offset =
        RVM_ADD_LENGTH_TO_OFFSET(rvm_region->offset,
                                 rvm_region->length);

    /* build range tree node */
    *mem_region = mem_node = make_mem_region();
    if (mem_node == NULL) return RVM_ENO_MEMORY;
    new_region->mem_region = mem_node;
    mem_node->vmaddr = new_region->vmaddr = rvm_region->vmaddr;
    mem_node->length = new_region->length
                     = (rvm_length_t)rvm_region->length;
    mem_node->region = NULL;

    /* put range tree node in tree to reserve range */
    RW_CRITICAL(region_tree_lock,w,     /* begin region_tree_lock crit sect */
        {
        if (!tree_insert(&region_tree,(tree_node_t *)mem_node,
                            mem_partial_include))
            retval = RVM_EVM_OVERLAP;         /* vm range already mapped */
        });                             /* end region_tree_lock crit sect */

    return retval;
    }
/* check for mapping dependencies on previously 
   mapped regions, or conflict with presently mapped region
   -- caller provides list locking
   returns true if dependency detected
*/
static region_t *chk_seg_mappings(chk_region,list_root)
    region_t        *chk_region;        /* region descriptor to chk*/
    list_entry_t    *list_root;         /* root of list to check */
    {
    region_t        *region;            /* internal region descriptor */

    FOR_ENTRIES_OF(*list_root,region_t,region)
        {
        /* test for overlap */
        if (dev_partial_include(&chk_region->offset,
                          &chk_region->end_offset,
                          &region->offset,&region->end_offset
                          ) == 0)
            return region;              /* overlap */
        }

    return NULL;
    }
/* check mapping dependencies within segment */
static rvm_return_t chk_dependencies(seg,region)
    seg_t           *seg;
    region_t        *region;
    {
    region_t        *x_region;          /* conflicting or dependent region */
    rvm_return_t    retval = RVM_SUCCESS;

    /* check for multiple mappings of same segment region */
    CRITICAL(seg->seg_lock,            /* begin seg_lock crit sect */
        {
        if ((x_region=chk_seg_mappings(region,&seg->map_list))
            == NULL)
            {
            /* enter region in map_list */
            region->seg = seg;
            (void)move_list_entry(NULL,&seg->map_list,
                                  &region->links);

            /* check for overlap with modified and unmapped regions of segment
               if found, must wait for truncation to get committed image of region */
            DO_FOREVER
                if ((x_region=chk_seg_mappings(region,
                                               &seg->unmap_list))
                    != NULL)        
                    {
                    (void)initiate_truncation(seg->log,100);
                    if ((retval=wait_for_truncation(seg->log,
                                                &x_region->unmap_ts))
                        != RVM_SUCCESS) goto err_exit;
                    free_region(x_region); /* can free now */
                    }
                else break;             /* no further dependencies */
            }
        else
            retval = RVM_EOVERLAP;      /* multiply mapped */
err_exit:;
        });                             /* end seg_lock crit sect */

    return retval;
    }
/* make data from segment available from mapped region */
static rvm_return_t map_data(rvm_options,region)
    rvm_options_t   *rvm_options;
    region_t        *region;
    {
    seg_t           *seg = region->seg;
    rvm_return_t    retval = RVM_SUCCESS;

    /* check for pager mapping */
    if (rvm_options != NULL)
        if (rvm_options->pager != NULL)
            {
            /* external pager interface not implemented yet */
            return RVM_EPAGER;
            }

    /* read data directly from segment */
    if (!region->no_copy)
        CRITICAL(seg->dev_lock,
            {
            if (read_dev(&seg->dev,&region->offset,
                         region->vmaddr,region->length) < 0)
                retval = RVM_EIO;
            });

    return retval;
    }
/* error exit cleanup */
static void clean_up(region,mem_region)
    region_t        *region;
    mem_region_t    *mem_region;
    {
    seg_t           *seg;

    /* kill region descriptor if created */
    if (region != NULL)
        {
        seg = region->seg;
        if (seg != NULL)
            CRITICAL(seg->seg_lock,
                {
                (void)move_list_entry(&seg->map_list,NULL,
                                       &region->links);
                });
        free_region(region);
        }

    /* kill region tree node if created */
    if (mem_region != NULL)
        {
        RW_CRITICAL(region_tree_lock,w,
            {
            (void)tree_delete(&region_tree,(tree_node_t *)mem_region,
                              mem_partial_include);
            });
        free_mem_region(mem_region);
        }
    }
/* rvm_map */
rvm_return_t rvm_map(rvm_region,rvm_options)
    rvm_region_t        *rvm_region;
    rvm_options_t       *rvm_options;
    {
    seg_t               *seg;              /* segment descriptor */
    region_t            *region = NULL;    /* new region descriptor */
    mem_region_t        *mem_region= NULL; /* new region's tree node */
    rvm_return_t        retval; 
    rvm_region_t        save_rvm_region;

    /* preliminary checks & saves */
    if (bad_init()) return RVM_EINIT;
    if ((retval=bad_region(rvm_region)) != RVM_SUCCESS)
        return retval;
    if (rvm_options != NULL)
        if ((retval=do_rvm_options(rvm_options)) != RVM_SUCCESS)
            return retval;
    if (default_log == NULL) return RVM_ELOG;
    (void)BCOPY((char *)rvm_region,(char *)&save_rvm_region,
               sizeof(rvm_region_t));

    /* find or build segment */
    seg = seg_lookup(rvm_region->data_dev,&retval);
    if (retval != RVM_SUCCESS) goto err_exit;
    if (seg == NULL)
        {                               /* must build a new segment */
        if ((seg=build_seg(rvm_region,default_log,&retval))
            == NULL) goto err_exit;
        }
    else
        /* test if segment closed by earlier (failing) rvm_terminate */
        if (seg->dev.handle == 0) return RVM_EIO;

    /* check for vm overlap with existing mappings & build descriptors */
    if ((retval = establish_range(rvm_region,&region,&mem_region,seg))
                != RVM_SUCCESS)
        goto err_exit;

    /* check for overlap with existing mappings in segment, check
       for truncation dependencies, and enter region in map_list */
    if ((retval=chk_dependencies(seg,region)) != RVM_SUCCESS)
        goto err_exit;
    /* get the data from the segment */
    if ((retval = map_data(rvm_options,region)) != RVM_SUCCESS)
        {
        rvm_region->length = 0;
        goto err_exit;
        }

    /* complete region tree node and exit*/
    mem_region->region = region;
    return RVM_SUCCESS;

  err_exit:
    clean_up(region,mem_region);
    (void)BCOPY((char *)&save_rvm_region,(char *)rvm_region,
               sizeof(rvm_region_t));
    return retval;
    }
