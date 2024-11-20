#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	elfparse \
	native \
	override 

native_body() {
	_expected="$(uname -s):$(uname -r | cut -d. -f1):$(uname -p | sed s/x86_64/amd64/)\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg config abi

	_expected="$(uname -s | tolower):$(uname -r | cut -d. -f1):$(uname -p | sed s/x86_64/x86:64/)\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg config altabi
}

override_body() {
	_expected="FreeBSD:12:powerpc\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o ABI=FreeBSD:12:powerpc config abi

	_expected="freebsd:12:powerpc:32:eb\n"
	atf_check \
		-o inline:"${_expected}" \
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

	_expected="DragonFly:5:amd64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/dfly.bin config abi

	_expected="dragonfly:5:x86:64\n"
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/dfly.bin config altabi
}
