include $(MK)/common.mk

all: lib$(LIB)$(LIBSOEXT) lib$(LIB)$(SH_SOEXT) lib$(LIB).a

lib$(LIB)$(SH_SOEXT): lib$(LIB)$(LIBSOEXT)
	ln -sf lib$(LIB)$(LIBSOEXT) $@

lib$(LIB)$(LIBSOEXT): $(SHOBJS)
	$(CC) $(SH_LDFLAGS) $(LDFLAGS) $(LOCAL_LDFLAGS) $(SH_PREFIX)$@ -o $@ $(SHOBJS)

lib$(LIB).a: $(OBJS)
	$(AR) cr $@ $(OBJS) $(EXTRA_DEPS)
	$(RANLIB) $@

clean:
	rm -f lib$(LIB).a lib$(LIB)$(SH_SOEXT) lib$(LIB)$(LIBSOEXT) $(OBJS) $(SHOBJS)

install:
