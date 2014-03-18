AUTOMAKE_OPTIONS=	subdir-objects

pkg_printf_SOURCES=	lib/pkg_printf_test.c
pkg_printf_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING
pkg_printf_LDADD=	$(top_builddir)/libpkg/libpkg.la -latf-c
pkg_printf_LDFLAGS=	-Wl,-rpath=\$$ORIGIN/../.libs
pkg_validation_SOURCES=	lib/pkg_validation.c
pkg_validation_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING
pkg_validation_LDADD=	$(top_builddir)/libpkg/libpkg.la -latf-c
pkg_validation_LDFLAGS=	-Wl,-rpath=\$$ORIGIN/../.libs

tests_programs=	pkg_printf pkg_validation
EXTRA_PROGRAMS=	$(tests_programs)
check_PROGRAMS=	@TESTS@

regression-test:
	@echo "ATF/KYUA has change our testing framework is not compatible for now"
