#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	messages

messages_body() {
	cat > test.ucl << EOF
name: "test"
origin: "test"
version: "5.20_3"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
message: [
	{ message: "Always print" },
	{ message: "package being removed", type: remove },
	{ message: "package being installed", type: install },
	{ message: "package is being upgraded", type: upgrade },
	{ message: "Upgrading from lower than 1.0", maximum_version: "1.0", type: upgrade },
	{ message: "Upgrading from higher than 1.0", minimum_version: "1.0", type: upgrade  },
	{ message: "Upgrading from >1.0 < 3.0", maximum_version: "3.0", minimum_version: "1.0", type: upgrade  }
]
EOF
	atf_check \
		-o match:".*Installing.*" \
		-o match:"^Always print.*" \
		-o match:"^package being installed.*" \
		pkg register -M test.ucl
	atf_check \
		-o match:"^package being removed.*" \
		pkg delete -y test

	cat << EOF > repo1.conf
local1: {
	url: file://${TMPDIR},
	enabled: true
}
EOF
	cat > test2.ucl << EOF
name: "test"
origin: "test"
version: "0.20_3"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
EOF
	atf_check -o ignore pkg register -M test2.ucl
	atf_check -o ignore pkg create -M test.ucl
	atf_check -o ignore pkg repo .
	atf_check -o match:"^Upgrading from lower than 1.0.*" \
	    -e match:".*load error: access repo file.*" \
	    pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -y
	atf_check -o ignore pkg delete -y test

	cat > test2.ucl << EOF
name: "test"
origin: "test"
version: "4.20_3"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
EOF
	atf_check -o ignore pkg register -M test2.ucl
	atf_check \
		-o match:"^Upgrading from higher than 1.0.*" \
	    pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -y
	atf_check -o ignore pkg delete -y test

	cat > test2.ucl << EOF
name: "test"
origin: "test"
version: "2.20_3"
arch: "*"
maintainer: "none"
prefix: "/usr/local"
www: "unknown"
comment: "need one"
desc: "also need one"
EOF
	atf_check -o ignore pkg register -M test2.ucl
	atf_check \
		-o match:"^Upgrading from >1.0 < 3.0.*" \
		-o match:"^Upgrading from higher than 1.0.*" \
		pkg -o REPOS_DIR="${TMPDIR}" -o PKG_CACHEDIR="${TMPDIR}" upgrade -y
OUTPUT='test-5.20_3:
Always:
Always print

On remove:
package being removed

On install:
package being installed

On upgrade:
package is being upgraded

On upgrade from test<1.0:
Upgrading from lower than 1.0

On upgrade from test>1.0:
Upgrading from higher than 1.0

On upgrade from test>1.0<3.0:
Upgrading from >1.0 < 3.0

'
	atf_check -o inline:"${OUTPUT}" pkg info -D test
}
