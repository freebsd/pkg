/*-
 * Copyright (c) 2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
 *~
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *~
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code has been extracted from the fossil scm code
 * and modified
 */

/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This module implements a 3-way merge
*/

#include <sys/types.h>
#include <sys/sbuf.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "private/utils.h"

/* The minimum of two integers */
#ifndef min
#  define min(A,B)  (A<B?A:B)
#endif

/*
** Compare N lines of text from pV1 and pV2.  If the lines
** are the same, return true.  Return false if one or more of the N
** lines are different.
**
** The cursors on both pV1 and pV2 is unchanged by this comparison.
*/
static int sameLines(const char *pV1, const char *pV2, int N){
  int i;
  char c;

  if( N==0 ) return 1;
  for(i=0; (c=pV1[i])==pV2[i]; i++){
    if( c=='\n' || c==0 ){
      N--;
      if( N==0 || c==0 ) return 1;
    }
  }
  return 0;
}

/*
** Look at the next edit triple in both aC1 and aC2.  (An "edit triple" is
** three integers describing the number of copies, deletes, and inserts in
** moving from the original to the edited copy of the file.) If the three
** integers of the edit triples describe an identical edit, then return 1.
** If the edits are different, return 0.
*/
static int sameEdit(
  int *aC1,      /* Array of edit integers for file 1 */
  int *aC2,      /* Array of edit integers for file 2 */
  const char *pV1,     /* Text of file 1 */
  const char *pV2      /* Text of file 2 */
){
  if( aC1[0]!=aC2[0] ) return 0;
  if( aC1[1]!=aC2[1] ) return 0;
  if( aC1[2]!=aC2[2] ) return 0;
  if( sameLines(pV1, pV2, aC1[2]) ) return 1;
  return 0;
}

static int
sbuf_copy_lines(struct sbuf *to, const char *from, int N)
{
	int cnt = 0;
	int i;

	if (N == 0)
		return (0);

	for (i = 0; from[i] != '\0'; i++) {
		if (from[i] == '\n') {
			cnt++;
			continue;
		}
		if (cnt == N)
			break;
	}

	if (to == NULL)
		return (i);
		
	if (sbuf_len(to) > 0 &&
	    sbuf_data(to)[sbuf_len(to)-1] != '\n')
		sbuf_putc(to, '\n');
	sbuf_bcat(to, from, i);
	sbuf_finish(to);

	return (i);
}

