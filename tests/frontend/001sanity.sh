#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
if [ "${OS}" != "Darwin" ]; then
	ldd=ldd
fi
tests_init \
	which \
	${ldd}

which_body() {
	atf_check \
	    -o inline:"$(atf_get_srcdir)/../../src/.libs/pkg\n" \
	    -e empty \
	    -s exit:0 \
	    which pkg
}


ldd_body() {
	atf_check \
	    -o match:".*libpkg.so.3 => $(atf_get_srcdir).*$" \
	    -e empty \
	    -s exit:0 \
	    ldd $(atf_get_srcdir)/../../src/.libs/pkg
}
