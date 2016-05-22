#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	config

config_body()
{
	new_pkg "test" "test" "1"
	echo "@config ${TMPDIR}/a" > plist

	echo "entry" > a

	atf_check \
		-e empty \
		-o empty \
		-s exit:0 \
		pkg create -M test.ucl -p plist
}
