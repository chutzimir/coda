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

   Copyright (C) 1998  John-Anthony Owens, Samuel Ieong, Rudi Seitz

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

*/


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <db.h>
#include <coda_assert.h>
#include "pdb.h"

void pdb_pack(PDB_profile *r, void **data)
{
	DBT *d;
	int32_t tmp[1024];
	int off, len;

	/* Pack the id and name */
	tmp[0] = htonl(r->id);
	off = 1;
	if(r->name != NULL){
		/* Have to store length because may be zero length */
		len = strlen(r->name) * sizeof(char);
		tmp[off] = htonl(len);
		/* Convert ot network layer while copying */
		memcpy((char *)&tmp[2], r->name, len);
		off = 2+len;
	} else {
		tmp[1] = 0;
		off = 2;
	}

	/* Pack the owner id and name */
	tmp[off++] = htonl(r->owner_id);
	if(r->owner_name != NULL){
		len = strlen(r->owner_name) * sizeof(char);
		/* Have to store length because may be zero length */
		tmp[off++] = htonl(len);
		memcpy((char *)&tmp[off], r->owner_name, len);
		off += len;
	} else
		tmp[off++] = 0;
	
	/* Pack the lists */
	off += pdb_array_pack(&tmp[off],&(r->member_of));
	off += pdb_array_pack(&tmp[off],&(r->cps));
	off += pdb_array_pack(&tmp[off],&(r->groups_or_members));

	CODA_ASSERT(off < 1024);
	
	/* The memory allocated here is freed in PDB_db_write */
	d = malloc(sizeof(DBT));
	d->data = malloc(off * sizeof(int32_t));
	memcpy(d->data, (char *) tmp,off * sizeof(int32_t));
	d->size = off*sizeof(int32_t);
	*data = (void *)d;
}


void pdb_unpack(PDB_profile *r, void *data)
{
	DBT *d = (DBT *) data;
	int32_t *tmp = (int32_t *) d->data;
	int off, len;

	if(d->size == 0){
		r->id = 0;
		free(d);
		return;
	}
	/* Unpack the id and name */
	r->id = ntohl(tmp[0]);
	off = 1;
	len = ntohl(tmp[off++]);
	r->name = malloc(len+1);
	memcpy(r->name, (char *)&tmp[off], len);
	r->name[len] = '\0';
	off += len;

	/* Unpack the owner id and name */
	r->owner_id = ntohl(tmp[off++]);
	len = ntohl(tmp[off++]);
	r->owner_name = malloc(len+1);
	memcpy(r->owner_name, (char *)&tmp[off], ntohl(tmp[off-1]));
	r->owner_name[len] = '\0';
	off += len;

	/* Unpack the lists */
	off += pdb_array_unpack(&tmp[off],&(r->member_of));
	off += pdb_array_unpack(&tmp[off],&(r->cps));
	off += pdb_array_unpack(&tmp[off],&(r->groups_or_members));

	CODA_ASSERT(off < 1024);

	/* Here we free the data allocated by PDB_db_read and db_fetch */
	free(d->data);
	free(d);
}
