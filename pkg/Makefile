PROG=		pkg
SRCS=		add.c \
		autoremove.c \
		create.c \
		delete.c \
		info.c \
		main.c \
		register.c \
		repo.c \
		update.c \
		upgrade.c \
		version.c \
		which.c
BINDIR=		/usr/sbin

DEBUG_FLAGS+=	-g -O0
CFLAGS+=	-I${.CURDIR}/../libpkg
LDADD+=		-L../libpkg \
		-lpkgng \
		-lutil
WARNS?=		6
MAN=	pkg.1 \
	pkg-add.1 \
	pkg-create.1 \
	pkg-delete.1 \
	pkg-info.1 \
	pkg-register.1 \
	pkg-repo.1 \
	pkg-update.1 \
	pkg-upgrade.1 \
	pkg-version.1 \
	pkg-which.1

.include <bsd.prog.mk>
