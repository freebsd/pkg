#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	package_split

package_split_body() {
	touch file1
	touch file2

	cat << EOF > pkg1.ucl
name: test
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
files: {
	${TMPDIR}/file1: "",
	${TMPDIR}/file2: "",
}
EOF

	cat << EOF > dep1.ucl
name: master
origin: test
version: 1
maintainer: test
categories: [test]
www: http://test
comment: a test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
deps: {
	test {
		origin: test,
		version: 1
	}
}
EOF

cat << EOF > pkg2.ucl
name: sub-test
origin: test
version: 1
maintainer: test
categories: [test]
www: http://test
comment: a test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
files: {
	${TMPDIR}/file1: "",
}
EOF

cat << EOF > pkg3.ucl
name: sub-test2
origin: test
version: 1
maintainer: test
categories: [test]
www: http://test
comment: a test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
files: {
	${TMPDIR}/file2: "",
}
EOF

	cat << EOF > pkg4.ucl
name: test
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
deps: {
	sub-test: {
		origin: test,
		version: 1
	},
	sub-test2: {
		origin: test,
		version: 1
	}
}
EOF

	for p in pkg1 dep1; do
		atf_check \
		    -o match:".*Installing.*\.\.\.$" \
		    -e empty \
		    -s exit:0 \
		    pkg register -M ${p}.ucl
	done

	for p in dep1 pkg2 pkg3 pkg4; do
		atf_check \
		    -o ignore \
		    -e empty \
		    -s exit:0 \
		    pkg create -M ./${p}.ucl
	done

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
	    -o ignore \
	    -e match:".*load error: access repo file.*" \
	    -s exit:0 \
	    pkg -o REPOS_DIR="$TMPDIR" -o PKG_CACHEDIR="$TMPDIR" upgrade -y

	test -f file1 || atf_fail "file1 is not present"
}
