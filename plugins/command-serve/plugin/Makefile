.include <bsd.own.mk>

PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib/pkg/commands
SHLIB_DIR?=	${LIBDIR}/
SHLIB_NAME?=	${PLUGIN_NAME}.so

PLUGIN_NAME=	serve
SRCS=		serve.c

CFLAGS+=	-I${.CURDIR}/../../../libpkg \
		-I${.CURDIR}/../mongoose

LDADD+=		-L${.OBJDIR}/../mongoose \
		-lmongoose

beforeinstall:
	${INSTALL} -d ${LIBDIR}

.include <bsd.lib.mk>
