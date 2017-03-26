#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	find_conflicts

find_conflicts_body() {
	touch a
	cat << EOF >> manifest
name: test
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
abi = "*";
desc: <<EOD
Yet another test
EOD
files: {
	${TMPDIR}/a: "",
}
EOF

	cat << EOF >> manifest2
name: test2
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
abi = "*";
desc: <<EOD
Yet another test
EOD
files: {
	${TMPDIR}/a: "",
}
EOF
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M manifest

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest2 -o .

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
