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
** This file contains code used to compute a "diff" between two
** text files.
*/
#include <sys/types.h>

#include <string.h>
#include <stdlib.h>
#include <utstring.h>

#include "private/utils.h"
#include "private/xmalloc.h"

/*
** Maximum length of a line in a text file, in bytes.  (2**13 = 8192 bytes)
*/
#define LENGTH_MASK_SZ  13
#define LENGTH_MASK     ((1<<LENGTH_MASK_SZ)-1)

/*
** Information about each line of a file being diffed.
**
** The lower LENGTH_MASK_SZ bits of the hash (DLine.h) are the length
** of the line.  If any line is longer than LENGTH_MASK characters,
** the file is considered binary.
*/
typedef struct DLine DLine;
struct DLine {
  const char *z;        /* The text of the line */
  unsigned int h;       /* Hash of the line */
  unsigned short indent;  /* Indent of the line. Only !=0 with -w/-Z option */
  unsigned short n;     /* number of bytes */
  unsigned int iNext;   /* 1+(Index of next line with same the same hash) */

  /* an array of DLine elements serves two purposes.  The fields
  ** above are one per line of input text.  But each entry is also
  ** a bucket in a hash table, as follows: */
  unsigned int iHash;   /* 1+(first entry in the hash chain) */
};

/*
** Length of a dline
*/
#define LENGTH(X)   ((X)->n)

/*
** A context for running a raw diff.
**
** The aEdit[] array describes the raw diff.  Each triple of integers in
** aEdit[] means:
**
**   (1) COPY:   Number of lines aFrom and aTo have in common
**   (2) DELETE: Number of lines found only in aFrom
**   (3) INSERT: Number of lines found only in aTo
**
** The triples repeat until all lines of both aFrom and aTo are accounted
** for.
*/
typedef struct DContext DContext;
struct DContext {
  int *aEdit;        /* Array of copy/delete/insert triples */
  int nEdit;         /* Number of integers (3x num of triples) in aEdit[] */
  int nEditAlloc;    /* Space allocated for aEdit[] */
  DLine *aFrom;      /* File on left side of the diff */
  int nFrom;         /* Number of lines in aFrom[] */
  DLine *aTo;        /* File on right side of the diff */
  int nTo;           /* Number of lines in aTo[] */
  int (*same_fn)(const DLine *, const DLine *); /* Function to be used for comparing */
};

/*
** Return an array of DLine objects containing a pointer to the
** start of each line and a hash of that line.  The lower
** bits of the hash store the length of each line.
**
** Trailing whitespace is removed from each line.  2010-08-20:  Not any
** more.  If trailing whitespace is ignored, the "patch" command gets
** confused by the diff output.  Ticket [a9f7b23c2e376af5b0e5b]
**
** Return 0 if the file is binary or contains a line that is
** too long.
**
** Profiling show that in most cases this routine consumes the bulk of
** the CPU time on a diff.
*/
static DLine *break_into_lines(char *z, int *pnLine){
  int nLine, i, j, k, s, x;
  unsigned int h, h2;
  DLine *a;

  int n = strlen(z);
  /* Count the number of lines.  Allocate space to hold
  ** the returned array.
  */
  for(i=j=0, nLine=1; i<n; i++, j++){
    int c = z[i];
    if( c==0 ){
      return 0;
    }
    if( c=='\n' && z[i+1]!=0 ){
      nLine++;
      if( j>LENGTH_MASK ){
        return 0;
      }
      j = 0;
    }
  }
  if( j>LENGTH_MASK ){
    return 0;
  }
  a = xcalloc(nLine, sizeof(a[0]) );
  if( n==0 ){
    *pnLine = 0;
    return a;
  }

  /* Fill in the array */
  for(i=0; i<nLine; i++){
    for(j=0; z[j] && z[j]!='\n'; j++){}
    a[i].z = z;
    k = j;
    a[i].n = k;
    s = 0;
    for(h=0, x=s; x<k; x++){
      h = h ^ (h<<2) ^ z[x];
    }
    a[i].indent = s;
    a[i].h = h = (h<<LENGTH_MASK_SZ) | (k-s);
    h2 = h % nLine;
    a[i].iNext = a[h2].iHash;
    a[h2].iHash = i+1;
    z += j+1;
  }

  /* Return results */
  *pnLine = nLine;
  return a;
}

