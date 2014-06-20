EXTRA_DIST=		libpkg.ver

pkg_common_cflags=	-I$(top_srcdir)/libpkg \
			@LIBELF_INCLUDE@ \
			@LIBSBUF_INCLUDE@ \
			@LDNS_CFLAGS@ \
			-I$(top_srcdir)/external/expat/lib \
			-I$(top_srcdir)/external/libucl/include \
			-I$(top_srcdir)/external/uthash \
			-I$(top_srcdir)/external/sqlite \
			-I$(top_srcdir)/external/libyaml/include \
			-DPREFIX=\"$(prefix)\" \
			-Wno-pointer-sign
libpkg_la_SOURCES=	pkg.c \
			backup.c \
			dns_utils.c \
			elfhints.c \
			fetch.c \
			packing.c \
			pkg_add.c \
			pkg_attributes.c \
			pkg_audit.c \
			pkg_checksum.c \
			pkg_config.c \
			pkg_conflicts.c \
			pkg_cudf.c \
			pkg_create.c \
			pkg_delete.c \
			pkg_elf.c \
			pkg_event.c \
			pkg_jobs.c \
			pkg_manifest.c \
			pkg_object.c \
			pkg_ports.c \
			pkg_printf.c \
			pkg_repo.c \
			pkg_repo_create.c \
			pkg_repo_update.c \
			pkg_repo_meta.c \
			pkg_solve.c \
			pkg_status.c \
			pkg_version.c \
			pkgdb.c \
			pkgdb_iterator.c \
			pkgdb_query.c \
			rcscripts.c \
			rsa.c \
			ssh.c \
			scripts.c \
			utils.c \
			plugins.c \
			pkg_old.c
libpkg_la_CFLAGS=	$(pkg_common_cflags) -shared
libpkg_la_LIBADD=	$(top_builddir)/external/libucl.la \
			$(top_builddir)/external/libsqlite.la \
			$(top_builddir)/external/libyaml.la \
			$(top_builddir)/external/libexpat.la \
			@REPOS_LDADD@ \
			@LIBELF_LIB@ \
			@LIBSBUF_LIB@ \
			@LIBEXECINFO_LIB@ \
			@LDNS_LIBS@ \
			-larchive \
			-lfetch \
			-lutil \
			-lcrypto \
			-lm

libpkg_la_LDFLAGS=	-version-info @LIBPKG_SO_VERSION@ -Wl,--version-script=$(srcdir)/libpkg.ver
lib_LTLIBRARIES=	libpkg.la libpkg_static.la
include_HEADERS=	pkg.h

libpkg_static_la_SOURCES=	$(libpkg_la_SOURCES)
libpkg_static_la_CFLAGS=	$(pkg_common_cflags) -static
libpkg_static_la_LIBADD=	$(top_builddir)/external/libucl_static.la \
			$(top_builddir)/external/libsqlite_static.la \
			$(top_builddir)/external/libexpat_static.la \
			$(top_builddir)/external/libyaml_static.la \
			@REPOS_LDADD_STATIC@

libpkg_static_la_LDFLAGS=	-all-static

noinst_HEADERS=		private/db_upgrades.h \
			private/elf_tables.h \
			private/event.h \
			private/ldconfig.h \
			private/pkg.h \
			private/pkg_printf.h \
			private/pkgdb.h \
			private/repodb.h \
			private/utils.h

SUBDIRS = repo .