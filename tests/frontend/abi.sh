#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	elfparse \
	machoparse \
	native \
	override

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
			thisarch="amd64"
			thisabi="x86:64"
			version=$(uname -r | cut -d. -f1)
			;;
		FreeBSD)
			version=$(uname -r | cut -d. -f1)
			thisarch=$(echo "${thisarch}" | sed s/x86_64/amd64/)
			thisabi=$(echo "${thisarch}" | sed s/amd64/x86:64/)
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
		macos.bin macos106.bin macos150.bin macosfat.bin \
		"macosfat.bin#amd64" "macosfat.bin#aarch64"
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
	_expected="Darwin:17:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config abi

	_expected="darwin:17:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config altabi
}
