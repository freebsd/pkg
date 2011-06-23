.include <bsd.own.mk>

LIB=		pkgng
INCS=		pkg.h
WARNS=		6
SHLIBDIR?=	/usr/lib
SHLIB_MAJOR=	0

SRCS=		pkg.c \
		pkg_add.c \
		pkg_attributes.c \
		pkg_config.c \
		pkg_create.c \
		pkg_delete.c \
		pkg_elf.c \
		pkg_event.c \
		pkg_error.c \
		pkg_handle.c \
		pkg_jobs.c \
		pkg_manifest.c \
		pkg_ports.c \
		pkg_repo.c \
		pkg_create_repo.c \
		pkg_util.c \
		pkg_version.c \
		pkgdb.c \
		fetch.c \
		packing.c \
		scripts.c

CFLAGS+=	-std=c99
CFLAGS+=	-I${.CURDIR} \
		-I${.CURDIR}/../external/sqlite \
		-I${.CURDIR}/../external/libyaml/include
LDADD+=		-L${.CURDIR}/../external/sqlite \
		-L${.CURDIR}/../external/libyaml \
		-lsqlite3 \
		-lyaml \
		-larchive \
		-lsbuf \
		-lfetch \
		-lelf \
		-lutil \
		-lpthread

DEBUG_FLAGS+=  -g -O0 -DDEBUG
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif
NO_MAN=		yes

.include <bsd.lib.mk>

