PROG=	test
SRCS=	test.c		\
	manifest.c	\
	pkg.c

CFLAGS+=-I.			\
	-I/usr/local/include	\
	-I../libpkg
LDADD+=	-L/usr/local/lib	\
	-latf-c			\
	-L../libpkg		\
	-lpkg
NO_MAN=	true

TESTS=	manifest \
	pkg

run: ${PROG}
.for test in ${TESTS}
	@env LD_LIBRARY_PATH=${.CURDIR}/../../libpkg ./${PROG} ${test}
.endfor

.include "../bsd.tests.mk"
