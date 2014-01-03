.include <bsd.own.mk>

LIB=	ucl
INTERNALLIB=

SRCS=	src/ucl_emitter.c \
	src/ucl_parser.c \
	src/ucl_util.c \
	src/ucl_hash.c \
	src/xxhash.c

CFLAGS=	-I${.CURDIR}/uthash \
	-I${.CURDIR}/include \
	-I${.CURDIR}/src

.if defined(TARGET_ARCH)
ARCH=	${TARGET_ARCH}
.else
ARCH=	${MACHINE_ARCH}
.endif

.if ${ARCH} == amd64 || ${ARCH} == x86_64
CFLAGS+=	-fPIC
.endif

OSVERSION!=	/sbin/sysctl -n kern.osreldate

.if ${OSVERSION} < 901000
.c.o:
	${CC} ${STATIC_CFLAGS} ${CFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endif

WARNS=	1
.include <bsd.lib.mk>
