#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	version \
	compare

version_body() {
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1 2
	atf_check -o inline:">\n" -s exit:0 pkg version -t 2 1
	atf_check -o inline:"=\n" -s exit:0 pkg version -t 2 2
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 2 1,1
	# Special prefixes
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1.pl1 1.alpha1
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1.alpha1 1.beta1
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1.beta1 1.pre1
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1.pre1 1.rc1
	atf_check -o inline:"<\n" -s exit:0 pkg version -t 1.rc1 1
}

compare_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 5.20_3

	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl
	atf_check \
		-o ignore \
		pkg info "test>0"
	atf_check \
		-o ignore \
		-e ignore \
		-s exit:1 \
		pkg info "test<5"
	atf_check \
		-o ignore \
		pkg info "test>5<6"
	atf_check \
		-o ignore \
		-e ignore \
		-s exit:1 \
		pkg info "test>5<5.20"
	atf_check \
		-o ignore \
		-e ignore \
		-s exit:1 \
		pkg info "test>5.20_3<6"
}
