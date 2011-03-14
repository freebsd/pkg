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

CFLAGS+=	-I${.CURDIR}/../libpkg
LDADD+=		-L../libpkg \
		-lpkg \
		-lutil
WARNS?=		6
NO_MAN=		true

.include <bsd.prog.mk>
