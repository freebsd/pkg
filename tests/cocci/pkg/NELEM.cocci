// Use the macro NELEM when possible.
// based on: http://coccinelle.lip6.fr/rules/array.html
//
// Confidence: High
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/array.html
// Options: -I ... -all_includes can give more complete results

@@
type T;
T[] E;
@@

- (sizeof(E)/sizeof(*E))
+ NELEM(E)

@@
type T;
T[] E;
@@

- (sizeof(E)/sizeof(E[...]))
+ NELEM(E)

@@
type T;
T[] E;
@@

- (sizeof(E)/sizeof(T))
+ NELEM(E)
