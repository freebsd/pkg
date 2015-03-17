#!/usr/bin/env atf-sh

atf_test_case which
which_head() {
	atf_set "descr" "test the sanity of the tested pkg location"
}

which_body() {
	atf_check \
	    -o inline:"$(atf_get_srcdir)/../../src/pkg\n" \
	    -e empty \
	    -s exit:0 \
	    which pkg
}

atf_test_case ldd
ldd_head() {
	atf_set "descr" "test the sanity of the ldd output"
}

ldd_body() {
	atf_check \
	    -o match:".*libpkg.so.3 => $(atf_get_srcdir).*$" \
	    -e empty \
	    -s exit:0 \
	    ldd -a $(atf_get_srcdir)/../../src/.libs/pkg
}
atf_init_test_cases() {
	. $(atf_get_srcdir)/test_environment.sh

	atf_add_test_case which
	if [ `uname -s` != "Darwin" ]; then
		atf_add_test_case ldd
	fi
}
