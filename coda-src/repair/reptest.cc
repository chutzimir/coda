/* BLURB gpl

                           Coda File System
                              Release 6

          Copyright (c) 1987-2003 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

#*/

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include "coda_assert.h"
#include "coda_string.h"
/* #include <ci.h> */
#include <rpc2/rpc2.h> */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#include <vice.h>
#include "repio.h"

struct listhdr *harray;
int hcount;
int  repair_DebugFlag;

int main(int argc, char **argv) {
  unsigned int j;
  int i, rc;
  struct listhdr *newha;
  int newhc;

  rc = repair_parsefile(argv[1], &hcount, &harray);
  if (rc < 0)
    return -1;

  rc = repair_putdfile("/tmp/xxx", hcount, harray);
  if (rc) {
    printf("repair_putdfile() failed\n");
    return -1;
  }

  rc = repair_getdfile("/tmp/xxx", &newhc, &newha);
  if (rc) {
    printf("repair_getdfile() failed\n");
    return -1;
  }

  for (i = 0; i < newhc; i++) {
    printf("\n** Replica %lu ***\n", newha[i].replicaId);
    for (j = 0; j < newha[i].repairCount; j++)
      repair_printline(&newha[i].repairList[j], stdout);
  }

  return 0;
}
