// Unchecked malloc(3) family functions calls.
//
// XXX: there is still a lot of work to be done here as it does not yet catch
// all.
//
// Confidence: Low
// Copyright: (C) The pkgng project, see COPYING.
// URL: https://github.com/freebsd/pkg/tree/master/tests/cocci/pkg/unchecked_malloc.cocci

@@
expression T;
@@

T = malloc(...);
+ if (T == NULL) {
+ 	pkg_emit_errno("malloc", __func__);
+	return (EPKG_FATAL);
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = malloc(...);

@@
expression T;
@@

T = calloc(...);
+ if (T == NULL) {
+ 	pkg_emit_errno("calloc", __func__);
+	return (EPKG_FATAL);
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = calloc(...);

@@
expression T;
@@

T = realloc(...);
+ if (T == NULL) {
+ 	pkg_emit_errno("realloc", __func__);
+	return (EPKG_FATAL);
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = realloc(...);

@@
expression T;
@@

T = strdup(...);
+ if (T == NULL) {
+ 	pkg_emit_errno("strdup", __func__);
+	return (EPKG_FATAL);
+ }
... when != (T == NULL)
    when != (T != NULL)
? T = strdup(...);
