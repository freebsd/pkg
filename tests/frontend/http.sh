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

	mkdir -p ${TMPDIR}/rootdir
	cat > ${TMPDIR}/rootdir/meh.json <<EOF
[
    {
        "affected": [
            {
                "package": {
                    "ecosystem": "FreeBSD:ports",
                    "name": "bar"
                },
                "ranges": [
                    {
                        "events": [
                            {
                                "fixed": "1.0"
                            },
                            {
                                "introduced": "0"
                            }
                        ],
                        "type": "ECOSYSTEM"
                    }
                ]
            }
        ],
        "database_specific": {
            "discovery": "2025-11-10T00:00:00Z",
            "references": {
                "bid": [
                    "BBBBBBBB"
                ],
                "certsa": [
                    "CA-XXXX-YY"
                ],
                "certvu": [
                    "CCCCCCCC"
                ],
                "cvename": [
                    "CVE-WWWW-WWWW"
                ],
                "freebsdpr": [
                    "ports/SSSSSSSS"
                ],
                "freebsdsa": [
                    "SA-XX:YY.bar"
                ]
            },
            "vid": "11111111-2222-3333-4444-555555555555"
        },
        "details": "Bar reports:\n\n> Lorem ipsum dolor sit amet,\n>\n> consectetur adipiscing elit,\n>\n> sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.",
        "id": "FreeBSD-2025-0001",
        "modified": "2025-11-12T00:00:00Z",
        "published": "2025-11-11T00:00:00Z",
        "references": [
            {
                "type": "ADVISORY",
                "url": "https://www.securityfocus.com/bid/BBBBBBBB/info"
            },
            {
                "type": "ADVISORY",
                "url": "https://www.cert.org/advisories/CA-XXXX-YY.html"
            },
            {
                "type": "ADVISORY",
                "url": "https://www.kb.cert.org/vuls/id/CCCCCCCC"
            },
            {
                "type": "REPORT",
                "url": "https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=SSSSSSSS"
            },
            {
                "type": "ADVISORY",
                "url": "https://www.freebsd.org/security/advisories/FreeBSD-SA-XX:YY.bar.asc"
            },
            {
                "type": "ADVISORY",
                "url": "https://api.osv.dev/v1/vulns/CVE-WWWW-WWWW"
            },
            {
                "type": "DISCUSSION",
                "url": "https://www.freebsd.org/community/mailinglists/"
            },
            {
                "type": "WEB",
                "url": "https://www.freebsd.org/about/"
            }
        ],
        "schema_version": "1.7.0",
        "summary": "bar -- some vulnerabilities"
    }
]
EOF
	httpd_startup ${TMPDIR}/rootdir

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
OSVF_SITE = "${url}/meh.json"
EOF

	atf_check -o ignore pkg -C ./pkg.conf audit -F
	atf_check -o ignore pkg -C ./pkg.conf audit -F bar-1.1
}
simple_audit_cleanup()
{
    httpd_cleanup
}
