#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	reinstall

reinstall_body()
{
	cat << EOF > test.ucl
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
EOF


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

	cat << EOF > repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}" install -y test
}
