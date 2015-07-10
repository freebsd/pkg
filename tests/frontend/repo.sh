#! /usr/bin/env atf-sh

atf_test_case repo
repo_head() {
	atf_set "descr" "testing pkg repo"
}

repo_body() {
	touch plop
	touch bla
	cat > test.ucl << EOF
name: "test"
origin: "osef"
version: "1"
arch: "freebsd:*"
maintainer: "test"
www: "unknown"
prefix: "${TMPDIR}"
comment: "need none"
desc: "here as well"
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
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	ln -s test-1.txz test.txz

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	if [ `uname -s` = "Darwin" ]; then
		atf_pass
	fi

	nb=$(tar -xf digests.txz -O digests | wc -l)
	atf_check_equal $nb 2

	mkdir Latest
	ln -s test-1.txz Latest/test.txz

	atf_check \
		-o inline:"Creating repository in .:  done\nPacking files for repository:  done\n" \
		-e empty \
		-s exit:0 \
		pkg repo .

	nb=$(tar -xf digests.txz -O digests | wc -l)
	atf_check_equal $nb 2

}

atf_init_test_cases() {
	. $(atf_get_srcdir)/test_environment.sh

	atf_add_test_case repo
}
