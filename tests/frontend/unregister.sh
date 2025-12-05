#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	unregister_all \
	unregister_pkg \
	unregister_with_directory_owned \
	simple_unregister \
	simple_unregister_prefix_ending_with_slash

unregister_all_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkg" "pkg" "1"
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	atf_check -o ignore pkg register -M foo.ucl
	atf_check -o ignore pkg register -M pkg.ucl
	atf_check -o ignore pkg register -M test.ucl

	atf_check -o ignore pkg unregister -ay
}

unregister_pkg_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "pkg" "pkg" "1"
	atf_check -o ignore pkg register -M pkg.ucl
	atf_check -o ignore -e ignore -s exit:3 pkg unregister -y pkg
	atf_check -o ignore -e ignore pkg unregister -yf pkg
}

simple_unregister_body() {
	touch file1
	mkdir dir1
	mkdir dir2
	touch dir1/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir1/file2: "",
}
directories: {
    ${TMPDIR}/dir1: {
        uname: "",
        gname: "",
        perm: "0000",
        fflags: 0
    }
    ${TMPDIR}/dir2: {
        uname: "",
        gname: "",
        perm: "0000",
        fflags: 0
    }
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
		pkg unregister -y test

	atf_check \
		-o ignore \
		-e ignore \
		-s exit:0 \
		test -f file1

	atf_check \
		-o ignore \
		-e ignore \
		-s exit:0 \
		test -f dir1/file2

	atf_check \
		-o ignore \
		-e ignore \
		-s exit:0 \
		test -d dir1

	atf_check \
		-o ignore \
		-e ignore \
		-s exit:0 \
		test -d dir2
}

simple_unregister_prefix_ending_with_slash_body() {
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
		pkg unregister -y test

	test -f file1 || atf_fail "'file1' is not present"
	test -f dir/file2 || atf_fail "'dir/file2' is not present"
	test -d dir || atf_fail "'dir' is not present"
	test -d ${TMPDIR} || atf_fail "Prefix have been removed"
}

unregister_with_directory_owned_body() {
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
		pkg unregister -y test

	test -f file1 || atf_fail "'file1' is not present"
	test -f dir/file2 || atf_fail "'dir/file2' is not present"
	test -d dir || atf_fail "'dir' has been removed"

	atf_check \
		-o match:".*Deinstalling.*" \
		-e empty \
		-s exit:0 \
		pkg unregister -y test2

	test -d dir || atf_fail "'dir' is not present"
	test -d ${TMPDIR} || atf_fail "Prefix has been removed"
}
