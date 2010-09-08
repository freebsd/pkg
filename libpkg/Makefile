.include <bsd.own.mk>

LIB=	pkg
INCS=	pkg.h pkgdb.h
WARNS=	6
SHLIBDIR?=	/usr/lib
SHLIB_MAJOR=	0

SRCS=	pkgdb.c pkgdb_cache.c pkg_compat.c util.c

CFLAGS+=  -std=c99
CFLAGS+=  -I${.CURDIR} -I${.CURDIR}/../external/tinycdb -I${.CURDIR}/../external/cjson/
LDADD+=   -L${.CURDIR}/../external/tinycdb -L${.CURDIR}/../external/cjson -lcdb -lcjson -lm

DEBUG_FLAGS+=  -g
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=    -pg
.endif
NO_MAN=   yes

.include <bsd.lib.mk>

