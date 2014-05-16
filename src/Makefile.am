noinst_HEADERS=	pkgcli.h progressmeter.h

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
			
pkg_LDADD=	$(top_builddir)/libpkg/libpkg.la \
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