/*
** Return true if two DLine elements are identical.
*/
static int same_dline(const DLine *pA, const DLine *pB){
  return pA->h==pB->h && memcmp(pA->z,pB->z, pA->h&LENGTH_MASK)==0;
}


/*
** Minimum of two values
*/
static int minInt(int a, int b){ return a<b ? a : b; }

/*
** Compute the optimal longest common subsequence (LCS) using an
** exhaustive search.  This version of the LCS is only used for
** shorter input strings since runtime is O(N*N) where N is the
** input string length.
*/
static void optimalLCS(
  DContext *p,               /* Two files being compared */
  int iS1, int iE1,          /* Range of lines in p->aFrom[] */
  int iS2, int iE2,          /* Range of lines in p->aTo[] */
  int *piSX, int *piEX,      /* Write p->aFrom[] common segment here */
  int *piSY, int *piEY       /* Write p->aTo[] common segment here */
){
  int mxLength = 0;          /* Length of longest common subsequence */
  int i, j;                  /* Loop counters */
  int k;                     /* Length of a candidate subsequence */
  int iSXb = iS1;            /* Best match so far */
  int iSYb = iS2;            /* Best match so far */

  for(i=iS1; i<iE1-mxLength; i++){
    for(j=iS2; j<iE2-mxLength; j++){
      if( !p->same_fn(&p->aFrom[i], &p->aTo[j]) ) continue;
      if( mxLength && !p->same_fn(&p->aFrom[i+mxLength], &p->aTo[j+mxLength]) ){
        continue;
      }
      k = 1;
      while( i+k<iE1 && j+k<iE2 && p->same_fn(&p->aFrom[i+k],&p->aTo[j+k]) ){
        k++;
      }
      if( k>mxLength ){
        iSXb = i;
        iSYb = j;
        mxLength = k;
      }
    }
  }
  *piSX = iSXb;
  *piEX = iSXb + mxLength;
  *piSY = iSYb;
  *piEY = iSYb + mxLength;
}

