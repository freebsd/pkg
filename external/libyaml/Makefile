LIB=	yaml
INTERNALLIB=
.PATH: ${.CURDIR}/src
SRCS=	api.c      \
	dumper.c   \
	emitter.c  \
	loader.c   \
	parser.c   \
	reader.c   \
	scanner.c  \
	writer.c

CFLAGS+=	-fPIC \
		-I${.CURDIR}/include \
		-I${.CURDIR} \
		-DHAVE_CONFIG_H

WARNS=		1

NO_MAN=		true

.if defined(DEBUG_FLAGS)
.if ${DEBUG_FLAGS} == 1
DEBUG_FLAGS=	-ggdb -O0
.else
DEBUG_FLAGS+=	-ggdb -O0
.endif
.endif

.include <bsd.lib.mk>
