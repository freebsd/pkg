#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	basic

basic_body() {
	if [ `uname -s` != "FreeBSD" ]; then
		atf_skip "Not yet supported on non FreeBSD"
	fi
	atf_check \
		-o inline:"FreeBSD:13:amd64\n" \
		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/fbsd.bin config abi

#	atf_check \
#		-o inline:"dragonfly:5.10:x86:64\n" \
#		pkg -o IGNORE_OSMAJOR=1 -o ABI_FILE=$(atf_get_srcdir)/dfly.bin config abi
}
