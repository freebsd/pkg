PROG?=		pkg
SRCS=		add.c \
		annotate.c \
		audit.c \
		autoremove.c \
		backup.c \
		check.c \
		clean.c \
		config.c \
		convert.c \
		create.c \
		delete.c \
		event.c \
		info.c \
		install.c \
		lock.c \
		main.c \
		plugins.c \
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
		stats.c \
		ssh.c

PREFIX?=	/usr/local
BINDIR=		${PREFIX}/sbin
MANDIR=		${PREFIX}/man/man

.if exists(${.CURDIR}/../.git) && exists(/usr/local/bin/git)
GITHASH!=	cd ${.CURDIR} && git rev-parse HEAD
CFLAGS+=	-DGITHASH="\" ${GITHASH}\""
.endif

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

CFLAGS+=	-I../libpkg \
		-I${.CURDIR}/../external/uthash \
		-I${.CURDIR}/../external/expat/lib
LDADD+=		-L../libpkg \
		-L../external/expat \
		-ledit \
		-lpkg \
		-larchive \
		-lutil \
		-lpthread \
		-lsbuf \
		-lfetch \
		-lssl \
		-lcrypto \
		-lmd \
		-lz \
		-lbz2 \
		-llzma \
		-lexpat

LDADD_STATIC=	-L../external/sqlite \
		-L../external/libyaml \
		-L../external/libucl

LDADD_STATIC+=	-lsqlite3 \
		-larchive \
		-lsbuf \
		-lfetch \
		-lpthread \
		-lelf \
		-lssl \
		-lcrypto \
		-lmd \
		-lz \
		-lbz2 \
		-llzma \
		-lyaml \
		-lucl

.if !exists(/usr/include/jail.h)
CFLAGS+=	-DNO_LIBJAIL
.else
LDADD+=		-ljail
.endif

.if exists(/usr/include/edit/readline/readline.h)
LDADD_STATIC+=	-ledit \
		-lncursesw
.endif

DPADD_STATIC=	${.OBJDIR}/../libpkg/libpkg.a

WARNS?=		6
MAN=	pkg.8 \
	pkg-add.8 \
	pkg-annotate.8 \
	pkg-audit.8 \
	pkg-autoremove.8 \
	pkg-backup.8 \
	pkg-check.8 \
	pkg-convert.8 \
	pkg-clean.8 \
	pkg-create.8 \
	pkg-delete.8 \
	pkg-fetch.8 \
	pkg-info.8 \
	pkg-install.8 \
	pkg-lock.8 \
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
	pkg-lock.8 pkg-unlock.8 \
	pkg.8 pkg-static.8

.PHONY: fix-xrefs

fix-xrefs:
	@./fix-xrefs ${MAN}

.include <bsd.prog.mk>

${PROG}: ${PROG}-static

LDFLAGS_STATIC:=	-static ${LDFLAGS}
LDFLAGS+=	-Wl,-rpath=/usr/lib:${PREFIX}/lib

${PROG}-static: ${OBJS}
	${CC} -static ${CFLAGS} ${LDFLAGS_STATIC} -o ${.TARGET} ${OBJS} ${LDADD} ${LDADD_STATIC}

realinstall: ${PROG}-static-install

${PROG}-static-install:
	${INSTALL} ${STRIP} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${_INSTALLFLAGS} ${PROG}-static ${DESTDIR}${BINDIR}/${PROG}-static

depend: staticdepend
.ORDER: ${DEPENDFILE} staticdepend

staticdepend:
.if defined(DPADD_STATIC) && !empty(DPADD_STATIC)
	echo ${PROG}-static: ${DPADD_STATIC} >> ${DEPENDFILE}
.endif
