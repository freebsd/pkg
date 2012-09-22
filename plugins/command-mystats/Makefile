.include <bsd.own.mk>

PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib/pkg/command
SHLIB_DIR?=	${LIBDIR}/
SHLIB_NAME?=	${PLUGIN_NAME}.so

PLUGIN_NAME=	command-mystats
SRCS=		mystats.c

CFLAGS+=	-I${.CURDIR}/../../libpkg

beforeinstall:
	${INSTALL} -d ${LIBDIR}

.include <bsd.lib.mk>