/*
** Compare two blocks of text on lines iS1 through iE1-1 of the aFrom[]
** file and lines iS2 through iE2-1 of the aTo[] file.  Locate a sequence
** of lines in these two blocks that are exactly the same.  Return
** the bounds of the matching sequence.
**
** If there are two or more possible answers of the same length, the
** returned sequence should be the one closest to the center of the
** input range.
**
** Ideally, the common sequence should be the longest possible common
** sequence.  However, an exact computation of LCS is O(N*N) which is
** way too slow for larger files.  So this routine uses an O(N)
** heuristic approximation based on hashing that usually works about
** as well.  But if the O(N) algorithm doesn't get a good solution
** and N is not too large, we fall back to an exact solution by
** calling optimalLCS().
*/
static void longestCommonSequence(
  DContext *p,               /* Two files being compared */
  int iS1, int iE1,          /* Range of lines in p->aFrom[] */
  int iS2, int iE2,          /* Range of lines in p->aTo[] */
  int *piSX, int *piEX,      /* Write p->aFrom[] common segment here */
  int *piSY, int *piEY       /* Write p->aTo[] common segment here */
){
  int i, j, k;               /* Loop counters */
  int n;                     /* Loop limit */
  DLine *pA, *pB;            /* Pointers to lines */
  int iSX, iSY, iEX, iEY;    /* Current match */
  int skew = 0;              /* How lopsided is the match */
  int dist = 0;              /* Distance of match from center */
  int mid;                   /* Center of the span */
  int iSXb, iSYb, iEXb, iEYb;   /* Best match so far */
  int iSXp, iSYp, iEXp, iEYp;   /* Previous match */
  int64_t bestScore;      /* Best score so far */
  int64_t score;          /* Score for current candidate LCS */
  int span;                     /* combined width of the input sequences */

  span = (iE1 - iS1) + (iE2 - iS2);
  bestScore = -10000;
  iSXb = iSXp = iS1;
  iEXb = iEXp = iS1;
  iSYb = iSYp = iS2;
  iEYb = iEYp = iS2;
  mid = (iE1 + iS1)/2;
  for(i=iS1; i<iE1; i++){
    int limit = 0;
    j = p->aTo[p->aFrom[i].h % p->nTo].iHash;
    while( j>0
      && (j-1<iS2 || j>=iE2 || !p->same_fn(&p->aFrom[i], &p->aTo[j-1]))
    ){
      if( limit++ > 10 ){
        j = 0;
        break;
      }
      j = p->aTo[j-1].iNext;
    }
    if( j==0 ) continue;
    if( i<iEXb && j>=iSYb && j<iEYb ) continue;
    if( i<iEXp && j>=iSYp && j<iEYp ) continue;
    iSX = i;
    iSY = j-1;
    pA = &p->aFrom[iSX-1];
    pB = &p->aTo[iSY-1];
    n = minInt(iSX-iS1, iSY-iS2);
    for(k=0; k<n && p->same_fn(pA,pB); k++, pA--, pB--){}
    iSX -= k;
    iSY -= k;
    iEX = i+1;
    iEY = j;
    pA = &p->aFrom[iEX];
    pB = &p->aTo[iEY];
    n = minInt(iE1-iEX, iE2-iEY);
    for(k=0; k<n && p->same_fn(pA,pB); k++, pA++, pB++){}
    iEX += k;
    iEY += k;
    skew = (iSX-iS1) - (iSY-iS2);
    if( skew<0 ) skew = -skew;
    dist = (iSX+iEX)/2 - mid;
    if( dist<0 ) dist = -dist;
    score = (iEX - iSX)*(int64_t)span - (skew + dist);
    if( score>bestScore ){
      bestScore = score;
      iSXb = iSX;
      iSYb = iSY;
      iEXb = iEX;
      iEYb = iEY;
    }else if( iEX>iEXp ){
      iSXp = iSX;
      iSYp = iSY;
      iEXp = iEX;
      iEYp = iEY;
    }
  }
  if( iSXb==iEXb && (iE1-iS1)*(iE2-iS2)<400 ){
    /* If no common sequence is found using the hashing heuristic and
    ** the input is not too big, use the expensive exact solution */
    optimalLCS(p, iS1, iE1, iS2, iE2, piSX, piEX, piSY, piEY);
  }else{
    *piSX = iSXb;
    *piSY = iSYb;
    *piEX = iEXb;
    *piEY = iEYb;
  }
}

/*
** Expand the size of aEdit[] array to hold at least nEdit elements.
*/
static void expandEdit(DContext *p, int nEdit){
  p->aEdit = xrealloc(p->aEdit, nEdit*sizeof(int));
  p->nEditAlloc = nEdit;
}

/*
** Append a new COPY/DELETE/INSERT triple.
*/
static void appendTriple(DContext *p, int nCopy, int nDel, int nIns){
  /* printf("APPEND %d/%d/%d\n", nCopy, nDel, nIns); */
  if( p->nEdit>=3 ){
    if( p->aEdit[p->nEdit-1]==0 ){
      if( p->aEdit[p->nEdit-2]==0 ){
        p->aEdit[p->nEdit-3] += nCopy;
        p->aEdit[p->nEdit-2] += nDel;
        p->aEdit[p->nEdit-1] += nIns;
        return;
      }
      if( nCopy==0 ){
        p->aEdit[p->nEdit-2] += nDel;
        p->aEdit[p->nEdit-1] += nIns;
        return;
      }
    }
    if( nCopy==0 && nDel==0 ){
      p->aEdit[p->nEdit-1] += nIns;
      return;
    }
  }
  if( p->nEdit+3>p->nEditAlloc ){
    expandEdit(p, p->nEdit*2 + 15);
    if( p->aEdit==0 ) return;
  }
  p->aEdit[p->nEdit++] = nCopy;
  p->aEdit[p->nEdit++] = nDel;
  p->aEdit[p->nEdit++] = nIns;
}

