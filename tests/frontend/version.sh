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
}

compare_body() {
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
EOF
	atf_check \
		-o match:".*Installing.*" \
		pkg register -M test.ucl
	atf_check \
		-o ignore \
		pkg info "test>0"
	atf_check \
		-o ignore \
		-e ignore \
		-s exit:70 \
		pkg info "test<5"
	atf_check \
		-o ignore \
		pkg info "test>5<6"
	atf_check \
		-o ignore \
		-e ignore \
		-s exit:70 \
		pkg info "test>5<5.20"
	atf_check \
		-o ignore \
		-e ignore \
		-s exit:70 \
		pkg info "test>5.20_3<6"
}
