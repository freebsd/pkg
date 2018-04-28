#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	rootdir

rootdir_body() {
	unset PKG_DBDIR
	if [ `uname -s` = "Linux" ]; then
		RP='readlink -e'
	else
		RP='realpath'
	fi

	atf_check \
		-o inline:"`${RP} ${TMPDIR}`/var/db/pkg\n" \
		-e empty \
		-s exit:0 \
		pkg -r "${TMPDIR}" config pkg_dbdir
}
