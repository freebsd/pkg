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
	if [ "${OS}" = "Linux" ]; then
		version=$(readelf -n /bin/uname  | awk '/ABI: / { split($NF, a, "."); print a[1]"."a[2] }')
	else
		version=$(uname -r | cut -d. -f1)
	fi
	if [ "${OS}" = "FreeBSD" ]; then
		thisarch=$(echo "${thisarch}" | sed s/x86_64/amd64/)
	fi
	_expected="${OS}:${version}:${thisarch}\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg config abi

	_expected="$(uname -s | tr '[:upper:]' '[:lower:]'):${version}:$(echo $thisarch | sed 's/x86_64/x86:64/; s/amd64/x86:64/')\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg config altabi
}

override_body() {
	_expected="FreeBSD:12:powerpc\n"
	atf_check \
		-o inline:"${_expected}" \
		-e ignore \
		pkg -o ABI=FreeBSD:12:powerpc config abi

	_expected="freebsd:12:powerpc:32:eb\n"
	atf_check \
		-o inline:"${_expected}" \
		-e ignore \
		pkg -o ABI=FreeBSD:12:powerpc config altabi
}

elfparse_body() {
	# ELF parsing now works across platforms
	_expected="FreeBSD:13:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/fbsd.bin config abi

	_expected="freebsd:13:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/fbsd.bin config altabi

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

	_expected="Darwin:24:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config abi

	_expected="darwin:24:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/macosfat.bin config altabi
}
