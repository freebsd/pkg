PROG=	pkg
SRCS=	main.c \
	info.c \
	create.c \
	which.c

CFLAGS+=	-I${.CURDIR}/../libpkg -I${.CURDIR}/../external/tinycdb/
LDADD+=	-L${.CURDIR}/../external -L../libpkg -lpkg -lutil
WARNS?=	6
NO_MAN= true

.include <bsd.prog.mk>
