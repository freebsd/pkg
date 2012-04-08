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
		shell.c

PREFIX?=	/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man

.if exists(../.git) && exists(/usr/local/bin/git)
GITHASH!=	git rev-parse HEAD
CFLAGS+=	-DGITHASH="\" ${GITHASH}\""
.endif

DEBUG_FLAGS+=	-g -O0
.if defined(PROFILE_BUILD)
DEBUG_FLAGS+=	-pg
.endif

CFLAGS+=	-I${.CURDIR}/../libpkg
LDADD+=		-L${.OBJDIR}/../libpkg \
		-lpkg \
		-lutil \
		-ljail \
		${LDADD_STATIC}

WARNS?=		6
MAN=	pkg.8 \
	pkg-add.8 \
	pkg-autoremove.8 \
	pkg-backup.8 \
	pkg-create.8 \
	pkg-delete.8 \
	pkg-info.8 \
	pkg-install.8 \
	pkg-query.8 \
	pkg-register.8 \
	pkg-repo.8 \
	pkg-search.8 \
	pkg-set.8 \
	pkg-shlib.8 \
	pkg-update.8 \
	pkg-updating.8 \
	pkg-upgrade.8 \
	pkg-version.8 \
	pkg-which.8 \
	pkg.conf.5

.include <bsd.prog.mk>
