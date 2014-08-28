AUTOMAKE_OPTIONS=	subdir-objects

pkg_printf_SOURCES=	lib/pkg_printf_test.c \
			lib/pkg_printf.c

CLEANFILES=		lib/pkg_printf.c
BUILT_SOURCES=		lib/pkg_printf.c

pkg_printf_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING \
			-I$(top_srcdir)/external/sqlite \
			-I$(top_srcdir)/external/uthash \
			-I$(top_srcdir)/external/libucl/include
pkg_printf_LDADD= $(top_builddir)/libpkg/libpkg_static.la \
			$(top_builddir)/external/libsbuf_static.la \
			$(pkg_OBJECTS) \
			@LIBJAIL_LIB@ \
			@LIBEXECINFO_LIB@ \
			@LIBELF_LIB@ \
			@LDNS_LIBS@ \
			-lfetch \
			-larchive \
			-lutil \
			-lm \
			-latf-c
pkg_validation_SOURCES=	lib/pkg_validation.c
pkg_validation_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING
pkg_validation_LDADD= $(top_builddir)/libpkg/libpkg_static.la \
			$(top_builddir)/external/libsbuf_static.la \
			$(pkg_OBJECTS) \
			@LIBJAIL_LIB@ \
			@LIBEXECINFO_LIB@ \
			@LIBELF_LIB@ \
			@LDNS_LIBS@ \
			-lfetch \
			-larchive \
			-lutil \
			-lm \
			-latf-c

tests_programs=	pkg_printf pkg_validation
EXTRA_PROGRAMS=	$(tests_programs)
check_PROGRAMS=	@TESTS@

check-local: $(tests_programs)

lib/pkg_printf.c:
	@echo "    CP    " $@;
	@cp $(top_srcdir)/libpkg/pkg_printf.c $@
