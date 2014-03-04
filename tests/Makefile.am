AUTOMAKE_OPTIONS=	subdir-objects

check_PROGRAMS=	pkg_printf pkg_validation

pkg_printf_SOURCES=	$(top_srcdir)/libpkg/pkg_printf.c \
				lib/pkg_printf_test.c
pkg_printf_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING
pkg_printf_LDADD=		libpkg.la -latf-c
pkg_printf_LDLAGS=	-Wl,-rpath=\$$ORIGIN/../.libs
pkg_validation_SOURCES=	lib/pkg_validation.c
pkg_validation_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING
pkg_validation_LDADD=		libpkg.la -latf-c
pkg_validation_LDLAGS=	-Wl,-rpath=\$$ORIGIN/../.libs

regression-test:
	@echo "ATF/KYUA has change our testing framework is not compatible for now"
