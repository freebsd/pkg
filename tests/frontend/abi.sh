#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	basic

basic_body() {
	_uname_s="$(uname -s)"
	_expected="TODO: implement me"
	if [ "${_uname_s}" = "FreeBSD" ]; then
		_expected="FreeBSD:13:amd64\n"
	else
		# The FreeBSD ELF ABI_FILE should be ignored on other systems:
		_expected="${_uname_s}:$(uname -r | cut -d. -f1):$(uname -p)\n"
		# atf_skip "Not yet supported on ${_uname_s}"
	fi
	atf_check \
		-o inline:"${_expected}" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/fbsd.bin config abi

#	atf_check \
#		-o inline:"dragonfly:5.10:x86:64\n" \
#		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/dfly.bin config abi
}
