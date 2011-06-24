LIB=	yaml
INTERALLIB=
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

.include <bsd.lib.mk>
