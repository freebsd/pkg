#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	elfparse \
	machoparse \
	native \
	override \
	shlib

native_body() {
	OS=$(uname -s)
	thisarch=$(uname -p)
	if [ "$thisarch" = "unknown" -o "${OS}" = "Darwin" ]; then
		thisarch=$(uname -m)
	fi
	thisabi=$thisarch
	case "${OS}" in
		Linux)
			version=$(readelf -n /bin/uname  | awk '/ABI: / { split($NF, a, "."); print a[1]"."a[2] }')
			;;
		Darwin)
			# without a hint, the first arch is selected, which happens to be consistently x86_64
			thisarch="x86_64"
			thisabi="x86_64"
			version=$(uname -r | cut -d. -f1)
			;;
		FreeBSD)
			version=$(freebsd-version -u | cut -d. -f1)
			thisarch=$(echo "${thisarch}" | sed s/x86_64/amd64/)
			thisabi=$(echo "${thisarch}" | sed s/amd64/x86:64/)
			thisabi=$(echo "${thisarch}" | sed s/amd64/aarch64:64/)
			;;
		*)
			version=$(uname -r | cut -d. -f1)
			;;
	esac
	_expected="${OS}:${version}:${thisarch}\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg config abi

	_expected="$(uname -s | tr '[:upper:]' '[:lower:]'):${version}:${thisabi}\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg config altabi
}

override_body() {
	_expected="FreeBSD:12:powerpc\n"
	atf_check \
		-o inline:"${_expected}" \
		-e ignore \
		pkg -o ABI=FreeBSD:12:powerpc -o OSVERSION=1201000 config abi

	_expected="freebsd:12:powerpc:32:eb\n"
	atf_check \
		-o inline:"${_expected}" \
		-e ignore \
		pkg -o ABI=FreeBSD:12:powerpc -o OSVERSION=1201000 config altabi
}

elfparse_body() {
	# ELF parsing now works across platforms

	for bin in \
		freebsd-aarch64.bin freebsd-amd64.bin freebsd-armv6.bin freebsd-armv7.bin \
		freebsd-i386.bin freebsd-powerpc.bin freebsd-powerpc64.bin freebsd-powerpc64le.bin \
		freebsd-riscv64.bin dfly.bin linux.bin
	do
		bin_meta ${bin}

		_expected="${XABI}\n"
		atf_check \
			-o inline:"${_expected}" \
			pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/${bin} config abi

		_expected="${XALTABI}\n"
		atf_check \
			-o inline:"${_expected}" \
			pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/${bin} config altabi
	done
}

machoparse_body() {
	# Macho-O parsing now works across platforms

	for bin in \
		macos.bin macos106.bin macos150.bin \
		macosfat.bin "macosfat.bin#x86_64" "macosfat.bin#aarch64" \
		macosfatlib.bin "macosfatlib.bin#x86_64" "macosfatlib.bin#aarch64"
	do
		bin_meta ${bin}

		_expected="${XABI}\n"
		atf_check \
			-o inline:"${_expected}" \
			pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/${bin} config abi

		_expected="${XALTABI}\n"
		atf_check \
			-o inline:"${_expected}" \
			pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/${bin} config altabi
	done

	# explicitely select a fat entry that is not in the ABI_FILE
	_expected="Scanned 2 entries, found none matching selector i386\n"
	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Unable to determine ABI" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#i386 config abi

	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Unable to determine ABI" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#i386 config altabi

	# explicitely select a fat entry that is not a valid architecture, hence not in the ABI_FILE
	_expected=""
	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Invalid ABI_FILE architecture hint abc" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#abc config abi

	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Invalid ABI_FILE architecture hint abc" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#abc config altabi

	# if the binary is not universal, selecting the first entry is not commentable
	_expected="Darwin:24:aarch64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e not-match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin config abi

	_expected="darwin:24:aarch64:64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e not-match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin config altabi

	_expected="Scanned 1 entry, found none matching selector i386\n"
	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Unable to determine ABI" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin#i386 config abi

	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Unable to determine ABI" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin#i386 config altabi

	# if the binary is universal, selecting the first entry is to be commented
	_expected="Darwin:17:x86_64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config abi

	_expected="darwin:17:x86_64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config altabi
}

