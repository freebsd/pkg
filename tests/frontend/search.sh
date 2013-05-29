#! /usr/bin/env atf-sh

atf_test_case search
search_head() {
	atf_set "descr" "testing pkg search"
}

search_body() {
	REPOS_DIR=/nonexistent
	atf_check -o inline:"pkg                            New generation package manager\n" -e empty -s exit:0 pkg -C '' search -e -Q comment -S name pkg
}

atf_init_test_cases() {
        . $(atf_get_srcdir)/test_environment

	unset PKG_DBDIR

	atf_add_test_case search
}
