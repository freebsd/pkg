AUTOMAKE_OPTIONS=	subdir-objects
CLEANFILES=		external/libelf/libelf_fsize.c \
			external/libelf/libelf_msize.c \
			external/libelf/libelf_convert.c

pdailydir=		${sysconfdir}/periodic/daily
psecuritydir=		${sysconfdir}/periodic/security
pweeklydir=		${sysconfdir}/periodic/weekly
bashcompdir=		${sysconfdir}/bash_completion.d
zshcompdir=		${prefix}/share/zsh/site-functions

EXTRA_LTLIBRARIES=	libelf.la

noinst_HEADERS=		libpkg/private/db_upgrades.h \
			libpkg/private/elf_tables.h \
			libpkg/private/event.h \
			libpkg/private/ldconfig.h \
			libpkg/private/pkg.h \
			libpkg/private/pkg_printf.h \
			libpkg/private/pkgdb.h \
			libpkg/private/repodb.h \
			libpkg/private/thd_repo.h \
			libpkg/private/utils.h \
			external/expat/amiga/expat_68k.h \
			external/expat/amiga/expat_base.h \
			external/expat/amiga/include/inline4/expat.h \
			external/expat/amiga/include/interfaces/expat.h \
			external/expat/amiga/include/libraries/expat.h \
			external/expat/amiga/include/proto/expat.h \
			external/expat/expat_config.h \
			external/expat/lib/amigaconfig.h \
			external/expat/lib/ascii.h \
			external/expat/lib/asciitab.h \
			external/expat/lib/expat.h \
			external/expat/lib/expat_external.h \
			external/expat/lib/iasciitab.h \
			external/expat/lib/internal.h \
			external/expat/lib/latin1tab.h \
			external/expat/lib/macconfig.h \
			external/expat/lib/nametab.h \
			external/expat/lib/utf8tab.h \
			external/expat/lib/winconfig.h \
			external/expat/lib/xmlrole.h \
			external/expat/lib/xmltok.h \
			external/expat/lib/xmltok_impl.h \
			external/expat/tests/chardata.h \
			external/expat/tests/minicheck.h \
			external/expat/vms/expat_config.h \
			external/expat/xmlwf/codepage.h \
			external/expat/xmlwf/filemap.h \
			external/expat/xmlwf/xmlfile.h \
			external/expat/xmlwf/xmlmime.h \
			external/expat/xmlwf/xmltchar.h \
			external/expat/xmlwf/xmlurl.h \
			external/libelf/_elftc.h \
			external/libelf/_libelf.h \
			external/libelf/_libelf_ar.h \
			external/libelf/_libelf_config.h \
			external/libelf/elfdefinitions.h \
			external/libelf/gelf.h \
			external/libelf/libelf.h \
			external/libucl/include/ucl.h \
			external/libucl/src/xxhash.h \
			external/libucl/src/ucl_internal.h \
			external/libucl/src/ucl_hash.h \
			external/libucl/src/ucl_chartable.h \
			external/libucl/uthash/uthash.h \
			external/libucl/uthash/utlist.h \
			external/libucl/uthash/utstring.h \
			external/libyaml/config.h \
			external/libyaml/include/yaml.h \
			external/libyaml/src/yaml_private.h \
			external/libyaml/win32/config.h \
			external/sqlite/sqlite3.h \
			external/uthash/uthash.h \
			external/uthash/utlist.h
