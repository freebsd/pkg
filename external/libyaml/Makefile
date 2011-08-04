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
		-DHAVE_CONFIG_H \
		-Wno-parentheses -Wno-empty-body

WARNS=		1

NO_MAN=		true

.include <bsd.lib.mk>
