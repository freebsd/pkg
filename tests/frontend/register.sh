#! /usr/bin/env atf-sh

atf_test_case register
register_conflicts_head() {
	atf_set "descr" "testing pkg register conflicts"
}

register_conflicts_body() {
	mkdir -p teststage/${TMPDIR}
	echo a > teststage/${TMPDIR}/plop
	sum=$(openssl dgst -sha256 -binary teststage/${TMPDIR}/plop | hexdump -v -e '/1 "%x"')
	cat > test.ucl << EOF
name: "test"
origin: "osef"
version: "1"
arch: "freebsd:*"
maintainer: "non"
prefix: "${TMPDIR}"
www: "unknown"
comment: "need one"
desc: "here as well"
files: {
	"${TMPDIR}/plop" : "$sum"
}
EOF
	atf_check \
	    -o match:".*Installing.*" \
	    -e empty \
	    -s exit:0 \
	    pkg register -i teststage -M test.ucl
	nsum=$(openssl dgst -sha256 -binary plop | hexdump -v -e '/1 "%x"')
	atf_check_equal ${sum} ${nsum}
	rm -f test.ucl
	echo b > teststage/${TMPDIR}/plop
	cat > test.ucl << EOF
name: "test2"
origin: "osef"
version: "1"
arch: "freebsd:*"
maintainer: "non"
prefix: "${TMPDIR}"
www: "unknown"
comment: "need one"
desc: "here as well"
files: {
	"${TMPDIR}/plop" : "$sum2"
}
EOF
	atf_check \
	    -o match:".*Installing.*" \
	    -e match:".*conflicts.*" \
	    -s exit:70 \
	    pkg register -i teststage -M test.ucl
	nsum=$(openssl dgst -sha256 -binary plop | hexdump -v -e '/1 "%x"')
	atf_check_equal ${sum} ${nsum}
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/test_environment.sh

	atf_add_test_case register_conflicts
}