noinst_LTLIBRARIES=	libucl.la libsqlite.la libyaml.la libexpat.la @LIBELF_BUNDLED@
lib_LTLIBRARIES=	libpkg.la
sbin_PROGRAMS=		pkg pkg-static
dist_sbin_SCRIPTS=	scripts/sbin/pkg2ng
dist_sysconf_DATA=	src/pkg.conf.sample
dist_man_MANS=		libpkg/pkg-repository.5 \
			libpkg/pkg_printf.3 \
			libpkg/pkg_repos.3 \
			src/pkg-add.8 \
			src/pkg-annotate.8 \
			src/pkg-audit.8 \
			src/pkg-autoremove.8 \
			src/pkg-backup.8 \
			src/pkg-check.8 \
			src/pkg-clean.8 \
			src/pkg-config.8 \
			src/pkg-convert.8 \
			src/pkg-create.8 \
			src/pkg-delete.8 \
			src/pkg-fetch.8 \
			src/pkg-info.8 \
			src/pkg-install.8 \
			src/pkg-lock.8 \
			src/pkg-query.8 \
			src/pkg-register.8 \
			src/pkg-repo.8 \
			src/pkg-rquery.8 \
			src/pkg-search.8 \
			src/pkg-set.8 \
			src/pkg-shell.8 \
			src/pkg-shlib.8 \
			src/pkg-stats.8 \
			src/pkg-update.8 \
			src/pkg-updating.8 \
			src/pkg-upgrade.8 \
			src/pkg-version.8 \
			src/pkg-which.8 \
			src/pkg.8 \
			src/pkg.conf.5

dist_pdaily_DATA=	scripts/periodic/411.pkg-backup \
			scripts/periodic/490.status-pkg-changes
dist_psecurity_DATA=	scripts/periodic/410.pkg-audit \
			scripts/periodic/460.pkg-checksum
dist_pweekly_DATA=	scripts/periodic/400.status-pkg
dist_bashcomp_DATA=	scripts/completion/_pkg.bash
dist_zshcomp_DATA=	scripts/completion/_pkg.zsh

include_HEADERS=	libpkg/pkg.h

check_PROGRAMS=	tests/pkg_printf tests/pkg_validation

libelf_la_SOURCES=	external/libelf/elf.c \
			external/libelf/elf_begin.c \
			external/libelf/elf_cntl.c \
			external/libelf/elf_data.c \
			external/libelf/elf_end.c \
			external/libelf/elf_errmsg.c \
			external/libelf/elf_errno.c \
			external/libelf/elf_fill.c \
			external/libelf/elf_flag.c \
			external/libelf/elf_getarhdr.c \
			external/libelf/elf_getarsym.c \
			external/libelf/elf_getbase.c \
			external/libelf/elf_getident.c \
			external/libelf/elf_hash.c \
			external/libelf/elf_kind.c \
			external/libelf/elf_memory.c \
			external/libelf/elf_next.c \
			external/libelf/elf_open.c \
			external/libelf/elf_phnum.c \
			external/libelf/elf_rand.c \
			external/libelf/elf_rawfile.c \
			external/libelf/elf_scn.c \
			external/libelf/elf_shnum.c \
			external/libelf/elf_shstrndx.c \
			external/libelf/elf_strptr.c \
			external/libelf/elf_update.c \
			external/libelf/elf_version.c \
			external/libelf/gelf_cap.c \
			external/libelf/gelf_checksum.c \
			external/libelf/gelf_dyn.c \
			external/libelf/gelf_ehdr.c \
			external/libelf/gelf_fsize.c \
			external/libelf/gelf_getclass.c \
			external/libelf/gelf_move.c \
			external/libelf/gelf_phdr.c \
			external/libelf/gelf_rel.c \
			external/libelf/gelf_rela.c \
			external/libelf/gelf_shdr.c \
			external/libelf/gelf_sym.c \
			external/libelf/gelf_syminfo.c \
			external/libelf/gelf_symshndx.c \
			external/libelf/gelf_xlate.c \
			external/libelf/libelf_align.c \
			external/libelf/libelf_allocate.c \
			external/libelf/libelf_ar.c \
			external/libelf/libelf_ar_util.c \
			external/libelf/libelf_checksum.c \
			external/libelf/libelf_data.c \
			external/libelf/libelf_ehdr.c \
			external/libelf/libelf_extended.c \
			external/libelf/libelf_memory.c \
			external/libelf/libelf_open.c \
			external/libelf/libelf_phdr.c \
			external/libelf/libelf_shdr.c \
			external/libelf/libelf_xlate.c \
			external/libelf/libelf_fsize.c \
			external/libelf/libelf_msize.c \
		       	external/libelf/libelf_convert.c
libelf_la_CFLAGS=	-I$(top_srcdir)/external/libelf
libucl_la_SOURCES=	external/libucl/src/ucl_emitter.c \
			external/libucl/src/ucl_hash.c \
			external/libucl/src/ucl_parser.c \
			external/libucl/src/ucl_util.c \
			external/libucl/src/xxhash.c
