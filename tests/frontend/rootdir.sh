#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	rootdir

rootdir_body() {
	unset PKG_DBDIR
	RP='realpath'

	atf_check \
		-o inline:"`${RP} ${TMPDIR}`/var/db/pkg\n" \
		-e empty \
		-s exit:0 \
		pkg -r "${TMPDIR}" config pkg_dbdir
}
