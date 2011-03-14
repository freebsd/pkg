.include <bsd.own.mk>

LIB=		pkg
INCS=		pkg.h
WARNS=		6
SHLIBDIR?=	/usr/lib
SHLIB_MAJOR=	0

SRCS=		pkg.c \
		pkg_add.c \
		pkg_conflict.c \
		pkg_create.c \
		pkg_delete.c \
		pkg_elf.c \
		pkg_error.c \
		pkg_exec.c \
		pkg_file.c \
		pkg_manifest.c \
		pkg_option.c \
		pkg_ports.c \
		pkg_repo.c \
		pkg_util.c \
		pkg_version.c \
		pkgdb.c

CFLAGS+=	-std=c99
CFLAGS+=	-I${.CURDIR} \
		-I${.CURDIR}/../external/sqlite
LDADD+=		-L${.CURDIR}/../external/sqlite \
		-lsqlite3 \
		-larchive \
		-lz \
		-lbz2 \
		-llzma \
		-lsbuf \
		-lfetch \
		-lelf \
		-lpthread

DEBUG_FLAGS+=  -g
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif
NO_MAN=		yes

.include <bsd.lib.mk>

