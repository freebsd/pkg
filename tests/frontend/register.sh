#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	register_conflicts \
	register_message

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

register_message_body() {
	cat << EOF > +MANIFEST
name: "test2"
origin: "osef"
version: "1"
arch: "freebsd:*"
maintainer: "non"
prefix: "${TMPDIR}"
www: "unknown"
comment: "need one"
desc: "here as well"
EOF
	cat << EOF > +DISPLAY
message
EOF

OUTPUT='test2-1:
Always:
message

'
	atf_check -o match:"message" pkg register -m .
	atf_check -o inline:"${OUTPUT}" pkg info -D test2

	cat << EOF > +DISPLAY
[
	{ message: "hey"},
	{ message: "install", type = install},
	{ message: "remove", type = remove},
]
EOF
OUTPUT='test2-1:
Always:
hey

On install:
install

On remove:
remove

'
	atf_check -o match:"hey" -o match:"install" -o not-match:"remove" pkg register -m .
	atf_check -o inline:"${OUTPUT}" pkg info -D test2

}
