AUTOMAKE_OPTIONS=	subdir-objects
ACLOCAL_AMFLAGS=	-I m4

pkgconfigdir=	$(prefix)/libdata/pkgconfig
pkgconfig_DATA=	libpkg/pkg.pc

$(pkgconfig_DATA): config.status

DISTCLEANFILES=	pkg_repos.h
EXTRA_DIST=	docs Kyuafile
dist-hook:
	rm -rf `find $(distdir)/external -type d -name .deps`

check-local:
	if [ "$(HTML)" != "" ]; then \
		args="-r $(top_builddir)/res.db" ; \
	fi ; \
	kyua --config=none test $$args \
		--kyuafile='$(top_srcdir)/Kyuafile' \
		--build-root='$(top_builddir)' || FAILED=1 ; \
	if [ "$(HTML)" != "" ]; then \
		kyua report-html --force $$args --output=$(HTML) ; \
		rm -f $(top_builddir)/res.db ; \
	fi ; \
	exit $${FAILED}

COCCI_ARGS=	-I ${top_srcdir} \
		-I /usr/include \
		-I /usr/local/include \
		-I ${top_srcdir}/compat \
		-I ${top_srcdir}/libpkg \
		-I ${top_srcdir}/src \
		-I ${top_srcdir}/external/expat/lib \
		-I ${top_srcdir}/external/libyaml/include \
		-I ${top_srcdir}/external/libucl/include \
		-I ${top_srcdir}/external/uthash \
		-I ${top_srcdir}/external/sqlite  \
		-I ${top_srcdir}/external/libelf

1cocci:
	spatch ${COCCI_ARGS} -in_place -sp_file $(top_srcdir)/tests/cocci/${COCCITEST} -dir ${top_srcdir}/libpkg ; \
	spatch ${COCCI_ARGS} -in_place -sp_file $(top_srcdir)/tests/cocci/${COCCITEST} -dir ${top_srcdir}/src ;

cocci:
	for c in ${top_srcdir}/tests/cocci/*.cocci ; do \
		spatch ${COCCI_ARGS} -in_place -sp_file $$c -dir ${top_srcdir}/libpkg ; \
		spatch ${COCCI_ARGS} -in_place -sp_file $$c -dir ${top_srcdir}/src ; \
	done

STYLEPATTERN=	*.[c]

style-fix:
	find ${top_srcdir}/src ${top_srcdir}/libpkg -name "${STYLEPATTERN}" | xargs \
		uncrustify -c ${top_srcdir}/freebsd.cfg --no-backup


SUBDIRS = compat external libpkg src tests scripts docs
