SUBDIR=	lib \
	frontend

.include <bsd.subdir.mk>

run:
.for dir in ${SUBDIR}
	cd ${.CURDIR}/${dir} && ${MAKE} run
.endfor
