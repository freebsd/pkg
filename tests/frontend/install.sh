#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	metalog \
	reinstall \
	pre_script_fail \
	post_script_ignored \
	install_missing_dep \
	install_register_only \
	install_autoremove \
	install_autoremove_flag \
	install_suggest_clear_automatic \
	install_suggest_set_automatic \
	install_no_suggest_when_flag_matches \
	install_from_url

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

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /
	atf_check touch ${TMPDIR}/testfile1
	echo "@(root,wheel,640,) testfile1" > test.plist
	echo "test123" > ${TMPDIR}/testfile2
	echo "@(daemon,nobody,644,) testfile2" >> test.plist
	atf_check ln -s ${TMPDIR}/testfile1 ${TMPDIR}/testlink1
	echo "@ testlink1" >> test.plist
	ln ${TMPDIR}/testfile2 ${TMPDIR}/testhlink2
	echo "@ testhlink2" >> test.plist
	atf_check mkdir -p ${TMPDIR}/testdir1/foo/bar/baz
	atf_check mkdir -p ${TMPDIR}/testdir1/foo/bar/baz2
	echo "@dir testdir1/foo/bar/baz" >> test.plist
	echo "@dir testdir1/foo/bar/baz2" >> test.plist
	atf_check mkdir ${TMPDIR}/testdir2
	atf_check chmod 750 ${TMPDIR}/testdir2
	echo "@dir(daemon) testdir2" >> test.plist

	atf_check \
		-o ignore \
		pkg create -r ${TMPDIR} -M test.ucl -p test.plist

	atf_check \
		-o ignore \
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
		mkdir ${TMPDIR}/root

	atf_check \
		-o ignore \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o METALOG=${TMPDIR}/METALOG -r ${TMPDIR}/root install -y test

	atf_check \
		-o match:"./testfile1 type=file uname=root gname=wheel mode=640" \
		-o match:"./testfile2 type=file uname=daemon gname=nobody mode=644" \
		-o match:"./testlink1 type=link uname=root gname=wheel mode=755 link=${TMPDIR}/testfile1" \
		-o match:"./testhlink2 type=file uname=root gname=wheel mode=644" \
		-o match:"./testdir1 type=dir uname=root gname=wheel mode=755" \
		-o match:"./testdir1/foo type=dir uname=root gname=wheel mode=755" \
		-o match:"./testdir1/foo/bar type=dir uname=root gname=wheel mode=755" \
		-o match:"./testdir1/foo/bar/baz type=dir uname=root gname=wheel mode=755" \
		-o match:"./testdir2 type=dir uname=daemon gname=wheel mode=750" \
		cat ${TMPDIR}/METALOG
}

reinstall_body()
{
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1 /usr/local

	atf_check \
		-o ignore \
		pkg register -M test.ucl

	atf_check \
		-o ignore \
		pkg create -M test.ucl

	atf_check \
		-o ignore \
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
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" install -y test
}

pre_script_fail_body()
{
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
scripts: {
   pre-install: "exit 1"
}
EOF

	atf_check \
		-o ignore \
		pkg create -M test.ucl

	atf_check -o ignore \
		-e inline:"${PROGNAME}: PRE-INSTALL script failed\n" \
		-s exit:3 \
		pkg -o REPOS_DIR="/dev/null" install -y ${TMPDIR}/test-1.pkg
}

post_script_ignored_body()
{
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat << EOF >> test.ucl
scripts: {
   post-install: "exit 1"
}
EOF

	atf_check \
		-o ignore \
		pkg create -M test.ucl

	atf_check -o ignore \
		-e inline:"${PROGNAME}: POST-INSTALL script failed\n" \
		pkg -o REPOS_DIR="/dev/null" install -y ${TMPDIR}/test-1.pkg
}

