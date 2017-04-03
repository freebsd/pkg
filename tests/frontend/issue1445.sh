#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	dead_symlink \
	good_symlink


dead_symlink_body() {
	new_pkg "test" "test" "1" || atf_fail "plop"
	cat << EOF >> test.ucl
directories {
	${TMPDIR}/plop = "y";
}
EOF
	mkdir ${TMPDIR}/plop
	atf_check \
		pkg create -M test.ucl

	rmdir ${TMPDIR}/plop
	ln -sf ${TMPDIR}/plop2 ${TMPDIR}/plop
	atf_check \
		-o ignore \
		pkg -o REPOS_DIR=/dev/null install -y ${TMPDIR}/test-1.txz
	test -d ${TMPDIR}/plop || atf_fail "directory not created"
}

good_symlink_body() {
	new_pkg "test" "test" "1" || atf_fail "plop"
	cat << EOF >> test.ucl
directories {
	${TMPDIR}/plop = "y";
}
EOF
	mkdir ${TMPDIR}/plop
	atf_check \
		pkg create -M test.ucl

	rmdir ${TMPDIR}/plop
	mkdir ${TMPDIR}/plop2
	ln -sf ${TMPDIR}/plop2 ${TMPDIR}/plop
	atf_check \
		-o ignore \
		pkg -o REPOS_DIR=/dev/null install -y ${TMPDIR}/test-1.txz
	test -h ${TMPDIR}/plop || atf_fail "Symlink deleted"
}
