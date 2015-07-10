#!/usr/bin/env atf-sh

atf_test_case rootdir
rootdir_head() {
	atf_set "descr" "pkg -r <rootdir>"
}

rootdir_body() {
	unset PKG_DBDIR

	atf_check \
		-o inline:"${TMPDIR}/var/db/pkg\n" \
		-e empty \
		-s exit:0 \
		pkg -r "${TMPDIR}" config pkg_dbdir
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/test_environment.sh

	atf_add_test_case rootdir
}
