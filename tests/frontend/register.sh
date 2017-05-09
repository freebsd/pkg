#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	register_conflicts \
	register_message \
	prefix_is_a_symlink

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
	new_manifest test1 1 "${TMPDIR}"
	cat << EOF > +DISPLAY
message
EOF

OUTPUT='test1-1:
Always:
message

'
	atf_check -o match:"message" pkg register -m .
	atf_check -o inline:"${OUTPUT}" pkg info -D test1

	new_manifest test2 1 "${TMPDIR}"
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

prefix_is_a_symlink_body()
{
	new_pkg "test" "test" "1"
	mkdir -p ${TMPDIR}/${TMPDIR}/plop/bla
	echo "something" > ${TMPDIR}/${TMPDIR}/plop/bla/a
	ln ${TMPDIR}/${TMPDIR}/plop/bla/a ${TMPDIR}/${TMPDIR}/plop/bla/b
	ln -sf a ${TMPDIR}/${TMPDIR}/plop/bla/c
	ln -sf a ${TMPDIR}/${TMPDIR}/plop/bla/1
	ln -sf d ${TMPDIR}/${TMPDIR}/plop/bla/2
	sed -e "s,^prefix.*,prefix = ${TMPDIR},g" test.ucl > test2.ucl
	echo "plop/bla/1" > plist
	echo "plop/bla/2" >> plist
	echo "plop/bla/a" >> plist
	echo "plop/bla/b" >> plist
	echo "plop/bla/c" >> plist

	mkdir -p ${TMPDIR}/target/${TMPDIR}/
	mkdir -p ${TMPDIR}/target/hey
	rmdir ${TMPDIR}/target/${TMPDIR}/
	ln -sf ${TMPDIR}/target/hey ${TMPDIR}/target/${TMPDIR}
	atf_check \
		-o ignore \
		pkg -r ${TMPDIR}/target register -M ${TMPDIR}/test2.ucl -f ${TMPDIR}/plist -i ${TMPDIR}
	test -f ${TMPDIR}/target/${TMPDIR}/plop/bla/a || atf_fail "hardlinks failed"
	test -f ${TMPDIR}/target/${TMPDIR}/plop/bla/b || atf_fail "hardlinks failed2"
	inode1=$(ls -i ${TMPDIR}/target/${TMPDIR}/plop/bla/a | awk '{ print $1 }')
	inode2=$(ls -i ${TMPDIR}/target/${TMPDIR}/plop/bla/b | awk '{ print $1 }')
	atf_check_equal $inode1 $inode2
	test -L ${TMPDIR}/target/${TMPDIR}/plop/bla/c || atf_fail "symlinks failed"
	test -L ${TMPDIR}/target/${TMPDIR}/plop/bla/1 || atf_fail "symlinks failed 1"
	test -L ${TMPDIR}/target/${TMPDIR}/plop/bla/2 || atf_fail "symlinks failed 2"
}
