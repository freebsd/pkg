#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	simple_delete \
	simple_delete_prefix_ending_with_slash \
	delete_with_directory_owned

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
