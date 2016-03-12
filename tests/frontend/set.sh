#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	set_automatic \
	set_change_name \
	set_change_origin \
	set_vital

initialize_pkg() {
	cat << EOF > test.ucl
name: test
origin: origin/test
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
EOF

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -t -M test.ucl
}

set_automatic_body() {
	initialize_pkg

	atf_check \
		-o inline:"0\n" \
		-e empty \
		-s exit:0 \
		pkg query "%a" test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg set -y -A 1 test

	atf_check \
		-o inline:"1\n" \
		-e empty \
		-s exit:0 \
		pkg query "%a" test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg set -y -A 0 test

	atf_check \
		-o inline:"0\n" \
		-e empty \
		-s exit:0 \
		pkg query "%a" test
}

set_change_name_body() {
	initialize_pkg

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg set -yn test:new

	atf_check \
		-o inline:"new-1\n" \
		-e empty \
		-s exit:0 \
		pkg info -q
}

set_change_origin_body() {
	initialize_pkg

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg set -yo origin/test:neworigin/test

	atf_check \
		-o inline:"neworigin/test\n" \
		-e empty \
		-s exit:0 \
		pkg info -qo
}

set_vital_body() {
	initialize_pkg

	atf_check \
		-o inline:"0\n" \
		-e empty \
		-s exit:0 \
		pkg query "%V" test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg set -y -v 1 test

	atf_check \
		-o inline:"1\n" \
		-e empty \
		-s exit:0 \
		pkg query "%V" test

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg set -y -v 0 test

	atf_check \
		-o inline:"0\n" \
		-e empty \
		-s exit:0 \
		pkg query "%V" test
}
