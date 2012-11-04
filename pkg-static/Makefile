PROG=pkg-static

STATIC_PKGNG=	yes
NO_SHARED?=	yes
NO_MAN=		yes
NOMAN=		yes
LDADD_STATIC=	-L${.OBJDIR}/../external/sqlite \
		-L${.OBJDIR}/../external/libyaml \
		-lsqlite3 \
		-lyaml \
		-larchive \
		-lsbuf \
		-lfetch \
		-lpthread \
		-lelf \
		-lssl \
		-lcrypto \
		-lmd \
		-lz \
		-lbz2 \
		-llzma

.if exists(/usr/include/edit/readline/readline.h)
LDADD_STATIC+=	-ledit \
		-lncursesw
.endif

.PATH:		${.CURDIR}/../pkg/

.include "${.CURDIR}/../pkg/Makefile"
