// Unchecked malloc(3) family functions calls.
//
// XXX: there is still a lot of work to be done here as it does not yet catch
// all.
//
// Confidence: Low
// Copyright: (C) The pkgng project, see COPYING.
// URL: https://github.com/freebsd/pkg/tree/master/tests/cocci/pkg/unchecked_malloc.cocci

@@
local idexpression n;
expression E;
@@

- n = malloc(E);
+ assert(n = malloc(E)) /* FIXME: unchecked_malloc.cocci */;
... when != (n == NULL)
    when != (n != NULL)

@@
local idexpression n;
expression E, E1;
@@

- n = calloc(E, E1);
+ assert(n = calloc(E, E1)) /* FIXME: unchecked_malloc.cocci */;
... when != (n == NULL)
    when != (n != NULL)

@@
local idexpression n;
expression E, E1;
@@

- n = realloc(E, E1);
+ assert(n = realloc(E, E1)) /* FIXME: unchecked_malloc.cocci */;
... when != (n == NULL)
    when != (n != NULL)

@@
local idexpression n;
expression E;
@@

- n = strdup(E);
+ assert(n = strdup(E)) /* FIXME: unchecked_malloc.cocci */;
... when != (n == NULL)
    when != (n != NULL)
