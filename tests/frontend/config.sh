#!/usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh
tests_init \
	inline_repo

inline_repo_body() {
	cat > pkgconfiguration << EOF
repositories: {
	pkg1: { url = file:///tmp },
	pkg2: { url = file:///tmp2 },
}
EOF
	atf_check -o match:'^    url             : "file:///tmp",$' \
		-o match:'^    url             : "file:///tmp2",$' \
		pkg -o REPOS_DIR=/dev/null -C pkgconfiguration -vv
}
