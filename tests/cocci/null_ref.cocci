// find cases where a pointer is dereferenced and then compared to NULL
//
// Confidence: High
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/null_ref.html
// Options:

@match exists@
expression x, E,E1;
identifier fld;
position p1,p2;
@@

(
x = E;
... when != \(x = E1\|&x\)
x@p2 == NULL
... when any
|
x = E
... when != \(x = E1\|&x\)
x@p2 == NULL
... when any
|
x != NULL && (<+...x->fld...+>)
|
x == NULL || (<+...x->fld...+>)
|
x != NULL ? (<+...x->fld...+>) : E
|
&x->fld
|
x@p1->fld
... when != \(x = E\|&x\)
x@p2 == NULL
... when any
)

@other_match exists@
expression match.x, E1, E2;
position match.p1,match.p2;
@@

(
x = E1
|
&x
)
... when != \(x = E2\|&x\)
    when != x@p1
x@p2

@other_match1 exists@
expression match.x, E2;
position match.p1,match.p2;
@@

... when != \(x = E2\|&x\)
    when != x@p1
x@p2

@ script:python depends on !other_match && !other_match1@
p1 << match.p1;
p2 << match.p2;
@@

cocci.print_main("",p1)
cocci.print_sec("NULL test",p2)
