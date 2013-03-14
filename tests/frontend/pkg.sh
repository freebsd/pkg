atf_test_case pkg
pkg_head() {
	atf_set "descr" "testing pkg"
}

pkg_body() {
	eval `../../newvers.sh`
	atf_check -o match:"^${PKGVERSION} .*$" -e empty -s exit:0 pkg -v
}

atf_init_test_cases() {
	atf_add_test_case pkg
}
