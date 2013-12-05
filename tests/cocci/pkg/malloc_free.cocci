// An malloc(3) is not matched by an free(3) before an error return.
//
// This file has been modified for pkgng, in particular the `when' conditions
// are to be adapted when needed.
//
// Confidence: Moderate
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/alloc_free.html
// Options:

@r exists@
local idexpression n;
statement S1,S2;
expression E, E1;
expression *ptr != NULL;
type T;
position p1,p2;
@@

(
if ((n = malloc@p1(...)) == NULL) S1
|
n = malloc@p1(...)
)
... when != free((T)n)
    when != if (...) { <+... free((T)n) ...+> } else S2
    when != true n == NULL  || ...
    when != n = (T)E
    when != E = (T)n
    when != HASH_ADD_INT(E, E1, n)
    when != HASH_ADD_STR(E, E1, n)
    when != HASH_ADD_INO(E, E1, n)
    when != LL_APPEND(E, n)
(
  return \(0\|<+...n...+>\|ptr\);
|
return@p2 ...;
)

@script:python@
p1 << r.p1;
p2 << r.p2;
@@

cocci.print_main("",p1)
cocci.print_sec("return",p2)