libucl_la_CFLAGS=	-I$(top_srcdir)/external/libucl/uthash \
			-I$(top_srcdir)/external/libucl/include  \
			-I$(top_srcdir)/external/libucl/src \
			-Wno-unused-parameter -Wno-pointer-sign
libexpat_la_SOURCES=	external/expat/lib/xmlparse.c \
			external/expat/lib/xmlrole.c \
			external/expat/lib/xmltok.c
libexpat_la_CFLAGS=	-I$(top_srcdir)/external/expat \
			-DHAVE_EXPAT_CONFIG_H
libyaml_la_SOURCES=	external/libyaml/src/api.c \
			external/libyaml/src/loader.c \
			external/libyaml/src/parser.c \
			external/libyaml/src/reader.c \
			external/libyaml/src/scanner.c
libyaml_la_CFLAGS=	-I$(top_srcdir)/external/libyaml/include \
			-I$(top_srcdir)/external/libyaml \
			-Wno-strict-aliasing \
			-Wno-unused
libsqlite_la_SOURCES=	external/sqlite/sqlite3.c \
			external/sqlite/shell.c
libsqlite_la_CFLAGS=	-DHAVE_POSIX_FALLOCATE=1 \
			-DHAVE_SYS_TYPES_H=1 \
			-DHAVE_SYS_STAT_H=1 \
			-DHAVE_STDLIB_H=1 \
			-DHAVE_STRING_H=1 \
			-DHAVE_MEMORY_H=1 \
			-DHAVE_STRINGS_H=1 \
			-DHAVE_INTTYPES_H=1 \
			-DHAVE_STDINT_H=1 \
			-DHAVE_UNISTD_H=1 \
			-DHAVE_DLFCN_H=1 \
			-DHAVE_USLEEP=1 \
			-DHAVE_LOCALTIME_R=1 \
			-DHAVE_GMTIME_R=1 \
			-DHAVE_DECL_STRERROR_R=1 \
			-DHAVE_STRERROR_R=1 \
			-Wno-unused-variable \
			-Wno-unused-function \
			-Wno-strict-aliasing \
			-DSQLITE_OMIT_AUTOVACUUM \
			-DSQLITE_OMIT_BLOB_LITERAL \
			-DSQLITE_OMIT_DECLTYPE \
			-DSQLITE_OMIT_EXPLAIN \
			-DSQLITE_OMIT_DEPRECATED \
			-DSQLITE_OMIT_LOAD_EXTENSION \
			-DSQLITE_OMIT_PROGRESS_CALLBACK \
			-DSQLITE_OMIT_TCL_VARIABLE \
			-DSQLITE_OMIT_UTF16 \
			-DSQLITE_OMIT_CAT \
			-DSQLITE_OMIT_CHECK \
			-DSQLITE_OMIT_AUTOINIT \
			-DSQLITE_OMIT_COMPILEOPTION_DIAGS \
			-DSQLITE_OMIT_INTEGRITY_CHECK \
			-DSQLITE_OMIT_BUILTIN_TEST \
			-DSQLITE_OMIT_SHARED_CACHE \
			-DSQLITE_ENABLE_UNLOCK_NOTIFY=1 \
			-DUSE_PREAD \
			-DSQLITE_THREADSAFE=1 \
			-DSQLITE_TEMP_STORE=3 \
			-Dmain=sqlite3_shell \
			-DNDEBUG
libpkg_la_SOURCES=	libpkg/pkg.c \
			libpkg/backup.c \
			libpkg/dns_utils.c \
			libpkg/elfhints.c \
			libpkg/fetch.c \
			libpkg/packing.c \
			libpkg/pkg_add.c \
			libpkg/pkg_attributes.c \
			libpkg/pkg_config.c \
			libpkg/pkg_conflicts.c \
			libpkg/pkg_cudf.c \
			libpkg/pkg_create.c \
			libpkg/pkg_delete.c \
			libpkg/pkg_elf.c \
			libpkg/pkg_event.c \
			libpkg/pkg_jobs.c \
			libpkg/pkg_manifest.c \
			libpkg/pkg_ports.c \
			libpkg/pkg_printf.c \
			libpkg/pkg_repo.c \
			libpkg/pkg_solve.c \
			libpkg/pkg_status.c \
			libpkg/pkg_version.c \
			libpkg/pkgdb.c \
			libpkg/pkgdb_repo.c \
			libpkg/rcscripts.c \
			libpkg/rsa.c \
			libpkg/ssh.c \
			libpkg/scripts.c \
			libpkg/update.c \
			libpkg/utils.c \
			libpkg/plugins.c \
			libpkg/pkg_old.c