/*
** Do a single step in the difference.  Compute a sequence of
** copy/delete/insert steps that will convert lines iS1 through iE1-1 of
** the input into lines iS2 through iE2-1 of the output and write
** that sequence into the difference context.
**
** The algorithm is to find a block of common text near the middle
** of the two segments being diffed.  Then recursively compute
** differences on the blocks before and after that common segment.
** Special cases apply if either input segment is empty or if the
** two segments have no text in common.
*/
static void diff_step(DContext *p, int iS1, int iE1, int iS2, int iE2){
  int iSX, iEX, iSY, iEY;

  if( iE1<=iS1 ){
    /* The first segment is empty */
    if( iE2>iS2 ){
      appendTriple(p, 0, 0, iE2-iS2);
    }
    return;
  }
  if( iE2<=iS2 ){
    /* The second segment is empty */
    appendTriple(p, 0, iE1-iS1, 0);
    return;
  }

  /* Find the longest matching segment between the two sequences */
  longestCommonSequence(p, iS1, iE1, iS2, iE2, &iSX, &iEX, &iSY, &iEY);

  if( iEX>iSX ){
    /* A common segment has been found.
    ** Recursively diff either side of the matching segment */
    diff_step(p, iS1, iSX, iS2, iSY);
    if( iEX>iSX ){
      appendTriple(p, iEX - iSX, 0, 0);
    }
    diff_step(p, iEX, iE1, iEY, iE2);
  }else{
    /* The two segments have nothing in common.  Delete the first then
    ** insert the second. */
    appendTriple(p, 0, iE1-iS1, iE2-iS2);
  }
}

/*
** Compute the differences between two files already loaded into
** the DContext structure.
**
** A divide and conquer technique is used.  We look for a large
** block of common text that is in the middle of both files.  Then
** compute the difference on those parts of the file before and
** after the common block.  This technique is fast, but it does
** not necessarily generate the minimum difference set.  On the
** other hand, we do not need a minimum difference set, only one
** that makes sense to human readers, which this algorithm does.
**
** Any common text at the beginning and end of the two files is
** removed before starting the divide-and-conquer algorithm.
*/
static void diff_all(DContext *p){
  int mnE, iS, iE1, iE2;

  /* Carve off the common header and footer */
  iE1 = p->nFrom;
  iE2 = p->nTo;
  while( iE1>0 && iE2>0 && p->same_fn(&p->aFrom[iE1-1], &p->aTo[iE2-1]) ){
    iE1--;
    iE2--;
  }
  mnE = iE1<iE2 ? iE1 : iE2;
  for(iS=0; iS<mnE && p->same_fn(&p->aFrom[iS],&p->aTo[iS]); iS++){}

  /* do the difference */
  if( iS>0 ){
    appendTriple(p, iS, 0, 0);
  }
  diff_step(p, iS, iE1, iS, iE2);
  if( iE1<p->nFrom ){
    appendTriple(p, p->nFrom - iE1, 0, 0);
  }

  /* Terminate the COPY/DELETE/INSERT triples with three zeros */
  expandEdit(p, p->nEdit+3);
  if( p->aEdit ){
    p->aEdit[p->nEdit++] = 0;
    p->aEdit[p->nEdit++] = 0;
    p->aEdit[p->nEdit++] = 0;
  }
}

/*
** Generate a report of the differences between files pA and pB.
** If pOut is not NULL then a unified diff is appended there.  It
** is assumed that pOut has already been initialized.  If pOut is
** NULL, then a pointer to an array of integers is returned.
** The integers come in triples.  For each triple,
** the elements are the number of lines copied, the number of
** lines deleted, and the number of lines inserted.  The vector
** is terminated by a triple of all zeros.
**
** This diff utility does not work on binary files.  If a binary
** file is encountered, 0 is returned and pOut is written with
** text "cannot compute difference between binary files".
*/
int *
text_diff(
  char *pA,   /* FROM file */
  char *pB   /* TO file */
){
  DContext c;

  /* Prepare the input files */
  memset(&c, 0, sizeof(c));
  c.same_fn = same_dline;
  c.aFrom = break_into_lines(pA, &c.nFrom);
  c.aTo = break_into_lines(pB, &c.nTo);
  if( c.aFrom==0 || c.aTo==0 ){
    free(c.aFrom);
    free(c.aTo);
    return 0;
  }

  /* Compute the difference */
  diff_all(&c);
    /* If a context diff is not requested, then return the
    ** array of COPY/DELETE/INSERT triples.
    */
    free(c.aFrom);
    free(c.aTo);
    return c.aEdit;
}
