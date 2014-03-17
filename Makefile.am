AUTOMAKE_OPTIONS=	subdir-objects
ACLOCAL_AMFLAGS=	-I m4

EXTRA_DIST=	docs
dist-hook:
	rm -rf `find $(distdir)/external -type d -name .deps`

SUBDIRS = external libpkg src @BUILD_TESTS@ scripts
