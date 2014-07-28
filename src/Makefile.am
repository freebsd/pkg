noinst_HEADERS=	pkgcli.h

pkg_SOURCES=		add.c \
			annotate.c \
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
			globals.c \
			info.c \
			install.c \
			lock.c \
			main.c \
			plugins.c \
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
			
pkg_LDADD=	$(top_builddir)/libpkg/libpkg.la \
		$(top_builddir)/external/libsbuf.la \
			@LIBJAIL_LIB@ \
			-lutil \
			-lcrypto
pkg_CFLAGS=		-I$(top_srcdir)/libpkg \
			-I$(top_srcdir)/external/uthash \
			-I$(top_srcdir)/external/expat/lib \
			-DGITHASH=\"$(GIT_HEAD)\"
pkg_static_SOURCES=
pkg_static_LDADD= $(top_builddir)/libpkg/libpkg_static.la \
			$(top_builddir)/external/libsbuf_static.la \
			$(pkg_OBJECTS) \
			@LIBJAIL_LIB@ \
			@LIBEXECINFO_LIB@ \
			@LIBELF_LIB@ \
			@LDNS_LIBS@ \
			-lfetch \
			-larchive \
			-lz \
			-lutil \
			-lbz2 \
			-llzma \
			-lssl \
			-lcrypto \
			-lmd \
			-lm
pkg_static_LDFLAGS=	-all-static
sbin_PROGRAMS=		pkg pkg-static

dist_sysconf_DATA=	pkg.conf.sample
