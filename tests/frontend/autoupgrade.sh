#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	autoupgrade \
	autoupgrade_multirepo

autoupgrade_body() {

	cat << EOF > pkg1.ucl
name: pkg
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
EOF

	cat << EOF > pkg2.ucl
name: pkg
origin: test
version: 1_1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M pkg1.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg2.ucl

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	cat << EOF > repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o match:".*New version of pkg detected.*" \
		-e match:".*load error: access repo file.*" \
		-s exit:1 \
		pkg -o REPOS_DIR="$TMPDIR" -o PKG_CACHEDIR="$TMPDIR" upgrade -n
}

autoupgrade_multirepo_head() {
	atf_set "timeout" 40
}

autoupgrade_multirepo_body() {

	cat << EOF > pkg1.ucl
name: pkg
origin: test
version: 1
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
EOF

	cat << EOF > pkg2.ucl
name: pkg
origin: test
version: "1.1"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -M pkg1.ucl

	mkdir repo1 repo2

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg1.ucl -o repo1

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg create -M ./pkg2.ucl -o repo2

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo repo1

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo repo2

	cat << EOF > repo.conf
repo1: {
	url: file:///$TMPDIR/repo1,
	enabled: true
}
repo2: {
	url: file:///$TMPDIR/repo2,
	enabled: true
}
EOF

	export REPOS_DIR="${TMPDIR}"
	atf_check \
		-o ignore \
		-e match:".*load error: access repo file.*" \
		-s exit:0 \
		pkg install -r repo1 -fy pkg-1

	atf_check \
		-o match:".*New version of pkg detected.*" \
		-e match:".*load error: access repo file.*" \
		-s exit:0 \
		pkg upgrade -y

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg upgrade -y
}

