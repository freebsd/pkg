#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	multiple_providers

multiple_providers_body() {
	touch file

	cat << EOF > pkg1.ucl
name: test1
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
shlibs_provided [
	"lib1.so.6"
]
files: {
	${TMPDIR}/file: ""
}
EOF

	cat << EOF > pkg2.ucl
name: dep
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
shlibs_required [
	"lib1.so.6"
]
deps: {
	test1 {
		origin: test
		version: 1
	}
}
EOF

	for p in pkg1 pkg2; do
		atf_check \
			-o match:".*Installing.*\.\.\.$" \
			-e empty \
			-s exit:0 \
			pkg register -M ${p}.ucl
	done

	cat << EOF > pkg3.ucl
name: test1
origin: test
version: "1_1"
maintainer: test
categories: [test]
comment: a test
www: http://test
prefix: /usr/local
desc: <<EOD
Yet another test
EOD
shlibs_provided [
	"lib1.so.6"
]
files: {
	${TMPDIR}/file: ""
}
EOF

	cat << EOF > pkg4.ucl
name: test2
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
shlibs_provided [
	"lib1.so.6"
]
files: {
	${TMPDIR}/file: ""
}
EOF

	cat << EOF > pkg5.ucl
name: dep
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
shlibs_required [
	"lib1.so.6"
]
deps: {
	test2 {
		origin: test
		version: 1
	}
}
EOF

	for p in pkg3 pkg4 pkg5; do
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
}

