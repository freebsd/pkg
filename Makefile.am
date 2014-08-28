AUTOMAKE_OPTIONS=	subdir-objects
ACLOCAL_AMFLAGS=	-I m4

pkgconfigdir=	$(prefix)/libdata/pkgconfig
pkgconfig_DATA=	libpkg/pkg.pc

$(pkgconfig_DATA): config.status

DISTCLEANFILES=	pkg_repos.h
EXTRA_DIST=	docs
dist-hook:
	rm -rf `find $(distdir)/external -type d -name .deps`

check-local:
	kyua --config=none test \
		--kyuafile='$(top_srcdir)/Kyuafile' \
		--build-root='$(top_builddir)'

SUBDIRS = external libpkg src tests scripts docs
