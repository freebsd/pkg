LIB=	ucl
INTERNALLIB=

SRCS=	src/ucl_emitter.c \
	src/ucl_parser.c \
	src/ucl_util.c \

CFLAGS=	-I${.CURDIR}/../uthash \
	-I${.CURDIR}/include \
	-I${.CURDIR}/src \
	-fPIC

WARNS=	1
.include <bsd.lib.mk>
