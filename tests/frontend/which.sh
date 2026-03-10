#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	which_basic \
	which_origin \
	which_quiet \
	which_quiet_origin \
	which_not_found \
	which_multiple \
	which_glob \
	which_glob_show_match \
	which_no_args \
	which_absolute_path

which_basic_body() {
	touch file1
	mkdir -p usr/local/bin

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/usr/local/bin/mybin: "",
}
EOF
	touch usr/local/bin/mybin

	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	atf_check \
	    -o match:"was installed by package test-1" \
	    -s exit:0 \
	    pkg which ${TMPDIR}/file1

	atf_check \
	    -o match:"was installed by package test-1" \
	    -s exit:0 \
	    pkg which ${TMPDIR}/usr/local/bin/mybin
}

which_origin_body() {
	touch file1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
}
EOF

	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	atf_check \
	    -o match:"was installed by package test" \
	    -s exit:0 \
	    pkg which -o ${TMPDIR}/file1
}

which_quiet_body() {
	touch file1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
}
EOF

	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	# Quiet mode: just package name-version
	atf_check \
	    -o inline:"test-1\n" \
	    -s exit:0 \
	    pkg which -q ${TMPDIR}/file1
}

which_quiet_origin_body() {
	touch file1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
}
EOF

	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	# Quiet + origin: just the origin
	atf_check \
	    -o inline:"test\n" \
	    -s exit:0 \
	    pkg which -qo ${TMPDIR}/file1
}

which_not_found_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	atf_check \
	    -o match:"was not found in the database" \
	    -s exit:1 \
	    pkg which /nonexistent/path/to/file

	# Quiet mode: no output
	atf_check \
	    -o empty \
	    -s exit:1 \
	    pkg which -q /nonexistent/path/to/file
}

which_multiple_body() {
	touch file1
	touch file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1" "${TMPDIR}"
	cat << EOF >> foo.ucl
files: {
    ${TMPDIR}/file1: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "2" "${TMPDIR}"
	cat << EOF >> bar.ucl
files: {
    ${TMPDIR}/file2: "",
}
EOF

	atf_check -o ignore -s exit:0 pkg register -M foo.ucl
	atf_check -o ignore -s exit:0 pkg register -M bar.ucl

	# Query two files at once
	atf_check \
	    -o match:"foo-1" \
	    -o match:"bar-2" \
	    -s exit:0 \
	    pkg which ${TMPDIR}/file1 ${TMPDIR}/file2
}

which_glob_body() {
	touch file1
	touch file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/file2: "",
}
EOF

	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	atf_check \
	    -o match:"test-1" \
	    -s exit:0 \
	    pkg which -g "${TMPDIR}/file*"
}

which_glob_show_match_body() {
	touch file1
	touch file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/file2: "",
}
EOF

	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	# -m shows matching files
	atf_check \
	    -o match:"${TMPDIR}/file1" \
	    -o match:"${TMPDIR}/file2" \
	    -s exit:0 \
	    pkg which -gm "${TMPDIR}/file*"
}

which_no_args_body() {
	atf_check \
	    -e match:"Usage" \
	    -s exit:1 \
	    pkg which
}

which_absolute_path_body() {
	# Relative paths should be resolved to absolute
	touch file1

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
}
EOF

	atf_check -o ignore -s exit:0 pkg register -M test.ucl

	# Use the absolute path explicitly
	atf_check \
	    -o match:"test-1" \
	    -s exit:0 \
	    pkg which ${TMPDIR}/file1
}
