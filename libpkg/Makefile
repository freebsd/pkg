.include <bsd.own.mk>

LIB=		pkg
INCS=		pkg.h
WARNS=		6
PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
SHLIB_MAJOR=	1

MANDIR=		${PREFIX}/man/man

PC=		pkg.pc
PKGH=		pkg.h
SRCS=		${PC} \
		${PKGH} \
		backup.c \
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
		pkg_printf.c \
		pkg_repo.c \
		pkg_status.c \
		pkg_version.c \
		pkgdb.c \
		pkgdb_repo.c \
		rcscripts.c \
		rsa.c \
		ssh.c \
		scripts.c \
		update.c \
		utils.c \
		plugins.c \
		pkg_old.c \
		pkg.pc

MAN=		pkg_printf.3 \
		pkg-repository.5

CLEANFILES=	${PC} ${PKGH}

NEWVERS=	${.CURDIR}/../newvers.sh

PKGVERSION!=	sh ${NEWVERS} pkg
PORTVERSION!=	sh ${NEWVERS} port

.if defined(CREATE_SNAPSHOT)
_snapshot=	snapshot

${_snapshot}:
.endif

all: ${PC}

realinstall: ${PC}
	${INSTALL} -m 644 ${PC} ${DESTDIR}${PREFIX}/libdata/pkgconfig

#
# Set DEFAULT_MIRROR_TYPE default to SRV
# Possible values are:
# 0: none
# 1: srv
# 2: http
#
DEFAULT_MIRROR_TYPE?=	1

CFLAGS+=	-DDEFAULT_MIRROR_TYPE=${DEFAULT_MIRROR_TYPE}
CFLAGS+=	-std=c99
CFLAGS+=	-I. \
		-I${.CURDIR} \
		-I${.CURDIR}/../external/sqlite \
		-I${.CURDIR}/../external/uthash \
		-I${.CURDIR}/../external/libucl/include \
		-I${.CURDIR}/../external/libyaml/include

.if defined(WITH_BUNDLED_LIBELF)
CFLAGS+=	-I${.CURDIR}/../external/libelf \
		-DBUNDLED_LIBELF
.endif

LDADD+=		-L../external/sqlite \
		-L../external/libyaml \
		-L../external/libucl

.if defined(WITH_BUNDLED_LIBELF)
LDADD+=		-L../external/libelf
DPADD+=		${.OBJDIR}/external/libelf/libelf.a
.endif

LDADD+=		-lsqlite3 \
		-larchive \
		-lsbuf \
		-lfetch \
		-lelf \
		-lutil \
		-lpthread \
		-lucl \
		-lyaml

DPADD+=		${.OBJDIR}/../external/sqlite/libsqlite3.a \
		${.OBJDIR}/../external/libyaml/libyaml.a \
		${.OBJDIR}/../external/libucl/libucl.a

.if exists(/usr/include/edit/readline/readline.h)
LDADD+=		-ledit
.endif

CFLAGS+=	-DPREFIX=\"${PREFIX}\"

.if defined(DEBUG_FLAGS)
.if ${DEBUG_FLAGS} == 1
DEBUG_FLAGS=	-ggdb -O0
.else
DEBUG_FLAGS+=	-ggdb -O0
.endif
.endif
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif
NO_MAN=		yes
NOMAN=		yes

.include <bsd.lib.mk>

.SUFFIXES: .pc.in .pc .h.in .h

.PHONY:	${_snapshot}

.h.in.h: ${NEWVERS} ${_snapshot}
	sed -e 's,%%PKGVERSION%%,$(PKGVERSION),'   \
	    -e 's,%%PORTVERSION%%,$(PORTVERSION),' \
	    ${.IMPSRC} > ${.TARGET}

.pc.in.pc: ${NEWVERS} ${_snapshot}
	sed -e "s,__PREFIX__,${PREFIX},g" \
	    -e "s,__VERSION__,${PKGVERSION},g" \
	    ${.IMPSRC} > ${.TARGET}


depend: staticdepend
.ORDER: ${DEPENDFILE} staticdepend

staticdepend:
.if defined(DPADD) && !empty(DPADD)
	echo lib${LIB}.a: ${DPADD} >> ${DEPENDFILE}
.endif
