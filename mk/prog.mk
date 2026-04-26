include $(MK)/common.mk

PROGNAME=	$(PROG)$(EXEEXT)

CLEAN_FILES+=	$(PROGNAME) $(OBJS) $(DEPFILES)

.PHONY: all install install-prog distclean

all: $(PROGNAME)

$(PROGNAME): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LOCAL_LDFLAGS)

install: install-prog

install-prog: $(PROGNAME)
	install -d -m 755 $(DESTDIR)$(bindir)
	install -m 755 $(PROGNAME) $(DESTDIR)$(bindir)/

distclean: clean
