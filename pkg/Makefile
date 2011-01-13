PROG=	pkg
SRCS=	main.c \
	info.c \
	which.c \
	register.c \
	version.c \
	add.c

CFLAGS+=	-I${.CURDIR}/../libpkg -I${.CURDIR}/../external/tinycdb/
LDADD+=	-L${.CURDIR}/../external -L../libpkg -lpkg -lutil
WARNS?=	6
NO_MAN= true

.include <bsd.prog.mk>
