SUBDIR=	sqlite \
	libyaml

.if defined(WITH_BUNDLED_LIBELF)
SUBDIR+= libelf
.endif

.PHONY: set-version

set-version:

.include <bsd.subdir.mk>
