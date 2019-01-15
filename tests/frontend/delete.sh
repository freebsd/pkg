#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	delete_all \
	delete_pkg \
	delete_with_directory_owned \
	simple_delete \
	simple_delete_prefix_ending_with_slash

delete_all_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkg" "pkg" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	atf_check -o ignore pkg register -M foo.ucl
	atf_check -o ignore pkg register -M pkg.ucl
	atf_check -o ignore pkg register -M test.ucl

	atf_check -o ignore pkg delete -ay
}

delete_pkg_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkg" "pkg" "1"
	atf_check -o ignore pkg register -M pkg.ucl
	atf_check -o ignore -e ignore -s exit:3 pkg delete -y pkg
	atf_check -o ignore -e ignore pkg delete -yf pkg
}

simple_delete_body() {
	touch file1
	mkdir dir
	touch dir/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test

	test -f file1 && atf_fail "'file1' still present"
	test -f dir/file2 && atf_fail "'dir/file2' still present"
	test -d dir && atf_fail "'dir' still present"
	test -d ${TMPDIR} || atf_fail "Prefix have been removed"
}

simple_delete_prefix_ending_with_slash_body() {
	touch file1
	mkdir dir
	touch dir/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}/"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test

	test -f file1 && atf_fail "'file1' still present"
	test -f dir/file2 && atf_fail "'dir/file2' still present"
	test -d dir && atf_fail "'dir' still present"
	test -d ${TMPDIR} || atf_fail "Prefix have been removed"
}

delete_with_directory_owned_body() {
	touch file1
	mkdir dir
	touch dir/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}/"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test2" "test2" "1" "${TMPDIR}/"
	cat << EOF >> test2.ucl
directories: {
    ${TMPDIR}/dir: 'y',
}
EOF
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M test2.ucl

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test

	test -f file1 && atf_fail "'file1' still present"
	test -f dir/file2 && atf_fail "'dir/file2' still present"
	test -d dir || atf_fail "'dir' has been removed"

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg delete -y test2

	test -d dir && atf_fail "'dir' still present"
	test -d ${TMPDIR} || atf_fail "Prefix has been removed"
}
