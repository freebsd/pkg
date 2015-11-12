#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	search

search_body() {
	export REPOS_DIR=/nonexistent
	atf_check -e inline:"No active remote repositories configured.\n" -o empty -s exit:3 pkg -C '' -R '' search -e -Q comment -S name pkg
}
