LIB=	expat
INTERNALLIB=
.PATH: ${.CURDIR}/lib
SRCS=	xmlparse.c xmlrole.c xmltok.c

CFLAGS=	-I${.CURDIR} -DHAVE_EXPAT_CONFIG_H
WARNS=	2
NO_MAN=	true

.include <bsd.lib.mk>
