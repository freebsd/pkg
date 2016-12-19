// Unchecked malloc(3) family functions calls.
//
// XXX: The generated code is a guide for identifying missing checks.  Check
// the caller to ensure if pkg_fatal_errno() is correct, or whether that needs
// to use pkg_errno() instead with the appropriate return value for that
// function.
//
// Confidence: Moderate
// Copyright: (C) The pkgng project, see COPYING.
// URL: https://github.com/freebsd/pkg/tree/master/tests/cocci/pkg/unchecked_malloc.cocci

@@
expression T;
@@

T = malloc(...);
+ if (T == NULL) {
+ 	pkg_fatal_errno("%s: %s", __func__, "malloc");
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = malloc(...);

@@
expression T;
@@

T = calloc(...);
+ if (T == NULL) {
+ 	pkg_fatal_errno("%s: %s", __func__, "calloc");
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = calloc(...);

@@
expression T;
@@

T = realloc(...);
+ if (T == NULL) {
+ 	pkg_fatal_errno("%s: %s", __func__, "realloc");
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = realloc(...);

@@
expression T;
@@

T = strdup(...);
+ if (T == NULL) {
+ 	pkg_fatal_errno("%s: %s", __func__, "strdup");
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = strdup(...);
