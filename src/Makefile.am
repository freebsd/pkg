noinst_HEADERS=	pkgcli.h progressmeter.h

pkg_SOURCES=		annotate.c \
			audit.c \
			autoremove.c \
			backup.c \
			check.c \
			clean.c \
			config.c \
			convert.c \
			create.c \
			delete.c \
			event.c \
			fetch.c \
			info.c \
			install.c \
			lock.c \
			main.c \
			plugins.c \
			progressmeter.c \
			query.c \
			register.c \
			repo.c \
			rquery.c \
			search.c \
			set.c \
			shell.c \
			shlib.c \
			ssh.c \
			stats.c \
			update.c \
			updating.c \
			upgrade.c \
			utils.c \
			version.c \
			which.c
			
dist_man_MANS=		pkg-annotate.8 \
			pkg-audit.8 \
			pkg-autoremove.8 \
			pkg-backup.8 \
			pkg-check.8 \
			pkg-clean.8 \
			pkg-config.8 \
			pkg-convert.8 \
			pkg-create.8 \
			pkg-delete.8 \
			pkg-fetch.8 \
			pkg-info.8 \
			pkg-install.8 \
			pkg-lock.8 \
			pkg-query.8 \
			pkg-register.8 \
			pkg-repo.8 \
			pkg-rquery.8 \
			pkg-search.8 \
			pkg-set.8 \
			pkg-shell.8 \
			pkg-shlib.8 \
			pkg-stats.8 \
			pkg-update.8 \
			pkg-updating.8 \
			pkg-upgrade.8 \
			pkg-version.8 \
			pkg-which.8 \
			pkg.8 \
			pkg.conf.5
			
pkg_LDADD=	$(top_builddir)/libpkg/libpkg.la \
			$(top_builddir)/external/libexpat.la \
			@LIBJAIL_LIB@
pkg_CFLAGS=		-I$(top_srcdir)/libpkg \
			-I$(top_srcdir)/external/uthash \
			-I$(top_srcdir)/external/expat/lib \
			-DGITHASH=\"$(GIT_HEAD)\"
pkg_static_SOURCES=
pkg_static_LDADD=	$(top_builddir)/libpkg/libpkg_static.la \
			$(top_builddir)/external/libexpat_static.la \
			$(pkg_OBJECTS) \
			@LIBJAIL_LIB@ \
			@LIBSBUF_LIB@ \
			@LIBELF_LIB@ \
			-lfetch \
			-larchive \
			-lz \
			-lutil \
			-lsbuf \
			-lbz2 \
			-llzma \
			-lssl \
			-lcrypto \
			-lmd \
			-lm
pkg_static_LDFLAGS=	-all-static -pthread
sbin_PROGRAMS=		pkg pkg-static

dist_sysconf_DATA=	pkg.conf.sample
