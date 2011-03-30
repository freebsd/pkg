PROG=		pkg
SRCS=		add.c \
		create.c \
		delete.c \
		info.c \
		main.c \
		register.c \
		repo.c \
		version.c \
		which.c
BINDIR=		/usr/sbin

CFLAGS+=	-I${.CURDIR}/../libpkg
LDADD+=		-L../libpkg \
		-lpkgng \
		-lutil
WARNS?=		6
MAN=	pkg.1

.include <bsd.prog.mk>
