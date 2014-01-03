// strlcpy(3) is meant to be checked for returned value.
//
// Confidence: High
// Copyright: (C) The pkgng project, see COPYING.
// URL: https://github.com/freebsd/pkg/tree/master/tests/cocci/pkg/strlcpy.cocci

@@
expression E, E1, S;
@@

- strlcpy(E, E1, S);
+ assert(strlcpy(E, E1, S) < S)/* FIXME: strlcpy.cocci */;
