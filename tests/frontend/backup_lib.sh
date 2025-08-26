#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic \
	split_upgrade

basic_body() {
	atf_skip_on Darwin The macOS linker uses different flags
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "1"
	cat << EOF >> test.ucl
files: {
	${TMPDIR}/libempty.so.1: "",
}
EOF

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/libfoo.so.1: "",
}
EOF
	mkdir ${TMPDIR}/target
	touch empty.c
	cc -shared -Wl,-soname=libempty.so.1 empty.c -o libempty.so.1
	cc -shared -Wl,-soname=libfoo.so.1 empty.c -o libfoo.so.1
	sum=$(openssl dgst -sha256 -hex libempty.so.1)

	atf_check pkg create -M test.ucl

	atf_check pkg create -M foo.ucl

	atf_check \
	    pkg -o BACKUP_LIBRARIES=true -o REPOS_DIR=/dev/null \
	        -r ${TMPDIR}/target \
	        install -qfy ${TMPDIR}/test-1.pkg

	atf_check \
	    pkg -o BACKUP_LIBRARIES=true -o REPOS_DIR=/dev/null \
	        -r ${TMPDIR}/target \
	        install -qfy ${TMPDIR}/foo-1.pkg

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "test" "test" "2"

	atf_check pkg create -M test.ucl

	rm test-1.pkg
	atf_check -o ignore pkg repo .

	mkdir reposconf
	cat <<EOF >> reposconf/repo.conf
local: {
	url: file:///${TMPDIR},
	enabled: true
}
EOF

	atf_check -o ignore \
	    pkg -o BACKUP_LIBRARY_PATH=/back/ -o BACKUP_LIBRARIES=true \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
	        upgrade -y
	atf_check -o ignore \
	    ls target/back/libempty.so.1
	atf_check -o inline:"/back/libempty.so.1\n" \
	    pkg -r ${TMPDIR}/target query "%Fp" compat-libraries
	rm foo-1.pkg
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "2"
	atf_check pkg create -M foo.ucl
	atf_check -o ignore pkg repo .
	atf_check -o ignore \
	    pkg -o BACKUP_LIBRARY_PATH=/back/ -o BACKUP_LIBRARIES=true \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
	        update -f
	version1=$(pkg -r ${TMPDIR}/target query "%v" compat-libraries)
	atf_check -o ignore \
	    pkg -o BACKUP_LIBRARY_PATH=/back/ -o BACKUP_LIBRARIES=true \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
	        upgrade -y
	atf_check -o inline:"/back/libempty.so.1\n/back/libfoo.so.1\n" \
	    pkg -r ${TMPDIR}/target query "%Fp" compat-libraries
	version2=$(pkg -r ${TMPDIR}/target query "%v" compat-libraries)
	[ ${version2} -ge ${version1} ] || \
	    atf_fail "the version hasn't been bumped ${version2} >= ${version1}"
}

# Make sure that we back up libraries properly when a package upgrade is
# split into separate deletion and installation steps.
split_upgrade_body()
{
	atf_skip_on Darwin The macOS linker uses different flags

	atf_check touch a b empty.c
	atf_check cc -shared -Wl,-soname=libfoo.so.1 empty.c -o libfoo.so.1
	atf_check cc -shared -Wl,-soname=libfoo.so.2 empty.c -o libfoo.so.2
	atf_check cc -shared -Wl,-soname=libbar.so.1 empty.c -o libbar.so.1
	atf_check cc -shared -Wl,-soname=libbar.so.2 empty.c -o libbar.so.2

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/a: "",
	${TMPDIR}/libfoo.so.1: "",
}
EOF
	atf_check pkg create -M foo.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "2"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/b: "",
	${TMPDIR}/libfoo.so.2: "",
}
EOF
	atf_check pkg create -M foo.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "1"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/b: "",
	${TMPDIR}/libbar.so.1: "",
}
EOF
	atf_check pkg create -M bar.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "2"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/a: "",
	${TMPDIR}/libbar.so.2: "",
}
EOF
	atf_check pkg create -M bar.ucl

	atf_check mkdir ${TMPDIR}/target ${TMPDIR}/reposconf
	atf_check -o ignore pkg repo .
	cat <<EOF > ${TMPDIR}/reposconf/repo.conf
local: {
	url: file://${TMPDIR},
	enabled: true
}
EOF

	# Install the base version packages, foo-1 and bar-1.
	atf_check \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target \
	        install -qfy ${TMPDIR}/foo-1.pkg
	atf_check \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target \
	        install -qfy ${TMPDIR}/bar-1.pkg

	# Now upgrade foo and bar to version 2.  Since the files a and b
	# move between the two packages, a split upgrade is necessary.
	atf_check -o ignore \
	    pkg -o BACKUP_LIBRARY_PATH=/back/ -o BACKUP_LIBRARIES=true \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
	        upgrade -y

	atf_check test -f ${TMPDIR}/target/back/libfoo.so.1
	atf_check test -f ${TMPDIR}/target/back/libbar.so.1
	atf_check -o inline:"libbar.so.1\nlibfoo.so.1\n" \
	    pkg -r ${TMPDIR}/target query "%b" compat-libraries
}
