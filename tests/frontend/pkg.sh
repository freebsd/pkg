atf_test_case pkg
pkg_head() {
	atf_set "descr" "testing pkg"
}

pkg_body() {
	atf_check -o empty -e inline:"pkg: package database non-existent\n" -s exit:69 -x PKG_DBDIR=/dev/null pkg -n
	eval `../../newvers.sh`
	atf_check -o match:"^${PKGVERSION} .*$" -e empty -s exit:0 pkg -v
	atf_check -o file:pkg-vv.out -e empty -s exit:0 -x 'ABI=freebsd pkg -C "" -vvv | grep -v "^version.*"'
}

atf_init_test_cases() {
	atf_add_test_case pkg
}
