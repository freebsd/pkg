.include <bsd.own.mk>

PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib/pkg/
SHLIB_DIR?=	${LIBDIR}/
SHLIB_NAME?=	${PLUGIN_NAME}.so

PLUGIN_NAME=	zfssnap
SRCS=		zfssnap.c

CFLAGS+=	-I${.CURDIR}/../../libpkg

beforeinstall:
	${INSTALL} -d ${LIBDIR}

.include <bsd.lib.mk>
