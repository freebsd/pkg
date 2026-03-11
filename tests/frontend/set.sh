#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	set_automatic \
	set_change_name \
	set_change_origin \
	set_change_name_not_origin \
	set_vital \
	set_partial

initialize_pkg() {

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
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

set_change_name_not_origin_body() {
	initialize_pkg

	# pkg set -n should match by name only, not by origin (issue #1187).
	# "origin/test" contains a slash and matches the origin, but -n
	# must not find the package through its origin and rename it.
	atf_check \
		-s exit:0 \
		pkg set -yn origin/test:newname

	# Verify the package name and origin are unchanged:
	# Before the fix, the name would become "newname" because -n
	# matched via origin instead of name.
	atf_check \
		-o inline:"test-1\n" \
		-e empty \
		-s exit:0 \
		pkg info -q

	atf_check \
		-o inline:"origin/test\n" \
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

set_partial_body() {
	initialize_pkg

	rm test.ucl
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf test plop-test origin/plop-test 1 /prefix

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -t -M test.ucl

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkgf test plop-foo origin/plop-foo 1 /prefix

	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -t -M test.ucl

	atf_check \
		-o inline:"plop-foo-1\nplop-test-1\ntest-1\n" \
		-e empty \
		-s exit:0 \
		pkg info -q

	atf_check \
		-o inline:"2 packages have been renamed.\n" \
		-e empty \
		-s exit:0 \
		pkg set -ypn test:bla

	atf_check \
		-o inline:"bla-1\nplop-bla-1\nplop-foo-1\n" \
		-e empty \
		-s exit:0 \
		pkg info -q

	atf_check \
		-o inline:"2 packages have been renamed.\n" \
		-e empty \
		-s exit:0 \
		pkg set -ypn bla:test

	atf_check \
		-o inline:"plop-foo-1\nplop-test-1\ntest-1\n" \
		-e empty \
		-s exit:0 \
		pkg info -q

	atf_check \
		-o inline:"origin/plop-foo\norigin/plop-test\norigin/test\n" \
		-e empty \
		-s exit:0 \
		pkg info -qo

	atf_check \
		-o inline:"2 packages have been renamed.\n" \
		-e empty \
		-s exit:0 \
		pkg set -ypo test:bla

	atf_check \
		-o inline:"origin/plop-foo\norigin/plop-bla\norigin/bla\n" \
		-e empty \
		-s exit:0 \
		pkg info -qo

}
