.include <bsd.own.mk>

LIB=		mongoose
INCS=		mongoose.h
#WARNS=		6
PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
SHLIB_MAJOR=	0

SRCS=		mongoose.c

CFLAGS+=	-std=c99 -W -Wall -pedantic -O2 -fPIC -shared
CFLAGS+=	-I${INCLUDEDIR}

LDADD+=		-lpthread

DEBUG_FLAGS+=  -g -O0

.include <bsd.lib.mk>
