#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	metalog \
	reinstall \
	pre_script_fail \
	post_script_ignored \
	install_missing_dep

test_setup()
{
	# Do a local config to avoid permissions-on-system-db errors.
        cat > ${TMPDIR}/pkg.conf << EOF
PKG_CACHEDIR=${TMPDIR}/cache
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[
	${TMPDIR}/reposconf
]
repositories: {
        local: { url : file://${TMPDIR} }
}
EOF
	mkdir -p ${TMPDIR}/reposconf
	cat << EOF > ${TMPDIR}/reposconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF
}

metalog_body()
{
        atf_skip_on Linux Test fails on Linux

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 / || atf_fail "Failed to create the ucl file"
	touch ${TMPDIR}/testfile1 || atf_fail "Failed to create the temp file"
	echo "@(root,wheel,640,) testfile1" > test.plist
	echo "test123" > ${TMPDIR}/testfile2 || atf_fail "Failed to create the temp file"
	echo "@(daemon,nobody,644,) testfile2" >> test.plist
	ln -s ${TMPDIR}/testfile1 ${TMPDIR}/testlink1
	echo "@ testlink1" >> test.plist
	ln ${TMPDIR}/testfile2 ${TMPDIR}/testhlink2
	echo "@ testhlink2" >> test.plist
	mkdir ${TMPDIR}/testdir1  || atf_fail "Failed to create the temp dir"
	echo "@dir testdir1" >> test.plist
	mkdir ${TMPDIR}/testdir2  || atf_fail "Failed to create the temp dir"
	chmod 750 ${TMPDIR}/testdir2 || atf_fail "Failed to chmod the temp dir"
	echo "@dir(daemon) testdir2" >> test.plist

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -r ${TMPDIR} -M test.ucl -p test.plist

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		mkdir ${TMPDIR}/root

	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o METALOG=${TMPDIR}/METALOG -r ${TMPDIR}/root install -y test

	atf_check \
		-o match:"./testfile1 type=file uname=root gname=wheel mode=640" \
		-o match:"./testfile2 type=file uname=daemon gname=nobody mode=644" \
		-o match:"./testlink1 type=link uname=root gname=wheel mode=755 link=${TMPDIR}/testfile1" \
		-o match:"./testhlink2 type=file uname=root gname=wheel mode=644" \
		-o match:"./testdir1 type=dir uname=root gname=wheel mode=755" \
		-o match:"./testdir2 type=dir uname=daemon gname=wheel mode=750" \
		-e empty \
		-s exit:0 \
		cat ${TMPDIR}/METALOG
}

reinstall_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /usr/local

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y test
}

pre_script_fail_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
scripts: {
   pre-install: "exit 1"
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check -o ignore \
		-e inline:"${PROGNAME}: PRE-INSTALL script failed\n" \
		-s exit:3 \
		pkg -o REPOS_DIR="/dev/null" install -y ${TMPDIR}/test-1.pkg
}

post_script_ignored_body()
{
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
scripts: {
   post-install: "exit 1"
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check -o ignore \
		-e inline:"${PROGNAME}: POST-INSTALL script failed\n" \
		-s exit:0 \
		pkg -o REPOS_DIR="/dev/null" install -y ${TMPDIR}/test-1.pkg
}

install_missing_dep_body()
{
	test_setup

	# Create one package so we at least have a repo.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg ${TMPDIR}/test test 1 /usr/local
	cat << EOF >> ${TMPDIR}/test.ucl
deps: {
	b: {
		origin: "wedontcare",
		version: "1"
	}
}
EOF
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C "${TMPDIR}/pkg.conf" create -o ${TMPDIR} -M ${TMPDIR}/test.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg  -C "${TMPDIR}/pkg.conf" repo ${TMPDIR}

	mkdir -p ${TMPDIR}/reposconf
	cat << EOF > ${TMPDIR}/reposconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o ignore \
		-e not-empty \
		-s not-exit:0 \
		pkg -C "${TMPDIR}/pkg.conf" install -y test
}
