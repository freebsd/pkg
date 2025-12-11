#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	info_json

info_json_body() {
	atf_require_prog python3

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg foo foo 1
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -A -M foo.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg info --raw --raw-format json foo | python3 -m json.tool

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg bar bar 1
	atf_check \
		-o match:".*Installing.*\.\.\.$" \
		-e empty \
		-s exit:0 \
		pkg register -A -M bar.ucl

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg info --raw --raw-format json foo bar | python3 -m json.tool
}
