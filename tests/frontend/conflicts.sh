#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	complex_conflicts \
	find_conflicts

# install foo
# foo depends on bar-1.0
# foo is upgraded to new dep on bar1-1.0 & bar is updated to 2.0
# bar1 and bar conflict with each other
complex_conflicts_body() {
	echo "bar-1.0" > file1
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 1.0 "${TMPDIR}"
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

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1.0 "${TMPDIR}"
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
		-e match:".*load error: access repo file.*" \
		-s exit:0 \
		pkg -C ./pkg.conf update -f

	atf_check \
		-o match:"Installing foo-1\.0" \
		-s exit:0 \
		pkg -C ./pkg.conf install -y foo

	# Upgrade bar
	rm -fr repo
	echo "bar-2.0" > file1
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 2.0 "${TMPDIR}"
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
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg bar1 bar1 1.1 "${TMPDIR}"
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

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1.0_1 "${TMPDIR}"
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
  - bar1-1.1 conflicts with bar-2.0 on ${TMPDIR}/file1
  - bar1-1.1 conflicts with bar-1.0 on ${TMPDIR}/file1
Cannot solve problem using SAT solver, trying another plan
Checking integrity... done (0 conflicting)
The following 3 package(s) will be affected (of 0 checked):

Installed packages to be REMOVED:
	bar-1.0

New packages to be INSTALLED:
	bar1: 1.1

Installed packages to be UPGRADED:
	foo: 1.0 -> 1.0_1

Number of packages to be removed: 1
Number of packages to be installed: 1
Number of packages to be upgraded: 1
[1/3] Deinstalling bar-1.0...
[1/3] Deleting files for bar-1.0:  done
[2/3] Installing bar1-1.1...
[2/3] Extracting bar1-1.1:  done
[3/3] Upgrading foo from 1.0 to 1.0_1...
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

find_conflicts_body() {
	touch a
	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_manifest test 1 /
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

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_manifest test2 1 /
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

	cat << EOF >> repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

OUTPUT="Updating local repository catalogue...
${JAILED}Fetching meta.txz:  done
${JAILED}Fetching packagesite.txz:  done
Processing entries:  done
local repository update completed. 1 packages processed.
All repositories are up to date.
Updating database digests format:  done
Checking integrity... done (1 conflicting)
  - test2-1 conflicts with test-1 on ${TMPDIR}/a
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

Installed packages to be REMOVED:
	test-1

New packages to be INSTALLED:
	test2: 1

Number of packages to be removed: 1
Number of packages to be installed: 1
${JAILED}[1/2] Deinstalling test-1...
${JAILED}[1/2] Deleting files for test-1:  done
${JAILED}[2/2] Installing test2-1...
${JAILED}[2/2] Extracting test2-1:  done
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-e match:".*load error: access repo file.*" \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" install -y test2-1
}
