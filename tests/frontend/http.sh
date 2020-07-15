#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

tests_init \
	simple_repo

cleanup() {
	pkill -15 -F ${TMPDIR}/http.pid
}

simple_repo_body()
{
	pidfile=${TMPDIR}/http.pid
	touch ${pidfile}

	atf_require mini_httpd "Requires mini_httpd to run this test"
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check pkg create -M test.ucl
	mkdir repo
	mv test-1.txz repo/
	atf_check -o ignore pkg repo repo

	trap cleanup EXIT
	atf_check -o empty -e empty mini_httpd -h 127.0.0.1 -p 64242 -d ${TMPDIR}/repo -dd ${TMPDIR}/repo -i ${pidfile}

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: http://localhost:64242 }
}
EOF
	atf_check -o ignore \
		pkg -C ./pkg.conf update
	atf_check -o match:"Installing test-1" \
		pkg -C ./pkg.conf install -y test
}
