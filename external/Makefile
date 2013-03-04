SUBDIR=	sqlite

.if !exists(/usr/include/bsdyml.h)
SUBDIR+= libyaml
.endif

.if defined(WITH_BUNDLED_LIBELF)
SUBDIR+= libelf
.endif

.PHONY: set-version

set-version:

.include <bsd.subdir.mk>
