#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	config \
	config_fileexist \
	config_fileexist_notinpkg \
	config_morecomplicated

config_body()
{
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
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

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	echo "local: { url: file://${TMPDIR} }" > local.conf
	atf_check \
		-e match:".*load error: access repo file.*" \
		pkg -o REPOS_DIR=${TMPDIR} -r ${TMPDIR}/target upgrade -qy test

	atf_check \
		-o inline:"entry 2\naddition\n" \
		cat ${TMPDIR}/target/${TMPDIR}/a
}

config_fileexist_body()
{
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	echo "${TMPDIR}/a" > plist

	echo "entry" > a

	atf_check \
		pkg create -M test.ucl -p plist

	mkdir ${TMPDIR}/target
	unset PKG_DBDIR
	atf_check \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy ${TMPDIR}/test-1.txz
	test -f ${TMPDIR}/target/${TMPDIR}/a || atf_fail "file absent"
	echo "addition" >> ${TMPDIR}/target/${TMPDIR}/a
	atf_check \
		-o inline:"entry\naddition\n" \
		cat ${TMPDIR}/target/${TMPDIR}/a

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a
	echo "@config ${TMPDIR}/a" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	echo "local: { url: file://${TMPDIR} }" > local.conf
	atf_check \
		-e match:".*load error: access repo file.*" \
		pkg -o REPOS_DIR=${TMPDIR} -r ${TMPDIR}/target upgrade -qy test

	test -f ${TMPDIR}/target/${TMPDIR}/a.pkgnew || atf_fail "file overwritten when it should not have"
}

config_fileexist_notinpkg_body()
{
	mkdir -p ${TMPDIR}/target/${TMPDIR}
	echo "entry" > ${TMPDIR}/target/${TMPDIR}/a
	unset PKG_DBDIR

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a
	echo "@config ${TMPDIR}/a" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	echo "local: { url: file://${TMPDIR} }" > local.conf
	atf_check \
		-e match:".*load error: access repo file.*" \
		pkg -o REPOS_DIR=${TMPDIR} -r ${TMPDIR}/target install -qy test

	test -f ${TMPDIR}/target/${TMPDIR}/a.pkgnew || atf_fail "file overwritten when it should not have"
}

config_morecomplicated_body()
{
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	echo "entry1" > test.conf
	echo "entry3" >> test.conf
	echo "@config ${TMPDIR}/test.conf" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	atf_check \
		-o match:"^config" \
		pkg info -R --raw-format ucl -F ${TMPDIR}/test-1.txz

	mkdir ${TMPDIR}/target
	unset PKG_DBDIR
	atf_check \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy ${TMPDIR}/test-1.txz
	test -f ${TMPDIR}/target/${TMPDIR}/test.conf || atf_fail "file absent"

	atf_check \
		-o inline:"entry1\nentry3\n" \
		cat ${TMPDIR}/target/${TMPDIR}/test.conf

	echo "entry4" >> ${TMPDIR}/target/${TMPDIR}/test.conf
	atf_check \
		-o inline:"entry1\nentry3\nentry4\n" \
		cat ${TMPDIR}/target/${TMPDIR}/test.conf

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry1" > test.conf
	echo "entry2" >> test.conf
	echo "entry3" >> test.conf

	atf_check \
		pkg create -M test.ucl -p plist

	atf_check \
		-o ignore \
		pkg repo .

	echo "local: { url: file://${TMPDIR} }" > local.conf
	atf_check \
		-e match:".*load error: access repo file.*" \
		pkg -o REPOS_DIR=${TMPDIR} -r ${TMPDIR}/target upgrade -qy test

	atf_check \
		-o inline:"entry1\nentry2\nentry3\nentry4\n" \
		cat ${TMPDIR}/target/${TMPDIR}/test.conf
}
