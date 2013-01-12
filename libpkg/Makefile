.include <bsd.own.mk>

LIB=		pkg
INCS=		pkg.h
WARNS=		6
PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
INCLUDEDIR=	${PREFIX}/include
SHLIB_MAJOR=	0

#gr_utils.c has to be deleted as soon as it goes in base
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
		pkg_repo.c \
		pkg_status.c \
		pkg_version.c \
		pkgdb.c \
		rcscripts.c \
		rsa.c \
		scripts.c \
		update.c \
		usergroup.c \
		utils.c \
		plugins.c \
		pkg_old.c \
		pkg.pc

CLEANFILES=	${PC} ${PKGH}

NEWVERS=	../newvers.sh

PKGVERSION!=	sh ${NEWVERS} pkg
PORTVERSION!=	sh ${NEWVERS} port

.if defined(CREATE_SNAPSHOT)
_snapshot=	snapshot

${_snapshot}:
.endif

all: ${PC}

realinstall: ${PC}
	${INSTALL} -m 644 ${PC} ${PREFIX}/libdata/pkgconfig

HAVE_GRUTILS!=	grep -q gr_copy /usr/include/libutil.h && echo yes || echo no

.if ${HAVE_GRUTILS} != yes
SRCS+=	gr_util.c
.else
CFLAGS+=	-DHAVE_GRUTILS
.endif


#
# Set DEFAULT_MIRROR_TYPE default to SRV
# Possible values are:
# 0: none
# 1: srv
# 2: http
#
DEFAULT_MIRROR_TYPE?=	0

CFLAGS+=	-DDEFAULT_MIRROR_TYPE=${DEFAULT_MIRROR_TYPE}
CFLAGS+=	-std=c99
CFLAGS+=	-I${.CURDIR} \
		-I${.CURDIR}/../external/sqlite \
		-I${.CURDIR}/../external/libyaml/include \
		-I${.CURDIR}/../external/uthash

.if defined(WITH_BUNDLED_LIBELF)
CFLAGS+=	-I${.CURDIR}/../external/libelf \
		-DBUNDLED_LIBELF
.endif

LDADD+=		-L${.OBJDIR}/../external/sqlite \
		-L${.OBJDIR}/../external/libyaml

.if defined(WITH_BUNDLED_LIBELF)
LDADD+=		-L${.OBJDIR}/../external/libelf
.endif

LDADD+=		-lsqlite3 \
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
