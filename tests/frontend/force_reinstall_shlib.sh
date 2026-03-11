#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	force_install_no_unrelated_reinstall \
	force_install_no_rdep_removal

force_install_no_unrelated_reinstall_body() {
	atf_skip_on Darwin The macOS linker uses different flags

	# Regression test: pkg install -f should not propose to reinstall
	# unrelated packages whose only connection to the target is via
	# shlib provides/requires.
	#
	# Without the fix in handle_provide, the remote provider would be
	# added to the universe even when its digest matches the local one,
	# giving the SAT solver the opportunity to pick the remote and
	# schedule a spurious reinstall.

	touch empty.c
	cc -shared -Wl,-soname=libtest.so.1 empty.c -o libtest.so.1
	ln -s libtest.so.1 libtest.so
	cc -shared -Wl,-soname=libconsumer.so.1 empty.c -o libconsumer.so.1 -ltest -L.

	# provider: provides libtest.so.1
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg provider provider 1 /usr/local
	cat << EOF >> provider.ucl
files: {
	${TMPDIR}/libtest.so.1: "",
}
EOF

	# target: links against libtest.so.1 but has NO explicit dep on
	# provider.  The only connection is via shlib require/provide.
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg target target 1 /usr/local
	cat << EOF >> target.ucl
files: {
	${TMPDIR}/libconsumer.so.1: "",
}
EOF

	# Create packages and repo
	for p in provider target; do
		atf_check \
			-o ignore \
			-e empty \
			-s exit:0 \
			pkg create -M ${p}.ucl
	done

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	# Install both from the repo (digests will match local vs remote)
	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" \
		install -y provider target

	# Force reinstall target: only target should be reinstalled,
	# provider must NOT be pulled in.
	atf_check \
		-o match:"target-1" \
		-o not-match:"provider" \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" \
		install -fn target
}

force_install_no_rdep_removal_body() {
	# Regression test for issue #2566: pkg install -f of a package
	# that many other packages depend on should NOT propose to remove
	# those reverse dependencies.

	# base: the package we will force-reinstall
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg base base 1 /usr/local

	# rdep1, rdep2: packages that depend on base
	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg rdep1 rdep1 1 /usr/local
	cat << EOF >> rdep1.ucl
deps: {
	base: {
		origin: "base",
		version: "1"
	}
}
EOF

	atf_check -s exit:0 sh ${RESOURCEDIR}/test_subr.sh new_pkg rdep2 rdep2 1 /usr/local
	cat << EOF >> rdep2.ucl
deps: {
	base: {
		origin: "base",
		version: "1"
	}
}
EOF

	# Create packages and repo
	for p in base rdep1 rdep2; do
		atf_check \
			-o ignore \
			-e empty \
			-s exit:0 \
			pkg create -M ${p}.ucl
	done

	atf_check \
		-o ignore \
		-e empty \
		-s exit:0 \
		pkg repo .

	mkdir reposconf
	cat << EOF > reposconf/repo.conf
local: {
	url: file:///$TMPDIR,
	enabled: true
}
EOF

	# Install all three packages
	atf_check \
		-o ignore \
		-s exit:0 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" \
		install -y base rdep1 rdep2

	# Force reinstall base: rdep1 and rdep2 must NOT be removed
	atf_check \
		-o match:"base" \
		-o not-match:"REMOVED" \
		-o not-match:"rdep1" \
		-o not-match:"rdep2" \
		-s exit:1 \
		pkg -o REPOS_DIR="${TMPDIR}/reposconf" -o PKG_CACHEDIR="${TMPDIR}" \
		install -fn base
}
