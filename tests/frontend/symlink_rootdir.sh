#! /usr/bin/env atf-sh

#
# Symlink handling inside a rootdir (-r).  A package may legitimately be
# installed through a directory symlink already present in the target, so
# every case below but the last must keep working; the last one must not
# write outside of the rootdir.
#

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	relative_symlink \
	absolute_symlink_inside \
	absolute_symlink_image \
	dead_symlink \
	outside_symlink

# Create a package shipping ${TMPDIR}/link/file.
_package_with_file_under_link() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
files: {
${TMPDIR}/link/file = "";
}
EOF
	atf_check -o ignore -e ignore -s exit:0 \
		pkg create -M test.ucl
}

relative_symlink_body()
{
	mkdir -p ${TMPDIR}/build${TMPDIR}/inside
	mkdir -p ${TMPDIR}/root${TMPDIR}/inside
	# the link in the target is relative, it only resolves inside the root
	ln -sf inside ${TMPDIR}/root${TMPDIR}/link
	# the same link on the build host, to create the package
	ln -sf ${TMPDIR}/build${TMPDIR}/inside ${TMPDIR}/link
	echo content > ${TMPDIR}/link/file

	_package_with_file_under_link

	atf_check -o ignore -e ignore -s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/root install -qy \
			${TMPDIR}/test-1.pkg

	test -h ${TMPDIR}/root${TMPDIR}/link || atf_fail "symlink replaced"
	test -f ${TMPDIR}/root${TMPDIR}/inside/file || \
		atf_fail "file not installed through the relative symlink"
}

absolute_symlink_inside_body()
{
	mkdir -p ${TMPDIR}/build${TMPDIR}/inside
	mkdir -p ${TMPDIR}/root${TMPDIR}/inside
	# absolute, but it resolves inside the target root
	ln -sf ${TMPDIR}/root${TMPDIR}/inside ${TMPDIR}/root${TMPDIR}/link
	ln -sf ${TMPDIR}/build${TMPDIR}/inside ${TMPDIR}/link
	echo content > ${TMPDIR}/link/file

	_package_with_file_under_link

	atf_check -o ignore -e ignore -s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/root install -qy \
			${TMPDIR}/test-1.pkg

	test -h ${TMPDIR}/root${TMPDIR}/link || atf_fail "symlink replaced"
	test -f ${TMPDIR}/root${TMPDIR}/inside/file || \
		atf_fail "file not installed through the absolute symlink"
}

absolute_symlink_image_body()
{
	# Known limitation: when the host side of the absolute target does not
	# exist at all, the temporary directory machinery replaces the symlink
	# with a directory instead of following the re-rooted target.  Remove
	# this marker once open_tempdir() keeps its name/len bookkeeping on a
	# resolved path.
	atf_expect_fail "absolute target absent on the host"

	# The target of the symlink is an absolute path as seen from inside
	# the target root; it has to be resolved there and not on the host.
	mkdir -p ${TMPDIR}/build${TMPDIR}/inside
	mkdir -p ${TMPDIR}/root${TMPDIR}/inside
	ln -sf ${TMPDIR}/inside ${TMPDIR}/root${TMPDIR}/link
	ln -sf ${TMPDIR}/build${TMPDIR}/inside ${TMPDIR}/link
	echo content > ${TMPDIR}/link/file

	_package_with_file_under_link

	atf_check -o ignore -e ignore -s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/root install -qy \
			${TMPDIR}/test-1.pkg

	test -f ${TMPDIR}/root${TMPDIR}/inside/file || \
		atf_fail "file not installed inside the rootdir"
	test ! -e ${TMPDIR}/inside/file || \
		atf_fail "file written on the host"
}

dead_symlink_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
directories {
	${TMPDIR}/link = "y";
}
EOF
	mkdir -p ${TMPDIR}/link
	atf_check -o ignore -e ignore -s exit:0 pkg create -M test.ucl

	# the target root has a dangling symlink where the directory must go
	mkdir -p ${TMPDIR}/root${TMPDIR}
	rmdir ${TMPDIR}/link
	ln -sf ${TMPDIR}/root${TMPDIR}/missing ${TMPDIR}/root${TMPDIR}/link

	atf_check -o ignore -e ignore -s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/root install -qy \
			${TMPDIR}/test-1.pkg

	test -d ${TMPDIR}/root${TMPDIR}/link || \
		atf_fail "dangling symlink not replaced by a directory"
}

outside_symlink_body()
{
	mkdir -p ${TMPDIR}/build${TMPDIR}/inside
	mkdir -p ${TMPDIR}/root${TMPDIR} ${TMPDIR}/outside
	# absolute and outside of the target root: it must not be followed
	ln -sf ${TMPDIR}/outside ${TMPDIR}/root${TMPDIR}/link
	ln -sf ${TMPDIR}/build${TMPDIR}/inside ${TMPDIR}/link
	echo content > ${TMPDIR}/link/file

	_package_with_file_under_link

	pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/root install -qy \
		${TMPDIR}/test-1.pkg > /dev/null 2>&1 || :

	test ! -e ${TMPDIR}/outside/file || \
		atf_fail "written $(ls ${TMPDIR}/outside) outside of the rootdir"
}
