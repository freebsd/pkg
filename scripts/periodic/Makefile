.SUFFIXES:	.in

PREFIX?=	/usr/local

SCRIPTS_DAILY=	411.pkg-backup.in \
		490.status-pkg-changes.in
SCRIPTS_WEEKLY=	400.status-pkg.in
SCRIPTS_SECURITY=	410.pkg-audit.in \
			460.pkg-checksum.in
SCRIPTS=	${SCRIPTS_DAILY:R} ${SCRIPTS_WEEKLY:R} ${SCRIPTS_SECURITY:R}
CLEANFILES=	${SCRIPTS}

.in:
	sed -e 's,__PREFIX__,${PREFIX},g' \
		${.IMPSRC} > ${.TARGET}

all: ${SCRIPTS}

# tell bmake we use the old :L :U modifiers
.MAKE.FreeBSD_UL= yes

install: all
.for period in daily weekly security

	install -d ${DESTDIR}${PREFIX}/etc/periodic/${period}
.  for periodic in ${SCRIPTS_${period:U}:R}
	install -m 555 ${periodic} ${DESTDIR}${PREFIX}/etc/periodic/${period}
.  endfor
.endfor

.include <bsd.prog.mk>
