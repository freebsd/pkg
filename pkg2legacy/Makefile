PROG=	pkg2legacy
SRC=	pkg2legacy.c
BINDIR=	/usr/sbin

CFLAGS=	-I${.CURDIR}/../libpkg
LDADD=	-L../libpkg \
	-lpkg
WARNS?=	6
NO_MAN=

.include <bsd.prog.mk>
