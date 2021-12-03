#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

CLEANUP="simple_fetch simple_repo simple_audit"

tests_init \
	simple_fetch \
	simple_repo \
	simple_audit


httpd_startup() {
	pidfile=${TMPDIR}/http.pid
	statusfile=${TMPDIR}/http.status
	: > ${pidfile}
	: > ${statusfile}

	DIR=$1
	python3 -u -m http.server -d "${DIR}" --bind 127.0.0.1 0 > ${statusfile} &
	jobs -p %1 > ${pidfile}
	# a crude way to wait for its startup, I can't think of anything better
	while kill -s 0 %1 && ! [ -s ${statusfile} ]; do
	    sleep .1
	done
        url=$(sed -n 's#.*(\(http://.*\)/).*#\1#p' ${statusfile})
	atf_check test -n "${url}"
}

httpd_cleanup() {
	pid=$(cat ${TMPDIR}/http.pid)
	if [ -z "${pid}" ]; then
	    return 1
	fi
	if kill -s 0 ${pid}; then
	    kill -s INT ${pid}
	    for i in 0 1 2 3 4 5 6 7 8 9; do
		if ! kill -s 0 ${pid}; then
		    break
		fi
	    done
	fi
}

simple_repo_body()
{
	atf_require python3 "Requires python3 to run this test"
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check pkg create -M test.ucl
	mkdir repo
	mv test-1.txz test-1.pkg repo/
	atf_check -o ignore pkg repo repo

	httpd_startup ${TMPDIR}/repo

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: ${url} }
}
EOF
	atf_check -o ignore \
		pkg -C ./pkg.conf update
	atf_check -o match:"Installing test-1" \
		pkg -C ./pkg.conf install -y test
}

simple_repo_cleanup()
{
    httpd_cleanup
}

simple_fetch_body()
{
	atf_require python3 "Requires python3 to run this test"
	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check pkg create -M test.ucl
	mkdir repo
	mv test-1.txz test-1.pkg repo/
	atf_check -o ignore pkg repo repo

	httpd_startup ${TMPDIR}/repo

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: ${url} }
}
EOF
	atf_check -o ignore \
		pkg -C ./pkg.conf update
	atf_check -o match:"test-1" \
		pkg -C ./pkg.conf fetch -y test
}

simple_fetch_cleanup()
{
    httpd_cleanup
}

simple_audit_body()
{
	atf_require python3 "Requires python3 to run this test"

	mkdir rootdir
	cat > ${TMPDIR}/rootdir/meh <<EOF
<?xml version="1.0" encoding="utf-8"?>
<vuxml xmlns="http://www.vuxml.org/apps/vuxml-1">
</vuxml>
EOF
	bzip2 ${TMPDIR}/rootdir/meh

	httpd_startup ${TMPDIR}/rootdir

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
VULNXML_SITE = "${url}/meh.bz2"
EOF

	atf_check -o ignore pkg -C ./pkg.conf audit -F

}
simple_audit_cleanup()
{
    httpd_cleanup
}
