#! /usr/bin/env atf-sh

. $(atf_get_srcdir)/test_environment.sh

CLEANUP="simple_fetch simple_repo simple_audit \
	server_ignores_ims server_sends_304 missing_pkg_port \
	server_truncates_once srv_fallback"

tests_init \
	simple_fetch \
	simple_repo \
	simple_audit \
	server_ignores_ims \
	server_sends_304 \
	missing_pkg_port \
	server_truncates_once \
	srv_fallback


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
	    return 0
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

# Same as httpd_startup() but the server always answers 200, even when the
# client sent an If-Modified-Since header.
httpd_startup_noims()
{
	pidfile=${TMPDIR}/http.pid
	statusfile=${TMPDIR}/http.status
	: > ${pidfile}
	: > ${statusfile}

	cat > ${TMPDIR}/noims.py << 'EOF'
import functools, http.server, sys

class Handler(http.server.SimpleHTTPRequestHandler):
    # always pretend the file changed, ignoring the conditional request
    def send_head(self):
        if 'If-Modified-Since' in self.headers:
            del self.headers['If-Modified-Since']
        return super().send_head()

with http.server.HTTPServer(('127.0.0.1', 0),
        functools.partial(Handler, directory=sys.argv[1])) as httpd:
    print('http://%s:%d' % (httpd.server_address[0], httpd.server_address[1]),
        flush=True)
    httpd.serve_forever()
EOF
	python3 -u ${TMPDIR}/noims.py "$1" > ${statusfile} &
	jobs -p %1 > ${pidfile}
	while kill -s 0 %1 && ! [ -s ${statusfile} ]; do
	    sleep .1
	done
	url=$(cat ${statusfile})
	atf_check test -n "${url}"
}

server_ignores_ims_body()
{
	atf_require python3 "Requires python3 to run this test"

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check pkg create -M test.ucl
	mkdir repo
	mv test-1.txz test-1.pkg repo/
	atf_check -o ignore pkg repo repo

	httpd_startup_noims ${TMPDIR}/repo

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: ${url} }
}
EOF
	atf_check -o ignore pkg -C ./pkg.conf update

	# The server answers 200 with an unchanged Last-Modified instead of
	# 304, the catalogue must not be fetched again.
	atf_check -o match:"local repository is up to date" \
		pkg -C ./pkg.conf update
}

server_ignores_ims_cleanup()
{
    httpd_cleanup
}

# Same as httpd_startup() but also captures the request log so that a test
# can check which status code the server answered.
httpd_startup_log()
{
	pidfile=${TMPDIR}/http.pid
	logfile=${TMPDIR}/http.log
	: > ${pidfile}
	: > ${logfile}

	python3 -u -m http.server -d "$1" --bind 127.0.0.1 0 \
		> ${logfile} 2>&1 &
	jobs -p %1 > ${pidfile}
	while kill -s 0 %1 && ! grep -q "Serving HTTP" ${logfile}; do
	    sleep .1
	done
	url=$(sed -n 's#.*(\(http://.*\)/).*#\1#p' ${logfile})
	atf_check test -n "${url}"
}

server_sends_304_body()
{
	atf_require python3 "Requires python3 to run this test"

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check pkg create -M test.ucl
	mkdir repo
	mv test-1.txz test-1.pkg repo/
	atf_check -o ignore pkg repo repo

	httpd_startup_log ${TMPDIR}/repo

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: ${url} }
}
EOF
	atf_check -o ignore pkg -C ./pkg.conf update

	# The server honours If-Modified-Since: it answers 304 and pkg must
	# report the catalogue as up to date.
	atf_check -o match:"local repository is up to date" \
		pkg -C ./pkg.conf update
	atf_check grep -q " 304 " ${TMPDIR}/http.log
}

server_sends_304_cleanup()
{
    httpd_cleanup
}

# The diagnostics must show the URL as it is used, port included: a repository
# served on a non default port must not be reported without it (issue #1953).
missing_pkg_port_body()
{
	atf_require python3 "Requires python3 to run this test"

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	mkdir repo
	atf_check -o ignore pkg create -o repo -M test.ucl
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

	# The catalogue still references the package, the archive is gone
	atf_check -o ignore rm ${TMPDIR}/repo/test-1.pkg

	atf_check \
		-o ignore \
		-e match:"${url}/test-1.pkg: Not Found" \
		-s ignore \
		pkg -C ./pkg.conf install -y test
}

missing_pkg_port_cleanup()
{
    httpd_cleanup
}

