#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	query

query_body() {
	touch plop
	touch bla
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	cat >> test.ucl << EOF
options: {
	"OPT1": "on"
	"OPT2": "off"
}
files: {
	"${TMPDIR}/plop": ""
	"${TMPDIR}/bla": ""
}
EOF

	atf_check \
		-o match:".*Installing.*" \
		-e empty \
		-s exit:0 \
		pkg register -M test.ucl

	atf_check \
		-o inline:"test\n" \
		-e empty \
		-s exit:0 \
		pkg query "%n"

	atf_check \
		-o inline:"test\n" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#O > 0" "%n"

	atf_check \
		-o inline:"test 2\n" \
		-e empty \
		-s exit:0 \
		pkg query "%n %#O"

	atf_check \
		-o inline:"test 1\n" \
		-e empty \
		-s exit:0 \
		pkg query "%n %?O"

	atf_check \
		-o inline:"" \
		-e empty \
		-s exit:0 \
		pkg query -e "%#O == 0" "%n"
}
