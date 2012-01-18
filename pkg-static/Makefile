PROG=pkg-static

STATIC_PKGNG=	yes
NO_SHARED?=	yes
NO_MAN=		yes
LDADD_STATIC=	-L${.CURDIR}/../external/sqlite \
		-L${.CURDIR}/../external/libyaml \
		-lsqlite3 \
		-lyaml \
		-larchive \
		-lsbuf \
		-lfetch \
		-lelf \
		-lssl \
		-lcrypto \
		-lmd \
		-lz \
		-lbz2 \
		-llzma

.PATH:		${.CURDIR}/../pkg/

.include "${.CURDIR}/../pkg/Makefile"