# Same as httpd_startup() but the first transfer of a package is truncated, as
# when the connection dies in the middle of a download.  Range requests are
# honoured, and every request is logged so that a test can count them.
httpd_startup_truncate()
{
	pidfile=${TMPDIR}/http.pid
	statusfile=${TMPDIR}/http.status
	: > ${pidfile}
	: > ${statusfile}
	: > ${TMPDIR}/http.log

	cat > ${TMPDIR}/truncate.py << 'EOF'
import http.server, os, sys

DIR, LOG = sys.argv[1], sys.argv[2]
STATE = {'truncated': False}

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        with open(LOG, 'a') as log:
            log.write('%s %s\n' % (self.path,
                self.headers.get('Range', '-')))

        path = os.path.join(DIR, self.path.lstrip('/'))
        if not os.path.isfile(path):
            self.send_error(404)
            return

        data = open(path, 'rb').read()
        start = 0
        rng = self.headers.get('Range')
        if rng:
            start = int(rng.split('=')[1].split('-')[0])
        body = data[start:]

        self.send_response(206 if rng else 200)
        self.send_header('Content-Length', str(len(body)))
        if rng:
            self.send_header('Content-Range',
                'bytes %d-%d/%d' % (start, len(data) - 1, len(data)))
        self.end_headers()

        # The catalogue files are packages too, only truncate a real one.
        truncatable = (self.path.endswith('.pkg')
            and not self.path.endswith('/data.pkg')
            and not self.path.endswith('/packagesite.pkg'))
        if truncatable and not STATE['truncated']:
            STATE['truncated'] = True
            self.wfile.write(body[:len(body) // 2])
            self.wfile.flush()
            self.close_connection = True
            return

        self.wfile.write(body)

with http.server.HTTPServer(('127.0.0.1', 0), Handler) as httpd:
    print('http://%s:%d' % httpd.server_address[:2], flush=True)
    httpd.serve_forever()
EOF
	python3 -u ${TMPDIR}/truncate.py "$1" ${TMPDIR}/http.log > ${statusfile} &
	jobs -p %1 > ${pidfile}
	while kill -s 0 %1 && ! [ -s ${statusfile} ]; do
	    sleep .1
	done
	url=$(cat ${statusfile})
	atf_check test -n "${url}"
}

# A transfer which dies in the middle is retried, and resumes where it stopped
# instead of downloading the package again from the beginning.
server_truncates_once_body()
{
	atf_require python3 "Requires python3 to run this test"

	atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg test test 1
	atf_check -o ignore pkg create -M test.ucl
	mkdir repo
	mv test-1.pkg repo/
	atf_check -o ignore pkg repo repo

	httpd_startup_truncate ${TMPDIR}/repo

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: ${url} }
}
EOF

	atf_check \
		-o match:"Fetching test-1:  done" \
		-e match:"fetch size does not match expected package size" \
		-s exit:0 \
		pkg -C ./pkg.conf -o FETCH_RETRY=3 fetch -y test

	# The package was requested twice, the second time to resume it.
	atf_check -o inline:"2\n" -s exit:0 \
		sh -c "grep -c '^/test-1.pkg ' ${TMPDIR}/http.log"

	atf_check -o match:"^/test-1.pkg bytes=[0-9]+-$" -s exit:0 \
		sh -c "grep '^/test-1.pkg ' ${TMPDIR}/http.log | tail -1"
}

server_truncates_once_cleanup()
{
	httpd_cleanup
}

# A repository with the SRV discovery of the servers, but without any SRV
# record, falls back on the host of its URL instead of failing, and the failed
# lookup is only done once and not again for every file fetched.
srv_fallback_body()
{
	atf_require python3 "Requires python3 to run this test"

	mkdir repo
	for p in onepkg twopkg; do
		atf_check sh ${RESOURCEDIR}/test_subr.sh new_pkg ${p} ${p} 1
		atf_check -o ignore pkg create -M ${p}.ucl
		mv ${p}-1.pkg repo/
	done
	atf_check -o ignore pkg repo repo

	httpd_startup ${TMPDIR}/repo

	cat > pkg.conf << EOF
PKG_DBDIR=${TMPDIR}
PKG_CACHEDIR=${TMPDIR}/cache
REPOS_DIR=[]
repositories: {
	local: { url: "pkg+${url}", mirror_type: "srv" }
}
EOF

	atf_check \
		-o match:"Installing onepkg-1" \
		-e save:srv.err \
		-s exit:0 \
		pkg -C ./pkg.conf install -y onepkg twopkg

	# One lookup for the whole run, not one per file nor per repository use.
	atf_check -o inline:"1\n" -s exit:0 \
		grep -c "SRV lookup failed" srv.err
}

srv_fallback_cleanup()
{
	httpd_cleanup
}
