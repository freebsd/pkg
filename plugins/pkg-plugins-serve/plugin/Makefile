.include <bsd.own.mk>

LIB=		pkg-plugin-serve
INCS=		serve.h
WARNS=		6
PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
SHLIB_MAJOR=	0

SRCS=		serve.c

CFLAGS+=	-std=c99 -fPIC -shared
CFLAGS+=	-I${INCLUDEDIR} \
		-I${.CURDIR}/mongoose

DEBUG_FLAGS+=  -g -O0

.include <bsd.lib.mk>
