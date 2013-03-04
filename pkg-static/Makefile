PROG=pkg-static

STATIC_PKGNG=	yes
NO_SHARED?=	yes
NO_MAN=		yes
NOMAN=		yes
LDADD_STATIC=	-L${.OBJDIR}/../external/sqlite

.if !exists(/usr/include/bsdyml.h)
LDADD_STATIC+=	-L${.OBJDIR}/../external/libyaml \
		-lyaml
.else
LDADD_STATIC+=	-lbsdyml
.endif

.if defined(WITH_BUNDLED_LIBELF)
LDADD_STATIC+=	-L${.OBJDIR}/../external/libelf
.endif

LDADD_STATIC+=	-lsqlite3 \
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
