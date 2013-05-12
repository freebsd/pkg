TESTS=	test

SRCS=		tests.h
test_SRCS=	manifest.c	\
		pkg.c

CFLAGS+=	-I../../libpkg
LDADD+=		-Wl,-rpath ${.CURDIR}/../../libpkg \
		-L../../libpkg	\
		-lpkg

.include "../bsd.tests.mk"
