SUBDIR=	external \
	libpkg \
	pkg

.if !defined(NOSTATIC)
SUBDIR+=	pkg-static
.endif

.include <bsd.subdir.mk>
