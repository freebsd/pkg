PROG=	pkg2legacy
SRC=	pkg2legacy.c
BINDIR=	/usr/sbin

CFLAGS=	-I${.CURDIR}/../libpkg
LDADD=	-L../libpkg \
	-lpkgng
WARNS?=	6

.include <bsd.prog.mk>
