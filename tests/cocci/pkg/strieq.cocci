// Use STRIEQ/STRMACRO macro when possible

@@
expression E, E1;
@@

- (strcasecmp(E, E1) == 0)
+ STRIEQ(E, E1)

@@
expression E, E1;
@@

- (strcmp(E, E1) == 0)
+ STREQ(E, E1)

@@
expression E, E1;
@@

- (strcasecmp(E, E1) != 0)
+ !STRIEQ(E, E1)

@@
expression E, E1;
@@

- (strcmp(E, E1) != 0)
+ !STREQ(E, E1)
