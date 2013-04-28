TESTS=	test

SRCS=		tests.h
test_SRCS=	manifest.c	\
		pkg.c

CFLAGS+=	-I../../libpkg
LDADD+=		-L../../libpkg	\
		-lpkg

.include "../bsd.tests.mk"
