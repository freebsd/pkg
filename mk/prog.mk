include $(MK)/common.mk

PROGNAME=	$(PROG)$(EXEEXT)

all: $(PROGNAME)

$(PROGNAME): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LOCAL_LDFLAGS)

clean:
	rm -f $(PROGNAME) $(OBJS)

install:
