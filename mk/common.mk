CFLAGS?=	-O2 -pipe
OBJS=	${SRCS:.c=.o}
SHOBJS?=	${SRCS:.c=.pico}
CFLAGS+=	$(CPPFLAGS)

.SUFFIXES: .pico .in

.c.o:
	$(CC) -Wall -Wextra -std=gnu99 -D_GNU_SOURCE=1 -o $@ -c $(CFLAGS) $(LOCAL_CFLAGS) $<

.c.pico:
	$(CC) -Wall -Wextra -std=gnu99 -D_GNU_SOURCE=1 -o $@ -c $(CFLAGS) $(LOCAL_CFLAGS) $(SHOBJ_CFLAGS) $<

.in:
	sed -e 's|@prefix@|$(PREFIX)|g; s|@abs_top_srcdir@|$(top_srcdir)|g' \
		-e 's|@VERSION@|$(version)|g' $< > $@
