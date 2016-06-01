#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	config

config_body()
{
	new_pkg "test" "test" "1"
	echo "@config ${TMPDIR}/a" > plist

	echo "entry" > a

	atf_check \
		pkg create -M test.ucl -p plist

	atf_check \
		-o match:"^config" \
		pkg info -R --raw-format ucl -F ${TMPDIR}/test-1.txz

	mkdir ${TMPDIR}/target
	unset PKG_DBDIR
	atf_check \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy ${TMPDIR}/test-1.txz
	test -f ${TMPDIR}/target/${TMPDIR}/a || atf_fail "file absent"
	echo "addition" >> ${TMPDIR}/target/${TMPDIR}/a
	atf_check \
		-o inline:"entry\naddition\n" \
		cat ${TMPDIR}/target/${TMPDIR}/a

	new_pkg "test" "test" "2"
	echo "entry 2" > a

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	echo "local: { url: file://${TMPDIR} }" > local.conf
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR} -r ${TMPDIR}/target upgrade -qy test

	atf_check \
		-o inline:"entry 2\naddition\n" \
		cat ${TMPDIR}/target/${TMPDIR}/a
}
