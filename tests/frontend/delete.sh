#! /usr/bin/env atf-sh

atf_test_case simple_delete
simple_delete_head() {
	atf_set "descr" "Testing pkg delete"
}

simple_delete_body() {
	touch file1
	mkdir dir
	touch dir/file2

	cat << EOF >> test.ucl
name: test
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: ${TMPDIR}
desc: <<EOD
Yet another test
EOD
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

atf_test_case simple_delete_prefix_ending_with_slash
simple_delete_prefix_ending_with_slash_head() {
	atf_set "descr" "Testing pkg delete when prefix end with /"
}

simple_delete_prefix_ending_with_slash_body() {
	touch file1
	mkdir dir
	touch dir/file2

	cat << EOF >> test.ucl
name: test
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: ${TMPDIR}/
desc: <<EOD
Yet another test
EOD
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

atf_test_case delete_with_directory_owned
delete_with_directory_owned_head() {
	atf_set "descr" "Testing pkg delete when a directory is owned by another package"
}

delete_with_directory_owned_body() {
	touch file1
	mkdir dir
	touch dir/file2

	cat << EOF >> test.ucl
name: test
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: ${TMPDIR}/
desc: <<EOD
Yet another test
EOD
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	cat << EOF >> test2.ucl
name: test2
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: ${TMPDIR}/
desc: <<EOD
Yet another test
EOD
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

atf_init_test_cases() {
	. $(atf_get_srcdir)/test_environment.sh

	atf_add_test_case simple_delete
	atf_add_test_case simple_delete_prefix_ending_with_slash
	atf_add_test_case delete_with_directory_owned
}
