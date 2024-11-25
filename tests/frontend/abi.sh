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

	_expected="FreeBSD:14:aarch64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-aarch64.bin config abi

	_expected="freebsd:14:aarch64:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-aarch64.bin config altabi

	_expected="FreeBSD:14:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-amd64.bin config abi

	_expected="freebsd:14:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-amd64.bin config altabi

	_expected="FreeBSD:13:armv6\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-armv6.bin config abi

	_expected="freebsd:13:armv6:32:el:eabi:hardfp\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-armv6.bin config altabi

	_expected="FreeBSD:14:armv7\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-armv7.bin config abi

	_expected="freebsd:14:armv7:32:el:eabi:hardfp\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-armv7.bin config altabi

	_expected="FreeBSD:14:i386\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-i386.bin config abi

	_expected="freebsd:14:x86:32\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-i386.bin config altabi

	_expected="FreeBSD:14:powerpc\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-powerpc.bin config abi

	_expected="freebsd:14:powerpc:32:eb\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-powerpc.bin config altabi

	_expected="FreeBSD:14:powerpc64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-powerpc64.bin config abi

	_expected="freebsd:14:powerpc:64:eb\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-powerpc64.bin config altabi

	_expected="FreeBSD:14:powerpc64le\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-powerpc64le.bin config abi

	_expected="freebsd:14:powerpc:64:el\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-powerpc64le.bin config altabi

	_expected="FreeBSD:14:riscv64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-riscv64.bin config abi

	_expected="freebsd:14:riscv:64:hf\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/freebsd-riscv64.bin config altabi

	_expected="dragonfly:5.10:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/dfly.bin config abi

	_expected="dragonfly:5.10:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/dfly.bin config altabi

	_expected="Linux:3.2:x86_64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/linux.bin config abi

	_expected="linux:3.2:x86_64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/linux.bin config altabi
}

machoparse_body() {
	# Macho-O parsing now works across platforms
	_expected="Darwin:24:aarch64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin config abi

	_expected="darwin:24:aarch64:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin config altabi

	_expected="Darwin:10:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos106.bin config abi

	_expected="darwin:10:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos106.bin config altabi

	_expected="Darwin:24:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos150.bin config abi

	_expected="darwin:24:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos150.bin config altabi

	# macosfat.bin has amd64 as its first entry
	_expected="Darwin:24:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config abi

	_expected="darwin:24:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config altabi

	# explicitely select an existing fat entry
	_expected="Darwin:24:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#amd64 config abi

	_expected="darwin:24:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#amd64 config altabi

	_expected="Darwin:24:aarch64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#aarch64 config abi

	_expected="darwin:24:aarch64:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#aarch64 config altabi

	# explicitely select a fat entry that is not in the ABI_FILE
	_expected="Scanned 2 entries, found none matching selector i386\n"
	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Unable to determine the ABI" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin#i386 config abi

	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Unable to determine the ABI" \
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
		-e match:"Unable to determine the ABI" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin#i386 config abi

	atf_check \
		-s exit:1 \
		-o inline:"${_expected}" \
		-e match:"Unable to determine the ABI" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macos.bin#i386 config altabi

	# if the binary is universal, selecting the first entry is to be commented
	_expected="Darwin:24:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config abi

	_expected="darwin:24:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		-e match:"picking first" \
		pkg -d -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config altabi
}
