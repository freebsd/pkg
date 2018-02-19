#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	set_automatic \
	set_change_name \
	set_change_origin \
	set_vital

initialize_pkg() {

	atf_check -s exit:0 ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	sed -i'' -e 's#origin.*#origin: origin/test#' test.ucl

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
