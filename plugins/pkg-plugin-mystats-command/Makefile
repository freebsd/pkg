.include <bsd.own.mk>

LIB=		pkg-plugin-mystats
INCS=		mystats.h
#WARNS=		6
PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
SHLIB_MAJOR=	0

SRCS=		mystats.c

CFLAGS+=	-std=c99 -fPIC -shared
CFLAGS+=	-I${INCLUDEDIR}

DEBUG_FLAGS+=  -g -O0

.include <bsd.lib.mk>
