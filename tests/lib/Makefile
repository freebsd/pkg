TESTS=	test pp_test

SRCS=		tests.h
test_SRCS=	manifest.c	\
		pkg.c

pp_test_SRCS=	pkg_printf.c

CFLAGS+=	-DTESTING -I../../libpkg
LDADD+=		-Wl,-rpath ${.CURDIR}/../../libpkg \
		-L../../libpkg	\
		-lpkg

CLEANFILES+=	pkg_printf.c

pkg_printf.c:	../../libpkg/pkg_printf.c
	ln -s ${.OODATE} ${.TARGET}

.include "../bsd.tests.mk"