# Helper for the test below.  Create a package with the specified name, version
# and ABI, containing a single shared library of the same name.
make_onelib_pkg()
{
	local name version abi

	name=$1
	version=$2
	abi=$3
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg $name $name $version / $abi
	cat <<__EOF__ >> ${name}.ucl
files: {
	${TMPDIR}/lib${name}.so.1: "",
}
__EOF__
	atf_check pkg create -M ${name}.ucl
}

# Try to verify ABI mixing in shared library dependencies.  It's ok for a shlib
# to depend on a shlib from a newer ABI, at least on FreeBSD.  In this case,
# bar-* can be satisfied by foo-*, but baz-2 cannot be satisfied by foo-1.  We
# check that the solver enforces this.
shlib_body()
{
	atf_skip_on Darwin Not sure how to test this there
	atf_skip_on Linux Not sure how to test this there

	atf_check touch empty.c
	atf_check cc -shared -Wl,-soname=libfoo.so.1 empty.c -o libfoo.so.1
	atf_check ln -s libfoo.so.1 libfoo.so
	atf_check cc -shared -Wl,-soname=libbar.so.1 empty.c -o libbar.so.1 -lfoo -L.
	atf_check ln -s libbar.so.1 libbar.so
	atf_check cc -shared -Wl,-soname=libbaz.so.1 empty.c -o libbaz.so.1 -lbar -L.

	make_onelib_pkg foo 1 FreeBSD:15:amd64
	make_onelib_pkg bar 1 FreeBSD:15:amd64
	make_onelib_pkg baz 1 FreeBSD:15:amd64

	make_onelib_pkg foo 2 FreeBSD:16:amd64
	make_onelib_pkg bar 2 FreeBSD:15:amd64
	make_onelib_pkg baz 2 FreeBSD:16:amd64

	atf_check mkdir target reposconf

	# Install the base version packages, foo-1, bar-1, baz-1.
	for pkg in foo bar baz; do
		atf_check \
		    pkg -o REPOS_DIR=/dev/null -r ${TMPDIR}/target \
			install -qfy ${TMPDIR}/${pkg}-1.pkg
	done

	# Set up our repos.  foo and baz go in one, bar in another.
	atf_check mkdir repo1 repo2
	atf_check cp ${TMPDIR}/foo-2.pkg ${TMPDIR}/repo1
	atf_check cp ${TMPDIR}/baz-2.pkg ${TMPDIR}/repo1
	atf_check cp ${TMPDIR}/bar-2.pkg ${TMPDIR}/repo2

	for repo in repo1 repo2; do
		atf_check -o ignore pkg repo ${repo}
		cat <<__EOF__ > ${TMPDIR}/reposconf/${repo}.conf
${repo}: {
	url: file://${TMPDIR}/${repo},
	enabled: true
}
__EOF__

	done

	# Override the ABI for each repo update, otherwise pkg
	# refuses to proceed.
	atf_check -o ignore -e ignore \
	    pkg -o REPOS_DIR=${TMPDIR}/reposconf \
		-r ${TMPDIR}/target -o ABI=FreeBSD:16:amd64 update -r repo1
	atf_check -o ignore -e ignore \
	    pkg -o REPOS_DIR=${TMPDIR}/reposconf \
		-r ${TMPDIR}/target -o ABI=FreeBSD:15:amd64 update -r repo2

	atf_check -o ignore -e empty \
	    pkg -o REPOS_DIR=${TMPDIR}/reposconf -r ${TMPDIR}/target \
		-o IGNORE_OSMAJOR=yes \
		-o OSVERSION=1600000 \
		-o ABI=FreeBSD:16:amd64 upgrade -y -r repo1

	# We should end up with foo-2, bar-1 and baz-2 installed.  This
	# is not quite right since baz-2 depends on libbar.so.1, which
	# has an older OS version.  But this is a pre-existing behaviour
	# of the solver: pkg_solve_add_require_rule() will ignore the
	# dependency if it cannot be satisfied.  If that behaviour
	# changes, this test will need an update.
	#
	# However, we shouldn't uninstall bar-1 since foo-2's
	# libfoo.so.1 should still be compatible even though its OS
	# version is newer.
	atf_check test -f ${TMPDIR}/target/${TMPDIR}/libfoo.so.1
	atf_check test -f ${TMPDIR}/target/${TMPDIR}/libbar.so.1
	atf_check test -f ${TMPDIR}/target/${TMPDIR}/libbaz.so.1
}
