include $(MK)/common.mk

CLEAN_FILES+=	lib$(LIB).a lib$(LIB)$(SH_SOEXT) lib$(LIB)$(LIBSOEXT) $(OBJS) $(SHOBJS) $(DEPFILES)

all: lib$(LIB)$(LIBSOEXT) lib$(LIB)$(SH_SOEXT) lib$(LIB).a

lib$(LIB)$(SH_SOEXT): lib$(LIB)$(LIBSOEXT)
	ln -sf lib$(LIB)$(LIBSOEXT) $@

lib$(LIB)$(LIBSOEXT): $(SHOBJS)
	$(CC) $(SH_LDFLAGS) $(LDFLAGS) $(LOCAL_LDFLAGS) $(SH_PREFIX)$@ -o $@ $(SHOBJS)

lib$(LIB).a: $(OBJS)
	$(AR) cr $@ $(OBJS) $(EXTRA_DEPS)
	$(RANLIB) $@

install: install-lib

install-lib: lib$(LIB)$(LIBSOEXT) lib$(LIB).a
	install -d -m 755 $(DESTDIR)$(libdir)
	$(INSTALL_LIB) -m 755 lib$(LIB)$(LIBSOEXT) $(DESTDIR)$(libdir)/
	ln -sf lib$(LIB)$(LIBSOEXT) $(DESTDIR)$(libdir)/lib$(LIB)$(SH_SOEXT)
	install -m 644 lib$(LIB).a $(DESTDIR)$(libdir)/

distclean: clean
