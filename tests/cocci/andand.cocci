// The right argument of || or && is dereferencing something known to be NULL
//
// Confidence: High
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/andand.html
// Options:

@ expression@
expression E;
identifier fld;
@@

- !E &&
+ !E ||
  <+...E->fld...+>

@ expression@
expression E;
identifier fld;
@@

- E ||
+ E &&
  <+...E->fld...+>
