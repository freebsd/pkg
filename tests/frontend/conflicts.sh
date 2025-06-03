#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	more_complex_choice \
	complex_conflicts \
	fileexists_notinpkg \
	find_conflicts

# install foo
# foo depends on bar-1.0
# foo is upgraded to new dep on bar1-1.0 & bar is updated to 2.0
# bar1 and bar conflict with each other
complex_conflicts_body() {
	echo "bar-1.0" > file1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 1.0 "${TMPDIR}"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/file1: "",
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./bar.ucl -o ./repo/

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1.0 "${TMPDIR}"
	cat << EOF >> foo.ucl
deps: {
	bar: {
		origin: "bar",
		version: "1.0"
	}
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./foo.ucl -o ./repo/

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check \
		-o inline:"Creating repository in ./repo:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf repo ./repo

	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -C ./pkg.conf update -f

	atf_check \
		-o match:"Installing foo-1\.0" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y foo

	# Upgrade bar
	rm -fr repo
	echo "bar-2.0" > file1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 2.0 "${TMPDIR}"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/file1: "",
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./bar.ucl -o ./repo/

	# Create bar1-1.1
	echo "bar-1.1" > file1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar1 bar1 1.1 "${TMPDIR}"
	cat << EOF >> bar1.ucl
files: {
	${TMPDIR}/file1: "",
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./bar1.ucl -o ./repo/

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1.0_1 "${TMPDIR}"
	cat << EOF >> foo.ucl
deps: {
	bar1: {
		origin: "bar1",
		version: "1.1"
	}
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./foo.ucl -o ./repo/

	atf_check \
		-o inline:"Creating repository in ./repo:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf repo ./repo

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf update -f

OUTPUT="Updating local repository catalogue...
local repository is up to date.
All repositories are up to date.
Checking for upgrades (2 candidates):  done
Processing candidates (2 candidates):  done
Checking integrity... done (2 conflicting)
  - bar1-1.1 conflicts with bar-1.0 on ${TMPDIR}/file1
  - bar1-1.1 conflicts with bar-2.0 on ${TMPDIR}/file1
Cannot solve problem using SAT solver, trying another plan
Checking integrity... done (0 conflicting)
The following 3 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	bar1: 1.1

Installed packages to be UPGRADED:
	foo: 1.0 -> 1.0_1

Installed packages to be REMOVED:
	bar: 1.0

Number of packages to be removed: 1
Number of packages to be installed: 1
Number of packages to be upgraded: 1
[1/3] Upgrading foo from 1.0 to 1.0_1...
[2/3] Deinstalling bar-1.0...
[2/3] Deleting files for bar-1.0:  done
[3/3] Installing bar1-1.1...
[3/3] Extracting bar1-1.1:  done
"

	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf upgrade -y

	atf_check \
		-o match:"foo-1.0_1" \
		-o match:"bar1-1.1" \
		-o not-match:"bar-2.0" \
		-e empty \
		-s exit:0 \
		pkg info
}

# install foo
# foo depends on bar-1.0
# foo is upgraded to new dep on bar1-1.0 & bar is updated to 2.0
# other still depends on bar1-1.0
# bar1 and bar conflict with each other
# install arp, which also depends on new bar1-1.0
#
# This also tests that other is deleted before the first half of the split
# upgrade foo job and arp is installed after the second half despite the default
# lexicographical job ordering when there are no hard ordering requirements.
# This behavior minimizes the distance between the split halves of an upgrade in
# the execution order.
more_complex_choice_body()
{
	echo "bar-1.0" > file1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 1.0 "${TMPDIR}"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/file1: "",
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./bar.ucl -o ./repo/

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1.0 "${TMPDIR}"
	cat << EOF >> foo.ucl
deps: {
	bar: {
		origin: "bar",
		version: "1.0"
	}
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./foo.ucl -o ./repo/

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg other other 1.0 "${TMPDIR}"
	cat << EOF >> other.ucl
deps: {
	bar: {
		origin: "bar",
		version: "1.0"
	}
}
EOF

	atf_check pkg create -M ./other.ucl -o ./repo/

	cat << EOF > pkg.conf
PKG_DBDIR=${TMPDIR}
REPOS_DIR=[]
repositories: {
	local: { url : file://${TMPDIR}/repo }
}
EOF

	atf_check \
		-o inline:"Creating repository in ./repo:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf repo ./repo

	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -C ./pkg.conf update -f

	atf_check \
		-o match:"Installing foo-1\.0" \
		-o match:"Installing other-1\.0" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y foo other

	# Upgrade bar
	rm -fr repo
	echo "bar-2.0" > file1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 2.0 "${TMPDIR}"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/file1: "",
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./bar.ucl -o ./repo/

	# Create bar1-1.1
	echo "bar-1.1" > file1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar1 bar1 1.1 "${TMPDIR}"
	cat << EOF >> bar1.ucl
files: {
	${TMPDIR}/file1: "",
}
EOF

	# Upgrade foo
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./bar1.ucl -o ./repo/

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1.0_1 "${TMPDIR}"
	cat << EOF >> foo.ucl
deps: {
	bar1: {
		origin: "bar1",
		version: "1.1"
	}
}
EOF

	# Create arp
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./foo.ucl -o ./repo/

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg arp arp 1.0_1 "${TMPDIR}"
	cat << EOF >> arp.ucl
deps: {
	bar1: {
		origin: "bar1",
		version: "1.1"
	}
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M ./arp.ucl -o ./repo/

	atf_check \
		-o inline:"Creating repository in ./repo:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf repo ./repo

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf update -f

OUTPUT="Updating local repository catalogue...
local repository is up to date.
All repositories are up to date.
Checking integrity... done (1 conflicting)
  - bar1-1.1 conflicts with bar-1.0 on ${TMPDIR}/file1
Checking integrity... done (0 conflicting)
The following 5 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	arp: 1.0_1
	bar1: 1.1

Installed packages to be UPGRADED:
	foo: 1.0 -> 1.0_1

Installed packages to be REMOVED:
	bar: 1.0
	other: 1.0

Number of packages to be removed: 2
Number of packages to be installed: 2
Number of packages to be upgraded: 1
[1/5] Installing arp-1.0_1...
[2/5] Upgrading foo from 1.0 to 1.0_1...
[3/5] Deinstalling other-1.0...
[4/5] Deinstalling bar-1.0...
[4/5] Deleting files for bar-1.0:  done
[5/5] Installing bar1-1.1...
[5/5] Extracting bar1-1.1:  done
"

	atf_check \
		-o inline:"${OUTPUT}" \
		-e empty \
		-s exit:0 \
		pkg -C ./pkg.conf install -y arp

	atf_check \
		-o match:"foo-1.0_1" \
		-o match:"bar1-1.1" \
		-o not-match:"other-1.0" \
		-o not-match:"bar-2.0" \
		-e empty \
		-s exit:0 \
		pkg info
}

fileexists_notinpkg_body()
{
	mkdir -p ${TMPDIR}/target/${TMPDIR}
	echo "entry" > ${TMPDIR}/target/${TMPDIR}/a
	unset PKG_DBDIR

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"
	echo "entry 2" > a
	echo "${TMPDIR}/a" > plist

	atf_check \
		pkg create -M test.ucl -p plist

	pkg repo .
	mkdir reposconf
	echo "local: { url: file://${TMPDIR} }" > reposconf/local.conf
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target install -qy test

	test -f ${TMPDIR}/target/${TMPDIR}/a.pkgsave || atf_fail "file not saved when it should have"

	# Test the nominal situation just in case
	rm -f ${TMPDIR}/target/${TMPDIR}/a.pkgsave
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target install -qyf test
	test -f ${TMPDIR}/target/${TMPDIR}/a.pkgsave && atf_fail "file saved when it should not have"

	# Test if both files are identical
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target delete -qyf test
	echo "entry 2" > ${TMPDIR}/target/${TMPDIR}/a
	atf_check \
		pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target install -qyf test
	test -f ${TMPDIR}/target/${TMPDIR}/a.pkgsave && atf_fail "file saved when it should not have"
	return 0
}

find_conflicts_body() {
	touch a
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_manifest test 1 /
	cat << EOF >> +MANIFEST
files: {
	${TMPDIR}/a: "",
}
EOF
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M +MANIFEST

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_manifest test2 1 /
	cat << EOF >> +MANIFEST
files: {
	${TMPDIR}/a: "",
}
EOF

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M +MANIFEST -o .

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir reposconf
	cat << EOF >> reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

OUTPUT="Updating local repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
local repository update completed. 1 packages processed.
All repositories are up to date.
Updating database digests format:  done
Checking integrity... done (1 conflicting)
  - test2-1 conflicts with test-1 on ${TMPDIR}/a
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

New packages to be INSTALLED:
	test2: 1

Installed packages to be REMOVED:
	test: 1

Number of packages to be removed: 1
Number of packages to be installed: 1
${JAILED}[1/2] Deinstalling test-1...
${JAILED}[1/2] Deleting files for test-1:  done
${JAILED}[2/2] Installing test2-1...
${JAILED}[2/2] Extracting test2-1:  done
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" install -y test2-1
}