install_missing_dep_body()
{
	test_setup

	# Create one package so we at least have a repo.
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg ${TMPDIR}/test test 1 /usr/local
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
		pkg -C "${TMPDIR}/pkg.conf" create -o ${TMPDIR} -M ${TMPDIR}/test.ucl

	atf_check \
		-o ignore \
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

install_register_only_body()
{
	test_setup

	touch file1
	mkdir dir
	touch dir/file2

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1" "${TMPDIR}"
	cat << EOF >> test.ucl
files: {
    ${TMPDIR}/file1: "",
    ${TMPDIR}/dir/file2: "",
}
EOF

	mkdir repoconf
	cat << EOF > repoconf/repo.conf
repo: {
	url: file:///$TMPDIR/repo,
	enabled: true
}
EOF

	mkdir repo

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl -o repo

	rm file1
	rm dir/file2
	rmdir dir

	ls
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo repo

	export REPOS_DIR="${TMPDIR}/repoconf"
	atf_check \
		-o ignore \
		-s exit:0 \
		pkg install -r repo -y --register-only test

	atf_check \
		-o inline:"0\n" \
		-e empty \
		pkg query "%a" test

	atf_check \
		-o ignore \
		-e ignore \
		-s exit:1 \
		test -f file1

	atf_check \
		-o ignore \
		-e ignore \
		-s exit:1 \
		test -d dir
}

install_autoremove_body() {
	# Pre-register: olddep (automatic), master depends on olddep
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "olddep" "olddep" "1"
	atf_check -o ignore pkg register -A -M olddep.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "master" "master" "1"
	cat << EOF >> master.ucl
deps: {
	olddep {
		origin: olddep,
		version: 1
	}
}
EOF
	atf_check -o ignore pkg register -M master.ucl

	# Create master v2 in repo (no longer depends on olddep)
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "master2" "master" "2"
	atf_check pkg create -M master2.ucl -o ./repo

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
AUTOREMOVE=YES
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check -o ignore pkg -C ./pkg.conf repo ./repo
	atf_check -o ignore pkg -C ./pkg.conf update -f

	# Install/upgrade master: olddep should be autoremoved
	atf_check \
		-o match:"Upgrading master" \
		-o match:"Deinstalling olddep" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y master

	atf_check -s exit:0 pkg info -e master
	atf_check -s exit:1 pkg info -e olddep
}

install_autoremove_flag_body() {
	# Same scenario but with --autoremove flag
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "olddep" "olddep" "1"
	atf_check -o ignore pkg register -A -M olddep.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "master" "master" "1"
	cat << EOF >> master.ucl
deps: {
	olddep {
		origin: olddep,
		version: 1
	}
}
EOF
	atf_check -o ignore pkg register -M master.ucl

	# Create master v2 in repo (no longer depends on olddep)
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "master2" "master" "2"
	atf_check pkg create -M master2.ucl -o ./repo

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check -o ignore pkg -C ./pkg.conf repo ./repo
	atf_check -o ignore pkg -C ./pkg.conf update -f

	# Install/upgrade master with --autoremove: olddep should be removed
	atf_check \
		-o match:"Upgrading master" \
		-o match:"Deinstalling olddep" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y --autoremove master

	atf_check -s exit:0 pkg info -e master
	atf_check -s exit:1 pkg info -e olddep
}

install_suggest_clear_automatic_body() {
	# pkg install foo when foo is already installed with automatic=1
	# should suggest clearing the automatic flag
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	# Register as automatic
	atf_check -o ignore pkg register -A -M test.ucl

	atf_check \
		-o inline:"1\n" \
		pkg query "%a" test

	# Create repo with same package
	atf_check pkg create -M test.ucl -o ./repo

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check -o ignore pkg -C ./pkg.conf repo ./repo
	atf_check -o ignore pkg -C ./pkg.conf update -f

	# pkg install -y test: should toggle automatic from 1 to 0
	atf_check \
		-o not-match:"already installed" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y test

	# Verify automatic flag was cleared
	atf_check \
		-o inline:"0\n" \
		pkg query "%a" test
}

install_suggest_set_automatic_body() {
	# pkg install -A foo when foo is already installed with automatic=0
	# should suggest setting the automatic flag
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	# Register as non-automatic
	atf_check -o ignore pkg register -M test.ucl

	atf_check \
		-o inline:"0\n" \
		pkg query "%a" test

	# Create repo with same package
	atf_check pkg create -M test.ucl -o ./repo

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check -o ignore pkg -C ./pkg.conf repo ./repo
	atf_check -o ignore pkg -C ./pkg.conf update -f

	# pkg install -Ay test: should toggle automatic from 0 to 1
	atf_check \
		-o not-match:"already installed" \
		-s exit:0 \
		pkg -C ./pkg.conf install -Ay test

	# Verify automatic flag was set
	atf_check \
		-o inline:"1\n" \
		pkg query "%a" test
}

install_no_suggest_when_flag_matches_body() {
	# pkg install foo when foo is already installed with automatic=0
	# should not suggest anything, just print the standard message
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	# Register as non-automatic
	atf_check -o ignore pkg register -M test.ucl

	atf_check \
		-o inline:"0\n" \
		pkg query "%a" test

	# Create repo with same package
	atf_check pkg create -M test.ucl -o ./repo

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check -o ignore pkg -C ./pkg.conf repo ./repo
	atf_check -o ignore pkg -C ./pkg.conf update -f

	# pkg install -y test: flag already matches, standard message
	atf_check \
		-o match:"already installed" \
		-o not-match:"Mark as" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y test

	# Flag should remain 0
	atf_check \
		-o inline:"0\n" \
		pkg query "%a" test
}

install_from_url_body() {
	# pkg install should accept file:// URLs
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"

	atf_check pkg create -M test.ucl -o ./repo

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check -o ignore pkg -C ./pkg.conf repo ./repo
	atf_check -o ignore pkg -C ./pkg.conf update -f

	# Install from a file:// URL
	atf_check \
		-o match:"Installing test" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y file://${TMPDIR}/repo/test-1.pkg

	atf_check -s exit:0 pkg info -e test
}
