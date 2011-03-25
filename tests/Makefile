PROG=	test
SRCS=	test.c		\
	manifest.c	\
	pkg.c		\

CFLAGS+=-I.			\
	-I/usr/local/include	\
	-I../libpkg
LDADD+=	-L/usr/local/lib	\
	-lcheck			\
	-L../libpkg		\
	-lpkgng
NO_MAN=	true

run: ${PROG}
	@env LD_LIBRARY_PATH=../libpkg ./${PROG}

.include <bsd.prog.mk>
