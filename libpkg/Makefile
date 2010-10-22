.include <bsd.own.mk>

LIB=		pkg
INCS=		pkg.h
WARNS=		6
SHLIBDIR?=	/usr/lib
SHLIB_MAJOR=	0

SRCS=		pkg.c \
		pkgdb.c \
		pkg_compat.c \
		pkg_create.c \
		pkg_manifest.c \
		util.c

CFLAGS+=	-std=c99
CFLAGS+=	-I${.CURDIR} \
		-I${.CURDIR}/../external/jansson \
		-I${.CURDIR}/../external/sqlite \
		-I${.CURDIR}/../external/tinycdb
LDADD+=		-L${.CURDIR}/../external/jansson \
		-L${.CURDIR}/../external/sqlite \
		-L${.CURDIR}/../external/tinycdb \
		-L/usr/local/lib \
		-lcdb \
		-ljansson \
		-lsqlite3 \
		-lm \
		-larchive \
		-lz \
		-lbz2 \
		-llzma

DEBUG_FLAGS+=  -g
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif
NO_MAN=		yes

.include <bsd.lib.mk>

