atf_test_case pkg
pkg_head() {
	atf_set "descr" "testing pkg"
}

pkg_body() {
	atf_check -o empty -e inline:"pkg: package database non-existent\n" -s exit:69 -x PKG_DBDIR=/dev/null pkg -n
}

atf_init_test_cases() {
	atf_add_test_case pkg
}