/*
** Do a three-way merge.  Initialize pOut to contain the result.
**
** The merge is an edit against pV2.  Both pV1 and pV2 have a
** common origin at pPivot.  Apply the changes of pPivot ==> pV1
** to pV2.
**
** The return is 0 upon complete success. If any input file is binary,
** -1 is returned and pOut is unmodified.  If there are merge
** conflicts, the merge proceeds as best as it can and the number
** of conflicts is returns
*/
static int
sbuf_merge(char *pPivot, char *pV1, char *pV2, struct sbuf *pOut){
  int *aC1;              /* Changes from pPivot to pV1 */
  int *aC2;              /* Changes from pPivot to pV2 */
  int i1, i2;            /* Index into aC1[] and aC2[] */
  int nCpy, nDel, nIns;  /* Number of lines to copy, delete, or insert */
  int limit1, limit2;    /* Sizes of aC1[] and aC2[] */

  sbuf_clear(pOut);         /* Merge results stored in pOut */

  /* Compute the edits that occur from pPivot => pV1 (into aC1)
  ** and pPivot => pV2 (into aC2).  Each of the aC1 and aC2 arrays is
  ** an array of integer triples.  Within each triple, the first integer
  ** is the number of lines of text to copy directly from the pivot,
  ** the second integer is the number of lines of text to omit from the
  ** pivot, and the third integer is the number of lines of text that are
  ** inserted.  The edit array ends with a triple of 0,0,0.
  */
  aC1 = text_diff(pPivot, pV1);
  aC2 = text_diff(pPivot, pV2);
  if( aC1==0 || aC2==0 ){
    free(aC1);
    free(aC2);
    return -1;
  }

  /* Determine the length of the aC1[] and aC2[] change vectors */
  for(i1=0; aC1[i1] || aC1[i1+1] || aC1[i1+2]; i1+=3){}
  limit1 = i1;
  for(i2=0; aC2[i2] || aC2[i2+1] || aC2[i2+2]; i2+=3){}
  limit2 = i2;

  /* Loop over the two edit vectors and use them to compute merged text
  ** which is written into pOut.  i1 and i2 are multiples of 3 which are
  ** indices into aC1[] and aC2[] to the edit triple currently being
  ** processed
  */
  i1 = i2 = 0;
  while( i1<limit1 && i2<limit2 ){

    if( aC1[i1]>0 && aC2[i2]>0 ){
      /* Output text that is unchanged in both V1 and V2 */
      nCpy = min(aC1[i1], aC2[i2]);
      pPivot += sbuf_copy_lines(pOut, pPivot, nCpy);
      pV1 += sbuf_copy_lines(NULL, pV1, nCpy);
      pV2 += sbuf_copy_lines(NULL, pV2, nCpy);
      aC1[i1] -= nCpy;
      aC2[i2] -= nCpy;
    }else
    if( aC1[i1] >= aC2[i2+1] && aC1[i1]>0 && aC2[i2+1]+aC2[i2+2]>0 ){
      /* Output edits to V2 that occurs within unchanged regions of V1 */
      nDel = aC2[i2+1];
      nIns = aC2[i2+2];
      pPivot += sbuf_copy_lines(NULL, pPivot, nDel);
      pV1 += sbuf_copy_lines(NULL, pV1, nDel);
      pV2 += sbuf_copy_lines(pOut, pV2, nIns);
      aC1[i1] -= nDel;
      i2 += 3;
    }else
    if( aC2[i2] >= aC1[i1+1] && aC2[i2]>0 && aC1[i1+1]+aC1[i1+2]>0 ){
      /* Output edits to V1 that occur within unchanged regions of V2 */
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      pPivot += sbuf_copy_lines(NULL, pPivot, nDel);
      pV2 += sbuf_copy_lines(NULL, pV2, nDel);
      pV1 += sbuf_copy_lines(pOut, pV1, nIns);
      aC2[i2] -= nDel;
      i1 += 3;
    }else
    if( sameEdit(&aC1[i1], &aC2[i2], pV1, pV2) ){
      /* Output edits that are identical in both V1 and V2. */
      nDel = aC1[i1+1];
      nIns = aC1[i1+2];
      pPivot += sbuf_copy_lines(NULL, pPivot, nDel);
      pV1 += sbuf_copy_lines(pOut, pV1, nIns);
      pV2 += sbuf_copy_lines(NULL, pV2, nIns);
      i1 += 3;
      i2 += 3;
    }else
    {
	    return (-1);
   }

    /* If we are finished with an edit triple, advance to the next
    ** triple.
    */
    if( i1<limit1 && aC1[i1]==0 && aC1[i1+1]==0 && aC1[i1+2]==0 ) i1+=3;
    if( i2<limit2 && aC2[i2]==0 && aC2[i2+1]==0 && aC2[i2+2]==0 ) i2+=3;
  }

  /* When one of the two edit vectors reaches its end, there might still
  ** be an insert in the other edit vector.  Output this remaining
  ** insert.
  */
  if( i1<limit1 && aC1[i1+2]>0 ){
    sbuf_copy_lines(pOut, pV1, aC1[i1+2]);
  }else if( i2<limit2 && aC2[i2+2]>0 ){
    sbuf_copy_lines(pOut, pV2, aC2[i2+2]);
  }

  free(aC1);
  free(aC2);
  return 0;
}

/*
** This routine is a wrapper around blob_merge() with the following
** enhancements:
**
**    (1) If the merge-command is defined, then use the external merging
**        program specified instead of the built-in blob-merge to do the
**        merging.  Panic if the external merger fails.
**        ** Not currently implemented **
**
**    (2) If gmerge-command is defined and there are merge conflicts in
**        blob_merge() then invoke the external graphical merger to resolve
**        the conflicts.
**
**    (3) If a merge conflict occurs and gmerge-command is not defined,
**        then write the pivot, original, and merge-in files to the
**        filesystem.
*/
int merge_3way(
  char *pPivot,       /* Common ancestor (older) */
  char *pV1,    /* Name of file for version merging into (mine) */
  char *pV2,          /* Version merging from (yours) */
  struct sbuf *pOut         /* Output written here */
){
  int rc;             /* Return code of subroutines and this routine */

  rc = sbuf_merge(pPivot, pV1, pV2, pOut);
  return rc;
}
