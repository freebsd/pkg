#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	basic \
	split_upgrade \
	depends \
	multiple_upgrade

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
	    pkg -r ${TMPDIR}/target query "%Fp" test-backup-libraries
	rm foo-1.pkg
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "2"
	atf_check pkg create -M foo.ucl
	atf_check -o ignore pkg repo .
	atf_check -o ignore \
	    pkg -o BACKUP_LIBRARY_PATH=/back/ -o BACKUP_LIBRARIES=true \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
	        update -f

	version1=$(pkg -r ${TMPDIR}/target query "%v" test-backup-libraries)
	atf_check -o ignore \
	    pkg -o BACKUP_LIBRARY_PATH=/back/ -o BACKUP_LIBRARIES=true \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
	        upgrade -y
	atf_check -o inline:"/back/libempty.so.1\n" \
	    pkg -r ${TMPDIR}/target query "%Fp" test-backup-libraries
	atf_check -o inline:"/back/libfoo.so.1\n" \
	    pkg -r ${TMPDIR}/target query "%Fp" foo-backup-libraries
	version2=$(pkg -r ${TMPDIR}/target query "%v" test-backup-libraries)
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
	atf_check -o inline:"libbar.so.1\n" \
	    pkg -r ${TMPDIR}/target query "%b" bar-backup-libraries
	atf_check -o inline:"libfoo.so.1\n" \
	    pkg -r ${TMPDIR}/target query "%b" foo-backup-libraries
}

# If a package foo provides libfoo.so.1 and a different package bar
# depends on it, and libfoo.so is upgraded to version 2, and
# BACKUP_LIBRARIES is configured, then upgrading should not result in
# removal of bar.
#
# XXX-MJ check the case where bar is not upgraded
# XXX-MJ check the case where foo and bar come from different repositories
depends_body()
{
	atf_skip_on Darwin The macOS linker uses different flags

	atf_check touch empty.c
	atf_check cc -shared -Wl,-soname=libfoo.so.1 empty.c -o libfoo.so.1
	atf_check ln -s libfoo.so.1 libfoo.so
	atf_check cc -shared -Wl,-soname=libbar.so.1 empty.c -o libbar.so.1 -lfoo -L.
	atf_check cc -shared -Wl,-soname=libbar.so.2 empty.c -o libbar.so.2 -lfoo -L.
	atf_check cc -shared -Wl,-soname=libfoo.so.2 empty.c -o libfoo.so.2

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/libfoo.so.1: "",
}
EOF
	atf_check pkg create -M foo.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "2"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/libfoo.so.2: "",
}
EOF
	atf_check pkg create -M foo.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "1"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/libbar.so.1: "",
}
EOF
	atf_check pkg create -M bar.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "2"
	cat << EOF >> bar.ucl
files: {
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

	# Upgrade foo to version 2.  This will remove libfoo.so.1, but
	# we shouldn't remove the bar package because libfoo.so.1 is
	# still available via the backup libraries mechanism.
	atf_check -o ignore \
	    pkg \
		-o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
		-o BACKUP_LIBRARIES=yes \
	        upgrade -y foo bar

	atf_check test -f ${TMPDIR}/target/${TMPDIR}/libfoo.so.2
	atf_check test -f ${TMPDIR}/target/${TMPDIR}/libbar.so.2
}

# A regression test for a scenario where the same shlib version is backed up
# multiple times.  This would result in a registration failure of the
# backup package, which in turn could result in a sqlite error if this
# happened during a pkg uninstall during a split upgrade.
multiple_upgrade_body()
{
	atf_skip_on Darwin The macOS linker uses different flags

	atf_check touch a b c d
	atf_check touch empty.c
	atf_check cc -shared -Wl,-soname=libfoo.so.1 empty.c -o libfoo.so.1
	atf_check ln -s libfoo.so.1 libfoo.so
	atf_check cc -shared -Wl,-soname=libbar.so.1 empty.c -o libbar.so.1
	atf_check ln -s libbar.so.1 libbar.so

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "1"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/a: "",
	${TMPDIR}/libfoo.so.1: "",
	${TMPDIR}/libbar.so.1: "",
}
EOF
	atf_check pkg create -M foo.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "1"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/b: "",
}
EOF
	atf_check pkg create -M bar.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "baz" "baz" "1"
	cat << EOF >> baz.ucl
files: {
	${TMPDIR}/c: "",
}
EOF
	atf_check pkg create -M baz.ucl

	atf_check mkdir ${TMPDIR}/target ${TMPDIR}/reposconf
	atf_check -o ignore \
	    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target install -qfy \
	    ${TMPDIR}/foo-1.pkg ${TMPDIR}/bar-1.pkg ${TMPDIR}/baz-1.pkg

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "2"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/a: "",
}
EOF
	atf_check pkg create -M foo.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "2"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/b: "",
	${TMPDIR}/libfoo.so.1: "",
	${TMPDIR}/libbar.so.1: "",
}
EOF
	atf_check pkg create -M bar.ucl

	cat <<EOF > ${TMPDIR}/reposconf/repo.conf
local: {
	url: file://${TMPDIR}/repo,
	enabled: true
}
EOF
	atf_check mkdir repo
	atf_check mv foo-2.pkg bar-2.pkg repo
	atf_check -o ignore pkg repo repo

	# Upgrade the packages.  Moving the libraries between packages will
	# cause them to be backed up.
	atf_check -o ignore \
	    pkg \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
		-o BACKUP_LIBRARIES=yes \
		-o BACKUP_LIBRARY_PATH=/back/ \
	        upgrade -y

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "foo" "foo" "3"
	cat << EOF >> foo.ucl
files: {
	${TMPDIR}/a: "",
	${TMPDIR}/libfoo.so.1: "",
	${TMPDIR}/libbar.so.1: "",
}
EOF
	atf_check pkg create -M foo.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "bar" "bar" "3"
	cat << EOF >> bar.ucl
files: {
	${TMPDIR}/b: "",
	${TMPDIR}/c: "",
}
EOF
	atf_check pkg create -M bar.ucl

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg "baz" "baz" "3"
	cat << EOF >> baz.ucl
files: {
	${TMPDIR}/d: "",
},
deps: {
	"foo": {
		version: "3",
		origin: "foo",
	},
}
EOF
	atf_check pkg create -M baz.ucl

	atf_check rm -rf repo
	atf_check mkdir repo
	atf_check mv foo-3.pkg bar-3.pkg baz-3.pkg repo
	atf_check -o ignore pkg repo repo

	# Make sure pkg fetches a new catalogue.
	atf_check -o ignore \
	    pkg \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
	        update -f

	# Upgrade the three packages to version 3.  The shlibs move from bar-2
	# to foo-3, and the library backup logic infers that it must therefore
	# back up libfoo.so and libbar.so.  (This is really a bug in itself
	# since those libraries aren't actually going away, but that's because
	# library backup mechanism is plumbed in at the wrong layer.  One step
	# at a time.)
	#
	# Then, we use a conflict between bar-3 and baz-2 to split the
	# bar-2->bar-3 upgrade.  baz-3 depends on foo-3 to try and provoke the
	# split, otherwise pkg would first upgrade baz-1->baz-3 and then there
	# would be no need to split the bar upgrade.
	atf_check -o ignore \
	    -e match:"bar-backup-libraries-.* conflicts with foo-backup-libraries-.*" \
	    pkg \
	        -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
		-o BACKUP_LIBRARIES=yes \
		-o BACKUP_LIBRARY_PATH=/back/ \
	        upgrade -y
}
