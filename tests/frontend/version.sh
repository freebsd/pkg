#! /usr/bin/env atf-sh

atf_test_case version
version_head() {
	atf_set "descr" "testing pkg version"
}

version_body() {
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1 2
	atf_check -o inline:">\n" -s exit:0 pkg version -t 2 1
	atf_check -o inline:"=\n" -s exit:0 pkg version -t 2 2
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 2 1,1
}

atf_init_test_cases() {
        eval `cat $(atf_get_srcdir)/test_environment`

	atf_add_test_case version
}
