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
		-lssl \
		-lcrypto \
		-L/usr/local/lib \
		-latf-c
PUBLIC_INCS=	-I$(top_srcdir)/libpkg -DTESTING \
		-I/usr/local/include
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

pkg_add_dir_to_del_SOURCES=	lib/pkg_add_dir_to_del.c
pkg_add_dir_to_del_CFLAGS=	$(PRIVATE_INCS)
pkg_add_dir_to_del_LDADD=	$(GENERIC_LDADD)

EXTRA_DIST=	frontend/png.uclin \
		frontend/sqlite3.uclin \
		Kyuafile \
		frontend/Kyuafile \
		frontend/annotate.shin \
		frontend/pkg.shin \
		frontend/search.shin \
		frontend/version.shin \
		frontend/test_environment.shin \
		frontend/register.shin

TESTS =
tests_programs=	pkg_printf \
		pkg_validation \
		plist \
		pkg_add_dir_to_del
EXTRA_PROGRAMS=	$(tests_programs)
check_PROGRAMS=	$(tests_programs)
check_SCRIPTS=	frontend/pkg.sh \
		frontend/annotate.sh \
		frontend/search.sh \
		frontend/version.sh \
		frontend/register.sh \
		frontend/test_environment.sh \
		frontend/png.ucl \
		frontend/sqlite3.ucl
CLEANFILES+=	${check_SCRIPTS}
SUFFIXES = .sh .ucl

.shin.sh:
	@echo "    GEN   " $@;
	@install -m 755 $< $@

.uclin.ucl:
	@echo "    GEN   " $@;
	@install -m 644 $< $@

lib/pkg_printf.c:
	@echo "    CP    " $@;
	@cp $(top_srcdir)/libpkg/pkg_printf.c $@
