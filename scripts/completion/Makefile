.SUFFIXES:	.in

PREFIX?=	/usr/local

COMPLETION_SRCS=	_pkg.bash.in \
			_pkg.zsh.in
COMPLETION=	${COMPLETION_SRCS:R}
CLEANFILES=	${COMPLETION}

.in:
	sed -e 's,__PREFIX__,${PREFIX},g' \
		${.IMPSRC} > ${.TARGET}

all: ${COMPLETION}

install: all
	install -d ${DESTDIR}${PREFIX}/share/zsh/site-functions/
	install -d ${DESTDIR}${PREFIX}/etc/bash_completion.d/
	install -m 444 _pkg.bash ${DESTDIR}${PREFIX}/etc/bash_completion.d/
	install -m 444 _pkg.zsh ${DESTDIR}${PREFIX}/share/zsh/site-functions/_pkg

.include <bsd.prog.mk>
