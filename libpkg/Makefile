.include <bsd.own.mk>

LIB=		pkg
INCS=		pkg.h
WARNS=		6
SHLIBDIR?=	/usr/lib
SHLIB_MAJOR=	0

SRCS=		pkg.c \
		pkgdb.c \
		pkg_conflict.c \
		pkg_file.c \
		pkg_manifest.c \
		pkg_version.c \
		util.c

CFLAGS+=	-std=c99
CFLAGS+=	-I${.CURDIR} \
		-I${.CURDIR}/../external/jansson \
		-I${.CURDIR}/../external/sqlite
LDADD+=		-L${.CURDIR}/../external/jansson \
		-L${.CURDIR}/../external/sqlite \
		-L/usr/local/lib \
		-ljansson \
		-lsqlite3 \
		-lm \
		-larchive \
		-lz \
		-lbz2 \
		-llzma \
		-lsbuf \
		-lfetch

DEBUG_FLAGS+=  -g
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif
NO_MAN=		yes

.include <bsd.lib.mk>

