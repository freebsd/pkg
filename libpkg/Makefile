.include <bsd.own.mk>

LIB=		pkg
INCS=		pkg.h
WARNS=		6
PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
SHLIB_MAJOR=	0

#gr_utils.c has to be deleted as soon as it goes in base
SRCS=		backup.c \
		dns_utils.c \
		elfhints.c \
		fetch.c \
		packing.c \
		pkg.c \
		pkg_add.c \
		pkg_attributes.c \
		pkg_config.c \
		pkg_create.c \
		pkg_delete.c \
		pkg_elf.c \
		pkg_event.c \
		pkg_jobs.c \
		pkg_manifest.c \
		pkg_ports.c \
		pkg_repo.c \
		pkg_version.c \
		pkgdb.c \
		rcscripts.c \
		rsa.c \
		scripts.c \
		update.c \
		usergroup.c \
		utils.c

HAVE_GRUTILS!=	grep -q gr_copy /usr/include/libutil.h && echo yes || echo no

.if ${HAVE_GRUTILS} != yes
SRCS+=	gr_util.c
.else
CFLAGS+=	-DHAVE_GRUTILS
.endif

CFLAGS+=	-std=c99
CFLAGS+=	-I${.CURDIR} \
		-I${.CURDIR}/../external/sqlite \
		-I${.CURDIR}/../external/libyaml/include
STATIC_CFLAGS+=	-DSTATIC_LINKAGE
LDADD+=		-L${.OBJDIR}/../external/sqlite \
		-L${.OBJDIR}/../external/libyaml \
		-lsqlite3 \
		-lyaml \
		-larchive \
		-lsbuf \
		-lfetch \
		-lelf \
		-lutil \
		-lpthread

.if exists(/usr/include/edit/readline/readline.h)
LDADD+=		-ledit
.endif

CFLAGS+=	-DPREFIX=\"${PREFIX}\"

DEBUG_FLAGS+=  -g -O0
STRIP=
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif
NO_MAN=		yes

.include <bsd.lib.mk>

