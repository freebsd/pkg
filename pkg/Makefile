PROG=	pkg
SRCS=	main.c \
		info.c

CFLAGS+=	-I${.CURDIR}/../libpkg -I${.CURDIR}/../external
LDADD+=	-L${.CURDIR}/../external -L../libpkg -lpkg
NO_MAN= true

.include <bsd.prog.mk>
