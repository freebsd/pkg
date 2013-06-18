
PREFIX?=	/usr/local

PERIODIC_SRCS=	400.status-pkg.in		\
		410.pkg-audit.in		\
		411.pkg-backup.in		\
		460.pkg-checksum.in		\
		490.status-pkg-changes.in

all: ${PERIODIC_SRCS:R}

clean:
	rm -f ${PERIODIC_SRCS:R}

.SUFFIXES: .in .out

.NULL:	.out

.in.out :
	sed -e 's,%%PREFIX%%,${PREFIX},' ${.IMPSRC} > ${.TARGET}

.include <bsd.prog.mk>
