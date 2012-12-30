PROG=	pkg_compat

SRCS=	pkg_compat.c register.c create.c

CFLAGS=	-I${.CURDIR}/../libpkg \
	-I${.CURDIR}/../pkg \
	-DPKG_COMPAT=yes

.PATH:	${.CURDIR}/../pkg

LDFLAGS=	-L${.OBJDIR}/../libpkg -lpkg

NO_MAN=	yes
WARNS=	6

.include <bsd.prog.mk>
