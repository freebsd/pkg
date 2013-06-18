.SUFFIXES:	.in

PREFIX?=	/usr/local

SCRIPTS_DAILY=	400.status-pkg.in \
		411.pkg-backup.in \
		490.status-pkg-changes.in
SCRIPTS_SECURITY=	410.pkg-audit.in \
			460.pkg-checksum.in
SCRIPTS=	${SCRIPTS_DAILY::R} ${SCRIPTS_SECURITY:R}

.in:
	sed -e 's,__PREFIX__,${PREFIX},g' \
		${.IMPSRC} > ${.TARGET}

all: ${SCRIPTS}

clean:
	rm -f ${.OBJDIR}/${SCRIPTS}

install: all
	install -d ${DESTDIR}${PREFIX}/etc/periodic/security
	install -d ${DESTDIR}${PREFIX}/etc/periodic/daily
.for periodic in ${SCRIPTS_DAILY:.in=}
	install -m 555 ${.OBJDIR}/${periodic} ${DESTDIR}${PREFIX}/etc/periodic/daily
.endfor
.for periodic in ${SCRIPTS_SECURITY:R}
	install -m 555 ${.OBJDIR}/${periodic} ${DESTDIR}${PREFIX}/etc/periodic/security
.endfor

.include <bsd.own.mk>
