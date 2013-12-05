// Continue at the end of a for loop has no purpose
//
// Confidence: Moderate
// Copyright: (C) Gilles Muller, Julia Lawall, EMN, INRIA, DIKU.  GPLv2.
// URL: http://coccinelle.lip6.fr/rules/continue.html
// Options:

@@
position p;
@@

for (...;...;...) {
   ...
   if (...) {
     ...
-   continue;
   }
}

@@
position p;
@@

while (...) {
   ...
   if (...) {
     ...
-   continue;
   }
}
