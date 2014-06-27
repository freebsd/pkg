AUTOMAKE_OPTIONS=	subdir-objects
ACLOCAL_AMFLAGS=	-I m4

pkgconfigdir=	$(prefix)/libdata/pkgconfig
pkgconfig_DATA=	libpkg/pkg.pc

$(pkgconfig_DATA): config.status

EXTRA_DIST=	docs
dist-hook:
	rm -rf `find $(distdir)/external -type d -name .deps`

SUBDIRS = external libpkg src tests scripts docs
