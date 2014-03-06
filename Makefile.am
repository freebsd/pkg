AUTOMAKE_OPTIONS=	subdir-objects
ACLOCAL_AMFLAGS=	-I m4

EXTRA_DIST=	external/expat external/libyaml external/libelf \
		 external/libucl external/uthash external/sqlite docs
dist-hook:
	rm -rf `find $(distdir)/external -type d -name .deps`

SUBDIRS = external libpkg src tests scripts