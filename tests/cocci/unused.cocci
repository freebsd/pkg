// A variable is only initialized to a constant and is never used otherwise
//
// Confidence: High
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/unused.html
// Options:

@e@
identifier i;
position p;
type T;
@@

extern T i@p;

@@
type T;
identifier i;
constant C;
position p != e.p;
@@

- T i@p;
  <+... when != i
- i = C;
  ...+>
