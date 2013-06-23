.SUFFIXES:	.in

PREFIX?=	/usr/local

SRCS=	pkg2ng.in
SCRIPTS=	${SRCS:R}

.in:
	sed -e 's,__PREFIX__,${PREFIX},g' \
		${.IMPSRC} > ${.TARGET}

all: ${SCRIPTS}

clean:
	rm -f ${.OBJDIR}/${SCRIPTS}

install: all
	install -m 555 ${.OBJDIR}/pkg2ng ${DESTDIR}${PREFIX}/sbin/

.include <bsd.own.mk>
