#! /usr/bin/env atf-sh

atf_test_case search
search_head() {
	atf_set "descr" "testing pkg search"
}

search_body() {
	REPOS_DIR=/nonexistent
	atf_check -e inline:"pkg: No active remote repositories configured\n" -o empty -s exit:74 pkg -C '' -R '' search -e -Q comment -S name pkg
}

atf_init_test_cases() {
        . $(atf_get_srcdir)/test_environment

	unset PKG_DBDIR

	atf_add_test_case search
}
