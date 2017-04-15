AUTOMAKE_OPTIONS=	subdir-objects

GENERIC_LDADD=	$(top_builddir)/libpkg/libpkg_static.la \
		$(top_builddir)/compat/libbsd_compat.la \
		$(top_builddir)/external/libfetch_static.la \
		@LDNS_LIBS@ \
		@LIBJAIL_LIB@ \
		@OS_LIBS@ \
		-larchive \
		-lutil \
		-lm \
		-lssl \
		-lcrypto \
		-L/usr/local/lib \
		-latf-c

if HAVE_ELF_ABI
if LIBELF_BUNDLED
GENERIC_LDADD+=	$(top_builddir)/external/libelf_static.la
else
GENERIC_LDADD+=	-lelf
endif
endif

if HAVE_MACHO_ABI
GENERIC_LDADD+=	$(top_builddir)/external/libmachista_static.la \
		-lresolv
endif

PUBLIC_INCS=	-DTESTING \
		-I$(top_srcdir)/compat \
		-I$(top_srcdir)/libpkg \
		-I/usr/local/include
PRIVATE_INCS=	-I$(top_srcdir)/external/sqlite \
		-I$(top_srcdir)/external/uthash \
		-I$(top_srcdir)/external/libucl/include \
		-I$(top_srcdir)/external/libucl/klib \
		$(PUBLIC_INCS)

pkg_printf_SOURCES=	lib/pkg_printf_test.c
nodist_pkg_printf_SOURCES=	lib/pkg_printf.c
CLEANFILES=		lib/pkg_printf.c
BUILT_SOURCES=		lib/pkg_printf.c
pkg_printf_CFLAGS=	$(PRIVATE_INCS)
pkg_printf_LDADD=	$(GENERIC_LDADD)

pkg_validation_SOURCES=	lib/pkg_validation.c
pkg_validation_CFLAGS=	$(PRIVATE_INCS)
pkg_validation_LDADD=	$(GENERIC_LDADD)

plist_SOURCES=		lib/plist.c
plist_CFLAGS=		$(PRIVATE_INCS)
plist_LDADD=		$(GENERIC_LDADD)

checksum_SOURCES=	lib/checksum.c
checksum_CFLAGS=	$(PRIVATE_INCS)
checksum_LDADD=		$(GENERIC_LDADD)

deps_formula_SOURCES=	lib/deps_formula.c
deps_formula_CFLAGS=	$(PRIVATE_INCS)
deps_formula_LDADD=		$(GENERIC_LDADD)

pkg_add_dir_to_del_SOURCES=	lib/pkg_add_dir_to_del.c
pkg_add_dir_to_del_CFLAGS=	$(PRIVATE_INCS)
pkg_add_dir_to_del_LDADD=	$(GENERIC_LDADD)
merge_SOURCES=	lib/merge.c
merge_CFLAGS=	$(PRIVATE_INCS)
merge_LDADD=	$(GENERIC_LDADD)

EXTRA_DIST=	frontend/png.ucl \
		frontend/sqlite3.ucl \
		$(tests_scripts)

tests_scripts=	\
		frontend/pkg.sh \
		frontend/add.sh \
		frontend/alias.sh \
		frontend/annotate.sh \
		frontend/autoremove.sh \
		frontend/autoupgrade.sh \
		frontend/config.sh \
		frontend/configmerge.sh \
		frontend/conflicts.sh \
		frontend/conflicts-multirepo.sh \
		frontend/create.sh \
		frontend/delete.sh \
		frontend/extract.sh \
		frontend/install.sh \
		frontend/jpeg.sh \
		frontend/lock.sh \
		frontend/messages.sh \
		frontend/multipleprovider.sh \
		frontend/packagesplit.sh \
		frontend/packagemerge.sh \
		frontend/php-pr.sh \
		frontend/pubkey.sh \
		frontend/query.sh \
		frontend/register.sh \
		frontend/repo.sh \
		frontend/requires.sh \
		frontend/rootdir.sh \
		frontend/rubypuppet.sh \
		frontend/search.sh \
		frontend/set.sh \
		frontend/version.sh \
		frontend/vital.sh \
		frontend/update.sh \
		frontend/test_environment.sh \
		frontend/issue1374.sh \
		frontend/issue1425.sh \
		frontend/issue1440.sh \
		frontend/issue1445.sh \
		frontend/fingerprint.sh

check_SCRIPTS=	$(tests_scripts:.sh=)
CLEANFILES+=	$(check_SCRIPTS)

TESTS =
tests_programs=	pkg_printf \
		pkg_validation \
		plist \
		checksum \
		deps_formula \
		pkg_add_dir_to_del \
		merge
EXTRA_PROGRAMS=	$(tests_programs)
check_PROGRAMS=	$(tests_programs)

SUFFIXES= .sh

lib/pkg_printf.c: $(top_srcdir)/libpkg/pkg_printf.c
	@echo "    CP    " $@;
	@cp $(top_srcdir)/libpkg/pkg_printf.c $@

.sh:
	@echo "    CP    " $@:
	@install -m 755 $< $@
