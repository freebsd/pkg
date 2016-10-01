#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
CLEANUP=chflags_schg
tests_init \
	basic \
	basic_dirs \
	setuid \
	setuid_hardlinks \
	chflags \
	chflags_schg \
	symlinks

basic_body()
{
	echo "test" > a
	new_pkg "test" "test" "1" || atf_fail "plop"
cat << EOF >> test.ucl
files = {
	${TMPDIR}/a: ""
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qy \
			${TMPDIR}/test-1.txz

OUTPUT="${TMPDIR}/target/local.sqlite
${TMPDIR}/target${TMPDIR}/a
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		-s exit:0 \
		find ${TMPDIR}/target -type f -print | sort

	echo "test2" > a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

# check no leftovers during upgrades/reinstallation
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.txz

	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		-s exit:0 \
		find ${TMPDIR}/target -type f -print | sort

}

basic_dirs_body()
{
	mkdir ${TMPDIR}/plop
	new_pkg "test" "test" "1" || atf_fail "fail to create the ucl file"
cat << EOF >> test.ucl
directories = {
	${TMPDIR}/plop: y
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.txz

	test -d ${TMPDIR}/target${TMPDIR}/plop || atf_fail "directory not extracted"
}

setuid_body()
{
	touch ${TMPDIR}/a
	chmod 04554 ${TMPDIR}/a || atf_fail "Fail to chmod"
	new_pkg "test" "test" "1" || atf_fail "fail to create the ucl file"
	cat << EOF >> test.ucl
files = {
	${TMPDIR}/a = ""
}
EOF
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o match:"^-r-sr-xr-- " \
		-e ignore \
		tar tvf ${TMPDIR}/test-1.txz

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.txz

	atf_check \
		-o match:"^-r-sr-xr-- " \
		-e empty \
		-s exit:0 \
		ls -l ${TMPDIR}/target${TMPDIR}/a
}

setuid_hardlinks_body()
{
	touch ${TMPDIR}/a
	ln ${TMPDIR}/a ${TMPDIR}/b
	chmod 04554 ${TMPDIR}/a || atf_fail "Fail to chmod"
	chmod 04554 ${TMPDIR}/b || atf_fail "Fail to chmod"
	new_pkg "test" "test" "1" || atf_fail "fail to create the ucl file"
	cat << EOF >> test.ucl
files = {
	${TMPDIR}/a = ""
	${TMPDIR}/b = ""
}
EOF
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o match:"^-r-sr-xr--.*a$" \
		-o match:"^hr-sr-xr--.*a$" \
		-e ignore \
		tar tvf ${TMPDIR}/test-1.txz

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null  -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.txz

	atf_check \
		-o match:"^-r-sr-xr-- " \
		-e empty \
		-s exit:0 \
		ls -l ${TMPDIR}/target${TMPDIR}/a

	atf_check \
		-o match:"^-r-sr-xr-- " \
		-e empty \
		-s exit:0 \
		ls -l ${TMPDIR}/target${TMPDIR}/b
}

chflags_body()
{
	test -x /bin/chflags || atf_skip "Requires chflags"
	# use nodump as it is the only one supported as user, by zfs and by
	# libarchive
	touch ${TMPDIR}/a
	new_pkg "test" "test" "1" || atf_fail "fail to create the ucl file"
	echo "@(,,,nodump) ${TMPDIR}/a" > test.plist
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl -p test.plist

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
		    ${TMPDIR}/test-1.txz

	atf_check \
		-o match:"nodump" \
		-e empty \
		-s exit:0 \
		ls -ol ${TMPDIR}/target${TMPDIR}/a

}

chflags_schg_body()
{
	test -x /bin/chflags || atf_skip "Requires chflags"
	test $(id -u) = 0 || atf_skip "Can only be run as root"

	touch ${TMPDIR}/a
	new_pkg "test" "test" "1" || atf_fail "fail to create the ucl file"
	echo "@(root,wheel,,schg) ${TMPDIR}/a" > test.plist
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl -p test.plist

	mkdir ${TMPDIR}/target
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.txz

	atf_check \
		-o match:"schg" \
		-e empty \
		-s exit:0 \
		ls -ol ${TMPDIR}/target${TMPDIR}/a

	# reinstall to for removal
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.txz
}

chflags_schg_cleanup()
{
	test -x /bin/chflags || atf_skip "Requires chflags"
	chflags -R noschg ${TMPDIR}
}

symlinks_body()
{
	new_pkg "test" "test" "1" || atf_fail "fail to create the ucl file"
	cat << EOF >> test.ucl
files: {
${TMPDIR}/a = "";
}
EOF

	ln -sf nothing a
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	mkdir ${TMPDIR}/target

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
			${TMPDIR}/test-1.txz
}
