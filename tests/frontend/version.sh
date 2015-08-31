#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	version

version_body() {
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1 2
	atf_check -o inline:">\n" -s exit:0 pkg version -t 2 1
	atf_check -o inline:"=\n" -s exit:0 pkg version -t 2 2
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 2 1,1
}
