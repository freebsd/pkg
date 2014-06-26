// A variable that is declared as unsigned should not be tested to be less than
// zero.
//
// Confidence: High
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/find_unsigned.html
// Options: -all_includes

@u@ type T; unsigned T i; position p; @@

 i@p < 0

@script:python@
p << u.p;
i << u.i;
@@

print "* file: %s signed reference to unsigned %s on line %s" % (p[0].file,i,p[0].line)

