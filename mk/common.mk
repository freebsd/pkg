CFLAGS?=	-O2 -pipe
OBJS=	${SRCS:.c=.o}
SHOBJS?=	${SRCS:.c=.pico}
DEPFILES=	${OBJS:.o=.Po} ${SHOBJS:.pico=.Ppico}
CFLAGS+=	$(CPPFLAGS)
CFLAGS+=	-Werror=implicit-function-declaration
CFLAGS+=	-Werror=return-type

# bmake's traditional include support treats empty strings in the expanded
# result (whether because the variable is empty or there are consecutive
# whitespace characters) as file names, and thus tries to read the containing
# directory as a Makefile, which fails, and isn't ignored since it exists.
# Work around this quirky behaviour by adding an extra entry that should never
# exist and then normalize its whitespace during substitution with :=.
DEPFILES_NONEMPTY=	$(DEPFILES) /nonexistent
-include $(DEPFILES_NONEMPTY:=)

.SUFFIXES: .pico .in .bin .binin

.c.o:
	$(CC) -Wall -Wextra -std=gnu99 -D_GNU_SOURCE=1 -MT $@ -MD -MP -MF $*.Tpo -o $@ -c $(CFLAGS) $(LOCAL_CFLAGS) $<
	mv $*.Tpo $*.Po

.c.pico:
	$(CC) -Wall -Wextra -std=gnu99 -D_GNU_SOURCE=1 -MT $@ -MD -MP -MF $*.Tpico -o $@ -c $(CFLAGS) $(LOCAL_CFLAGS) $(SHOBJ_CFLAGS) $<
	mv $*.Tpico $*.Ppico

.in:
	sed -e 's|@prefix@|$(PREFIX)|g; s|@abs_top_srcdir@|$(top_srcdir)|g' \
		-e 's|@VERSION@|$(version)|g' $< > $@

.binin.bin:
	cp $< $@
