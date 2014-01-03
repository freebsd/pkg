// !x&y combines boolean negation with bitwise and
//
// Confidence: High
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/notand.html
// Options:

@@ expression E; constant C; @@
(
  !E & !C
|
- !E & C
+ !(E & C)
)
