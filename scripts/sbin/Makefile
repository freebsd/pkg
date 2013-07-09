.SUFFIXES:	.in

PREFIX?=	/usr/local

SCRIPTS=	pkg2ng
CLEANFILES=	${SCRIPTS}
BINDIR=		${PREFIX}/sbin

.in:
	sed -e 's,__PREFIX__,${PREFIX},g' \
		${.IMPSRC} > ${.TARGET}

.include <bsd.prog.mk>
