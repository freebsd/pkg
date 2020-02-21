#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic

basic_body() {
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/libempty.so.1: "",
}

EOF

	mkdir ${TMPDIR}/target
	touch empty.c
	cc -shared -Wl,-soname=libempty.so.1 empty.c -o libempty.so.1
	sum=$(openssl dgst -sha256 -hex libempty.so.1)

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	atf_check \
		-e empty \
		-o empty \
		-s exit:0 \
		pkg -o BACKUP_LIBRARIES=true -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy ${TMPDIR}/test-1.txz

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"

	atf_check \
		-o empty \
		-e empty \
		-s exit:0 \
		pkg create -M test.ucl

	rm test-1.txz
	atf_check \
		-o ignore \
		-s exit:0 pkg repo .

	mkdir reposconf
	cat <<EOF >> reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check \
		-e empty \
		-o ignore \
		-s exit:0 \
		pkg -o BACKUP_LIBRARY_PATH=/back/ -o BACKUP_LIBRARIES=true -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target upgrade -y
	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		ls target/back/libempty.so.1
}
