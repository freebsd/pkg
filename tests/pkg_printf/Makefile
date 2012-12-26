PROG=	test
SRCS=	test.c		\
	pkg_printf.c

TOPDIR=	${.CURDIR}/../..

CLEANFILES=	pkg_printf.c

.if defined(STATIC_TESTS)
NO_SHARED=	yes
.else
LDFLAGS+=	-Wl,-rpath=${TOPDIR}/libpkg
.endif

CFLAGS+=-DTESTING		\
	-I${.CURDIR}		\
	-I${TOPDIR}/libpkg	\
	-I/usr/local/include

CFLAGS+=-I${TOPDIR}/external/sqlite		\
	-I${TOPDIR}/external/libyaml/include	\
	-I${TOPDIR}/external/uthash

.if defined(WITH_BUNDLED_LIBELF)
CFLAGS+=-I${TOPDIR}/external/libelf \
	-DBUNDLED_LIBELF
.endif

LDADD+=	-L${TOPDIR}/libpkg		\
	-lpkg

.if defined(STATIC_TESTS)
LDADD+=	-L${TOPDIR}/external/sqlite	\
	-lsqlite3			\
	-L${TOPDIR}/external/libyaml	\
	-lyaml

.  if defined(WITH_BUNDLED_LIBELF)
LDADD+=	-L${TOPDIR}/external/libelf
.  endif
.endif

LDADD+=	-L/usr/local/lib	\
	-latf-c

.if defined(STATIC_TESTS)
LDADD+=	-larchive	\
	-lsbuf		\
	-lfetch		\
	-lelf		\
	-lutil		\
	-lpthread	\
	-lssl		\
	-lcrypto	\
	-lmd		\
	-lz		\
	-lbz2		\
	-llzma

.  if exists(/usr/include/edit/readline/readline.h)
LDADD+=	-ledit		\
	-lncursesw
.  endif
.endif

NO_MAN=	true

TESTS=	human_number

pkg_printf.c:	${TOPDIR}/libpkg/pkg_printf.c
	ln -s ${.OODATE} ${.TARGET}

run: ${PROG}
	atf-run

.include <bsd.prog.mk>