libpkg_la_CFLAGS=	-I$(top_srcdir)/libpkg \
			@LIBELF_INCLUDE@ \
			-I$(top_srcdir)/external/libucl/include \
			-I$(top_srcdir)/external/uthash \
			-I$(top_srcdir)/external/sqlite \
			-I$(top_srcdir)/external/libyaml/include \
			-DPREFIX=\"$(prefix)\" \
			-Wno-pointer-sign
libpkg_la_LIBADD=	libucl.la libsqlite.la libyaml.la \
			@LIBELF_LIB@ \
			-larchive \
			-lsbuf \
			-lfetch \
			-lutil \
			-lssl \
			-lcrypto
libpkg_la_LDFLAGS=	-version-info 1 -pthread
pkg_SOURCES=		src/add.c \
			src/annotate.c \
			src/audit.c \
			src/autoremove.c \
			src/backup.c \
			src/check.c \
			src/clean.c \
			src/config.c \
			src/convert.c \
			src/create.c \
			src/delete.c \
			src/event.c \
			src/fetch.c \
			src/info.c \
			src/install.c \
			src/lock.c \
			src/main.c \
			src/plugins.c \
			src/progressmeter.c \
			src/query.c \
			src/register.c \
			src/repo.c \
			src/rquery.c \
			src/search.c \
			src/set.c \
			src/shell.c \
			src/shlib.c \
			src/ssh.c \
			src/stats.c \
			src/update.c \
			src/updating.c \
			src/upgrade.c \
			src/utils.c \
			src/version.c \
			src/which.c
pkg_LDADD=		libpkg.la libexpat.la \
			-ljail
pkg_CFLAGS=		-I$(top_srcdir)/libpkg \
			-I$(top_srcdir)/external/uthash \
			-I$(top_srcdir)/external/expat/lib \
			-DGITHASH=\"$(GIT_HEAD)\"
pkg_static_CFLAGS=	$(pkg_CFLAGS)
pkg_static_SOURCES=	$(pkg_SOURCES)
pkg_static_LDADD=	$(pkg_LDADD) -llzma -lbz2 -lz
pkg_static_LDFLAGS=	-all-static

tests_pkg_printf_SOURCES=	libpkg/pkg_printf.c \
				tests/lib/pkg_printf_test.c
tests_pkg_printf_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING
tests_pkg_printf_LDADD=		libpkg.la -latf-c
tests_pkg_printf_LDLAGS=	-Wl,-rpath=\$$ORIGIN/../.libs
tests_pkg_validation_SOURCES=	tests/lib/pkg_validation.c
tests_pkg_validation_CFLAGS=	-I$(top_srcdir)/libpkg -DTESTING
tests_pkg_validation_LDADD=		libpkg.la -latf-c
tests_pkg_validation_LDLAGS=	-Wl,-rpath=\$$ORIGIN/../.libs

external/libelf/libelf_convert.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=external/libelf $(top_srcdir)/external/libelf/elf_types.m4 $(top_srcdir)/external/libelf/libelf_convert.m4 > $@

external/libelf/libelf_fsize.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=external/libelf $(top_srcdir)/external/libelf/elf_types.m4 $(top_srcdir)/external/libelf/libelf_fsize.m4 > $@

external/libelf/libelf_msize.c:
	@echo "    M4    " $@;
	@m4 -D SRCDIR=external/libelf $(top_srcdir)/external/libelf/elf_types.m4 $(top_srcdir)/external/libelf/libelf_msize.m4 > $@

regression-test:check
	@echo "ATF/KYUA has change our testing framework is not compatible for now"
