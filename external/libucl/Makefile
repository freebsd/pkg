LIB=	ucl
INTERNALLIB=

SRCS=	src/ucl_emitter.c \
	src/ucl_parser.c \
	src/ucl_util.c \
	src/ucl_hash.c \
	src/xxhash.c

CFLAGS=	-I${.CURDIR}/uthash \
	-I${.CURDIR}/include \
	-I${.CURDIR}/src \
	-fPIC

OSVERSION!=	/sbin/sysctl -n kern.osreldate

.if ${OSVERSION} < 901000
.c.o:
	${CC} ${STATIC_CFLAGS} ${CFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endif

WARNS=	1
.include <bsd.lib.mk>
