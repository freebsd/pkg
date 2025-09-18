#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic

basic_body() {
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	cat <<__EOF__ >> test.ucl
files: {
    ${TMPDIR}/a: "",
}
__EOF__

	echo a > a
	atf_check pkg create -M test.ucl
	atf_check mkdir -p target
	atf_check pkg -o REPOS_DIR=/dev/null -r target install -qfy ${TMPDIR}/test-1.pkg
	atf_check pkg -r target check -q
	echo b > ${TMPDIR}/target/${TMPDIR}/a
	atf_check -s not-exit:0 -e inline:"test-1: checksum mismatch for ${TMPDIR}/a\n" \
	    pkg -r target check -q
}
