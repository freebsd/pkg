#! /usr/bin/env atf-sh
. $(atf_get_srcdir)/test_environment.sh

tests_init \
	conflicts_multirepo \
	conflicts_multirepo_cycle

conflicts_multirepo_head() {
	atf_set "timeout" "40"
}

conflicts_multirepo_body() {
	touch a b c
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
	${TMPDIR}/b: "",
	${TMPDIR}/c: "",
}
EOF

	cat << EOF >> manifest3
name: test
origin: test
version: "1.1"
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
	${TMPDIR}/b: "",
}
EOF

	cat << EOF >> manifest4
name: test2
origin: test
version: "1.1"
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
	${TMPDIR}/c: "",
}
EOF
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M manifest

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M manifest2

	mkdir repo1
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest -o repo1/

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest2 -o repo1/

	mkdir repo2
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest3 -o repo2/

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest4 -o repo2/

	atf_check \
		-o inline:"Creating repository in repo1:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo repo1

	atf_check \
		-o inline:"Creating repository in repo2:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo repo2

	cat << EOF >> repo.conf
local1: {
	url: file:///${TMPDIR}/repo1,
	enabled: true
}
local2: {
	url: file:///${TMPDIR}/repo2,
	enabled:true
}
EOF

OUTPUT="Updating local1 repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
local1 repository update completed. 2 packages processed.
Updating local2 repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
local2 repository update completed. 2 packages processed.
All repositories are up to date.
Updating database digests format:  done
Checking for upgrades (2 candidates):  done
Processing candidates (2 candidates):  done
Checking integrity... done (2 conflicting)
  - test-1.1 [local2] conflicts with test2-1 [installed] on ${TMPDIR}/b
  - test-1.1 [local2] conflicts with test2-1 [local1] on ${TMPDIR}/b
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

Installed packages to be UPGRADED:
	test: 1 -> 1.1 [local2]
	test2: 1 -> 1.1 [local2]

Number of packages to be upgraded: 2
${JAILED}[1/2] Upgrading test2 from 1 to 1.1...
${JAILED}[1/2] Extracting test2-1.1:  done
${JAILED}[2/2] Upgrading test from 1 to 1.1...
${JAILED}[2/2] Extracting test-1.1:  done
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-s exit:0 \
		pkg -o CONSERVATIVE_UPGRADE=no -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -y
}

conflicts_multirepo_cycle_head() {
	atf_set "timeout" "40"
}

# This case is the same as conflicts_multirepo but with test2 depending on test.
# This added dependency creates a cycle in the job scheduling graph which requires
# the upgrade job for test2 to be split.
conflicts_multirepo_cycle_body() {
	touch a b c
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
deps: {
	test: {
		origin: "test",
		version: "1"
	}
}
files: {
	${TMPDIR}/b: "",
	${TMPDIR}/c: "",
}
EOF

	cat << EOF >> manifest3
name: test
origin: test
version: "1.1"
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
	${TMPDIR}/b: "",
}
EOF

	cat << EOF >> manifest4
name: test2
origin: test
version: "1.1"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /
abi = "*";
desc: <<EOD
Yet another test
EOD
deps: {
	test: {
		origin: "test",
		version: "1.1"
	}
}
files: {
	${TMPDIR}/c: "",
}
EOF
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M manifest

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M manifest2

	mkdir repo1
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest -o repo1/

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest2 -o repo1/

	mkdir repo2
	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest3 -o repo2/

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M manifest4 -o repo2/

	atf_check \
		-o inline:"Creating repository in repo1:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo repo1

	atf_check \
		-o inline:"Creating repository in repo2:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo repo2

	cat << EOF >> repo.conf
local1: {
	url: file:///${TMPDIR}/repo1,
	enabled: true
}
local2: {
	url: file:///${TMPDIR}/repo2,
	enabled:true
}
EOF

OUTPUT="Updating local1 repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
local1 repository update completed. 2 packages processed.
Updating local2 repository catalogue...
${JAILED}Fetching meta.conf:  done
${JAILED}Fetching data.pkg:  done
Processing entries:  done
local2 repository update completed. 2 packages processed.
All repositories are up to date.
Updating database digests format:  done
Checking for upgrades (2 candidates):  done
Processing candidates (2 candidates):  done
Checking integrity... done (2 conflicting)
  - test-1.1 [local2] conflicts with test2-1 [installed] on ${TMPDIR}/b
  - test-1.1 [local2] conflicts with test2-1 [local1] on ${TMPDIR}/b
Checking integrity... done (0 conflicting)
The following 2 package(s) will be affected (of 0 checked):

Installed packages to be UPGRADED:
	test: 1 -> 1.1 [local2]
	test2: 1 -> 1.1 [local2]

Number of packages to be upgraded: 2
${JAILED}[1/2] Upgrading test2 from 1 to 1.1...
${JAILED}[1/2] Extracting test2-1.1:  done
${JAILED}[2/2] Upgrading test from 1 to 1.1...
${JAILED}[2/2] Extracting test-1.1:  done
"
	atf_check \
		-o inline:"${OUTPUT}" \
		-s exit:0 \
		pkg -o CONSERVATIVE_UPGRADE=no -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -y
}
