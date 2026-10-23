#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init concurrent_downloads

concurrent_downloads_body()
{
	atf_require python3 "Requires python3 to run this test"
	atf_check -o ignore -e empty python3 \
		${RESOURCEDIR}/fetch_parallel.py "$(command -v pkg)"
}
