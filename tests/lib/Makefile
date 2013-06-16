TESTS=	test pkg_printf_test pkg_validation

SRCS=		tests.h
test_SRCS=	manifest.c	\
		pkg.c

pkg_printf_test_SRCS=	pkg_printf.c

pkg_validation_SRC=	pkg_validation

CFLAGS+=	-DTESTING -I../../libpkg
LDADD+=		-Wl,-rpath ${.CURDIR}/../../libpkg \
		-L../../libpkg	\
		-lpkg

CLEANFILES+=	pkg_printf.c

pkg_printf.c:	../../libpkg/pkg_printf.c
	ln -sf ${.OODATE} ${.TARGET}

.include "../bsd.tests.mk"
