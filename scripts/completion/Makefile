.SUFFIXES:	.in

PREFIX?=	/usr/local

COMPLETION_SRCS=	_pkg.bash.in \
			_pkg.zsh.in
COMPLETION=	${COMPLETION_SRCS:.in=}

.in:
	sed -e 's,__PREFIX__,${PREFIX},g' \
		${.IMPSRC} > ${.TARGET}

all: ${COMPLETION}

clean:
	rm -f ${COMPLETION}

install: all
	install -d ${DESTDIR}${PREFIX}/share/zsh/site-functions/
	install -d ${DESTDIR}${PREFIX}/etc/bash_completion.d/
	install -m 444 ${.OBJDIR}/_pkg.bash ${DESTDIR}${PREFIX}/etc/bash_completion.d/
	install -m 444 ${.OBJDIR}/_pkg.zsh ${DESTDIR}${PREFIX}/share/zsh/site-functions/_pkg

.include <bsd.own.mk>
