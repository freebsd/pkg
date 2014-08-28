AUTOMAKE_OPTIONS=	subdir-objects

GENERIC_LDADD=	$(top_builddir)/libpkg/libpkg_static.la \
		$(top_builddir)/external/libsbuf_static.la \
		@LIBJAIL_LIB@ \
		@LIBEXECINFO_LIB@ \
		@LIBELF_LIB@ \
		@LDNS_LIBS@ \
		-lfetch \
		-larchive \
		-lutil \
		-lm \
		-latf-c
PUBLIC_INCS=	-I$(top_srcdir)/libpkg -DTESTING
PRIVATE_INCS=	$(PUBLIC_INCS) \
		-I$(top_srcdir)/external/sqlite \
		-I$(top_srcdir)/external/uthash \
		-I$(top_srcdir)/external/libucl/include

pkg_printf_SOURCES=	lib/pkg_printf_test.c \
			lib/pkg_printf.c
CLEANFILES=		lib/pkg_printf.c
BUILT_SOURCES=		lib/pkg_printf.c
pkg_printf_CFLAGS=	$(PRIVATE_INCS)
pkg_printf_LDADD=	$(GENERIC_LDADD)

pkg_validation_SOURCES=	lib/pkg_validation.c
pkg_validation_CFLAGS=	$(PUBLIC_INCS)
pkg_validation_LDADD=	$(GENERIC_LDADD)

plist_SOURCES=		lib/plist.c
plist_CFLAGS=		$(PRIVATE_INCS)
plist_LDADD=		$(GENERIC_LDADD)

tests_programs=	pkg_printf pkg_validation plist
EXTRA_PROGRAMS=	$(tests_programs)
check_PROGRAMS=	@TESTS@

check-local: $(tests_programs)

lib/pkg_printf.c:
	@echo "    CP    " $@;
	@cp $(top_srcdir)/libpkg/pkg_printf.c $@
