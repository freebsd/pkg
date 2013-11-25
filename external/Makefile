SUBDIR=	sqlite libyaml expat libucl

.if !exists(/usr/lib/libelf.so)
SUBDIR+= libelf
.endif

.include <bsd.subdir.mk>
