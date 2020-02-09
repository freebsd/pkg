#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	config \
	config_fileexist \
	config_filenotexist \
	config_fileexist_notinpkg \
	config_hardlink \
	config_morecomplicated

config_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
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

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	mkdir reposconf
	echo "local: { url: file://${TMPDIR} }" > reposconf/local.conf
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target upgrade -qy test

	atf_check \
		-o inline:"entry 2\naddition\n" \
		cat ${TMPDIR}/target/${TMPDIR}/a
}

config_fileexist_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
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

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a
	echo "@config ${TMPDIR}/a" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	mkdir reposconf
	echo "local: { url: file://${TMPDIR} }" > reposconf/local.conf
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target upgrade -qy test

	test -f ${TMPDIR}/target/${TMPDIR}/a.pkgnew || atf_fail "file overwritten when it should not have"
}

config_filenotexist_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
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

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a
	echo "@config ${TMPDIR}/a" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	mkdir reposconf
	echo "local: { url: file://${TMPDIR} }" > reposconf/local.conf
	rm ${TMPDIR}/target/${TMPDIR}/a
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target upgrade -qy test

	test ! -f ${TMPDIR}/target/${TMPDIR}/a.pkgnew || atf_fail "redundant pkgnew left hanging"
	test -f ${TMPDIR}/target/${TMPDIR}/a || atf_fail "config file not installed"
}

config_hardlink_body()
{
	# Create a pkg
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1.0"
	echo "line 1" > a
	echo "line 2" >> a
	ln a b
	echo "@config /a" > plist
	echo "/b" >> plist
	atf_check \
		-o empty \
		-e empty \
		pkg create -M test.ucl -p plist -r .
	atf_check -o ignore pkg repo .
	mkdir reposconf
	echo "local: { url: file://${TMPDIR} }" > reposconf/local.conf
	mkdir ${TMPDIR}/target

	# Install the pkg
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target install -qy test
	rm *.txz

	# Modify the local config
	echo "line 1a" > target/a
	echo "line 2" >> target/a

	# Create an updated pkg
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1.1"
	echo "line 1" > a
	echo "line 2" >> a
	echo "@config /a" > plist
	echo "/b" >> plist
	atf_check \
		-o empty \
		-e empty \
		pkg create -M test.ucl -p plist -r .
	atf_check -o ignore pkg repo .
	atf_check -e ignore -o ignore pkg -o REPOS_DIR=${TMPDIR}/reposconf update -f

	# Upgrade
	atf_check \
		-o ignore \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target upgrade -y

	atf_check \
		-o match:"test-1.1*" \
		pkg -r ${TMPDIR}/target info

}

config_fileexist_notinpkg_body()
{
	mkdir -p ${TMPDIR}/target/${TMPDIR}
	echo "entry" > ${TMPDIR}/target/${TMPDIR}/a
	unset PKG_DBDIR

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a
	echo "@config ${TMPDIR}/a" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	mkdir reposconf
	echo "local: { url: file://${TMPDIR} }" > reposconf/local.conf
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target install -qy test

	test -f ${TMPDIR}/target/${TMPDIR}/a.pkgsave || atf_fail "file overwritten when it should not have"
}

config_morecomplicated_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	echo "entry1" > test.config
	echo "entry3" >> test.config
	echo "@config ${TMPDIR}/test.config" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	atf_check \
		-o match:"^config" \
		pkg info -R --raw-format ucl -F ${TMPDIR}/test-1.txz

	mkdir ${TMPDIR}/target
	unset PKG_DBDIR
	atf_check \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy ${TMPDIR}/test-1.txz
	test -f ${TMPDIR}/target/${TMPDIR}/test.config || atf_fail "file absent"

	atf_check \
		-o inline:"entry1\nentry3\n" \
		cat ${TMPDIR}/target/${TMPDIR}/test.config

	echo "entry4" >> ${TMPDIR}/target/${TMPDIR}/test.config
	atf_check \
		-o inline:"entry1\nentry3\nentry4\n" \
		cat ${TMPDIR}/target/${TMPDIR}/test.config

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry1" > test.config
	echo "entry2" >> test.config
	echo "entry3" >> test.config

	atf_check \
		pkg create -M test.ucl -p plist

	atf_check \
		-o ignore \
		pkg repo .

	mkdir reposconf
	echo "local: { url: file://${TMPDIR} }" > reposconf/local.conf
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target upgrade -qy test

	atf_check \
		-o inline:"entry1\nentry2\nentry3\nentry4\n" \
		cat ${TMPDIR}/target/${TMPDIR}/test.config
}
