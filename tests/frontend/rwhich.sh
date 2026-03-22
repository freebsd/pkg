#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	rwhich_basic \
	rwhich_glob \
	rwhich_quiet \
	rwhich_not_found \
	rwhich_multiple_packages \
	rwhich_no_args \
	rwhich_multiple_args \
	rwhich_repo_flag \
	rwhich_shared_dirs \
	rwhich_no_filelist

rwhich_basic_body() {
	mkdir -p usr/local/bin usr/local/lib
	touch usr/local/bin/mybin usr/local/lib/libtest.so.1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/usr/local/bin/mybin: "",
    ${TMPDIR}/usr/local/lib/libtest.so.1: "",
}
EOF

	atf_check -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo -l .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	atf_check \
		-o match:"is provided by package test-1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich ${TMPDIR}/usr/local/bin/mybin

	atf_check \
		-o match:"is provided by package test-1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich ${TMPDIR}/usr/local/lib/libtest.so.1
}

rwhich_glob_body() {
	mkdir -p usr/local/bin usr/local/lib
	touch usr/local/bin/mybin usr/local/bin/mybin2 usr/local/lib/libtest.so.1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/usr/local/bin/mybin: "",
    ${TMPDIR}/usr/local/bin/mybin2: "",
    ${TMPDIR}/usr/local/lib/libtest.so.1: "",
}
EOF

	atf_check -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo -l .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	atf_check \
		-o match:"is provided by package test-1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich -g "*/lib/libtest*"
}

rwhich_quiet_body() {
	mkdir -p usr/local/bin
	touch usr/local/bin/mybin

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/usr/local/bin/mybin: "",
}
EOF

	atf_check -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo -l .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	atf_check \
		-o inline:"test-1\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich -q ${TMPDIR}/usr/local/bin/mybin
}

rwhich_not_found_body() {
	mkdir -p usr/local/bin
	touch usr/local/bin/mybin

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/usr/local/bin/mybin: "",
}
EOF

	atf_check -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo -l .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	atf_check \
		-o match:"was not found in the repository" \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich /nonexistent/file
}

rwhich_multiple_packages_body() {
	mkdir -p usr/local/bin usr/local/lib usr/local/share
	touch usr/local/share/common usr/local/bin/tool1
	touch usr/local/bin/tool2 usr/local/lib/libfoo.so

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test1 test1 1 "${TMPDIR}"
	cat << EOF >> test1.ucl
files: {
    ${TMPDIR}/usr/local/share/common: "",
    ${TMPDIR}/usr/local/bin/tool1: "",
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test2 test2 2 "${TMPDIR}"
	cat << EOF >> test2.ucl
files: {
    ${TMPDIR}/usr/local/bin/tool2: "",
    ${TMPDIR}/usr/local/lib/libfoo.so: "",
}
EOF

	atf_check -s exit:0 pkg create -M test1.ucl
	atf_check -s exit:0 pkg create -M test2.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo -l .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	atf_check \
		-o match:"is provided by package test1-1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich ${TMPDIR}/usr/local/bin/tool1

	atf_check \
		-o match:"is provided by package test2-2" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich ${TMPDIR}/usr/local/lib/libfoo.so
}

rwhich_no_args_body() {
	atf_check \
		-e inline:"Usage: pkg rwhich [-gq] [-r reponame] <file>\n\nFor more information see 'pkg help rwhich'.\n" \
		-s exit:1 \
		pkg rwhich
}

rwhich_multiple_args_body() {
	mkdir -p usr/local/bin usr/local/lib
	touch usr/local/bin/mybin usr/local/lib/libtest.so.1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/usr/local/bin/mybin: "",
    ${TMPDIR}/usr/local/lib/libtest.so.1: "",
}
EOF

	atf_check -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo -l .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	# Query two files at once
	atf_check \
		-o match:"usr/local/bin/mybin is provided by package test-1" \
		-o match:"usr/local/lib/libtest.so.1 is provided by package test-1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich \
		    ${TMPDIR}/usr/local/bin/mybin ${TMPDIR}/usr/local/lib/libtest.so.1
}

rwhich_repo_flag_body() {
	mkdir -p usr/local/bin usr/local/sbin
	touch usr/local/bin/mybin usr/local/sbin/othertool

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg alpha alpha 1 "${TMPDIR}"
	cat << EOF >> alpha.ucl
files: {
    ${TMPDIR}/usr/local/bin/mybin: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg beta beta 2 "${TMPDIR}"
	cat << EOF >> beta.ucl
files: {
    ${TMPDIR}/usr/local/sbin/othertool: "",
}
EOF

	atf_check -s exit:0 pkg create -o ${TMPDIR}/repoA -M alpha.ucl
	atf_check -s exit:0 pkg create -o ${TMPDIR}/repoB -M beta.ucl

	atf_check -o ignore pkg repo -l ${TMPDIR}/repoA
	atf_check -o ignore pkg repo -l ${TMPDIR}/repoB

	mkdir -p reposconf
	cat << EOF > reposconf/multi.conf
repoA: {
    url: file://${TMPDIR}/repoA,
    enabled: true
}
repoB: {
    url: file://${TMPDIR}/repoB,
    enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	# File is in repoA, should be found when restricting to repoA
	atf_check \
		-o match:"is provided by package alpha-1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich -r repoA \
		    ${TMPDIR}/usr/local/bin/mybin

	# File is not in repoB, should not be found
	atf_check \
		-o match:"was not found in the repository" \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich -r repoB \
		    ${TMPDIR}/usr/local/bin/mybin
}

rwhich_shared_dirs_body() {
	# Verify that packages sharing the same directory are
	# both found via glob
	mkdir -p usr/local/bin
	touch usr/local/bin/tool1 usr/local/bin/tool2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg1 pkg1 1 "${TMPDIR}"
	cat << EOF >> pkg1.ucl
files: {
    ${TMPDIR}/usr/local/bin/tool1: "",
}
EOF
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg pkg2 pkg2 1 "${TMPDIR}"
	cat << EOF >> pkg2.ucl
files: {
    ${TMPDIR}/usr/local/bin/tool2: "",
}
EOF

	atf_check -s exit:0 pkg create -M pkg1.ucl
	atf_check -s exit:0 pkg create -M pkg2.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo -l .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	# Glob matching both packages in the same directory
	atf_check \
		-o match:"pkg1-1" \
		-o match:"pkg2-1" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich -gq "*/usr/local/bin/tool*"
}

rwhich_no_filelist_body() {
	# Repo created without -l should not have file data
	mkdir -p usr/local/bin
	touch usr/local/bin/mybin

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/usr/local/bin/mybin: "",
}
EOF

	atf_check -s exit:0 pkg create -M test.ucl
	atf_check -o ignore -e empty -s exit:0 pkg repo .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore -e empty -s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" update

	# Without filelist data, rwhich should not find anything
	atf_check \
		-o match:"was not found in the repository" \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" \
		    -o PKG_CACHEDIR="${TMPDIR}" rwhich ${TMPDIR}/usr/local/bin/mybin
}
