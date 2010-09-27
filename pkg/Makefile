PROG=	pkg
SRCS=	main.c \
		info.c

CFLAGS+=	-I${.CURDIR}/../libpkg -I${.CURDIR}/../external/tinycdb/
LDADD+=	-L${.CURDIR}/../external -L../libpkg -lpkg
WARNS?=	6
NO_MAN= true

.include <bsd.prog.mk>
