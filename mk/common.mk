CFLAGS?=	-O2 -pipe
OBJS=	${SRCS:.c=.o}
SHOBJS?=	${SRCS:.c=.pico}
DEPFILES=	${OBJS:.o=.Po} ${SHOBJS:.pico=.Ppico}
CFLAGS+=	$(CPPFLAGS)
CFLAGS+=	-Werror=implicit-function-declaration
CFLAGS+=	-Werror=return-type

-include $(DEPFILES)

.SUFFIXES: .pico .in

.c.o:
	$(CC) -Wall -Wextra -std=gnu99 -D_GNU_SOURCE=1 -MT $@ -MD -MP -MF $*.Tpo -o $@ -c $(CFLAGS) $(LOCAL_CFLAGS) $<
	mv $*.Tpo $*.Po

.c.pico:
	$(CC) -Wall -Wextra -std=gnu99 -D_GNU_SOURCE=1 -MT $@ -MD -MP -MF $*.Tpico -o $@ -c $(CFLAGS) $(LOCAL_CFLAGS) $(SHOBJ_CFLAGS) $<
	mv $*.Tpico $*.Ppico

.in:
	sed -e 's|@prefix@|$(PREFIX)|g; s|@abs_top_srcdir@|$(top_srcdir)|g' \
		-e 's|@VERSION@|$(version)|g' $< > $@
