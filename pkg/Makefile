PROG?=		pkg
SRCS=		add.c \
		audit.c \
		autoremove.c \
		backup.c \
		check.c \
		clean.c \
		create.c \
		delete.c \
		event.c \
		info.c \
		install.c \
		main.c \
		progressmeter.c \
		query.c \
		register.c \
		repo.c \
		rquery.c \
		update.c \
		upgrade.c \
		search.c \
		set.c \
		shlib.c \
		updating.c \
		utils.c \
		version.c \
		which.c \
		fetch.c \
		shell.c \
		stats.c

PREFIX?=	/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man

.if exists(${.CURDIR}/../.git) && exists(/usr/local/bin/git)
GITHASH!=	cd ${.CURDIR} && git rev-parse HEAD
CFLAGS+=	-DGITHASH="\" ${GITHASH}\""
.endif

.if defined(DEBUG_FLAGS)
DEBUG_FLAGS+=	-g -O0
.endif
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif

CFLAGS+=	-I${.CURDIR}/../libpkg
LDADD+=		-L${.OBJDIR}/../libpkg \
		-lpkg \
		-larchive \
		-lutil \
		-lpthread \
		${LDADD_STATIC}

.if !exists(/usr/include/jail.h)
CFLAGS+=	-DNO_LIBJAIL
.else
LDADD+=		-ljail
.endif

.if !defined(STATIC_PKGNG)
LDFLAGS+=	-Wl,-rpath=/usr/lib:${PREFIX}/lib
.endif

WARNS?=		6
MAN=	pkg.8 \
	pkg-add.8 \
	pkg-audit.8 \
	pkg-autoremove.8 \
	pkg-backup.8 \
	pkg-check.8 \
	pkg-clean.8 \
	pkg-create.8 \
	pkg-delete.8 \
	pkg-fetch.8 \
	pkg-info.8 \
	pkg-install.8 \
	pkg-query.8 \
	pkg-register.8 \
	pkg-repo.8 \
	pkg-rquery.8 \
	pkg-search.8 \
	pkg-set.8 \
	pkg-shell.8 \
	pkg-shlib.8 \
	pkg-stats.8 \
	pkg-update.8 \
	pkg-updating.8 \
	pkg-upgrade.8 \
	pkg-version.8 \
	pkg-which.8 \
	pkg.conf.5

MLINKS=	pkg-delete.8 pkg-remove.8 \
	pkg.8 pkg-static.8

.PHONY: fix-xrefs set-version

fix-xrefs:
	@./fix-xrefs ${MAN}

set-version:

.include <bsd.prog.mk>
