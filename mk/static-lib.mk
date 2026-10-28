include $(MK)/common.mk

CLEAN_FILES+=	lib$(LIB).a lib$(LIB)_pic.a $(OBJS) $(SHOBJS) $(DEPFILES)

all: lib$(LIB).a lib$(LIB)_pic.a

lib$(LIB).a: $(OBJS)
	rm -f $@
	$(AR) cr $@ $(OBJS)
	$(RANLIB) $@

lib$(LIB)_pic.a: $(SHOBJS)
	rm -f $@
	$(AR) cr $@ $(SHOBJS)
	$(RANLIB) $@

distclean: clean
