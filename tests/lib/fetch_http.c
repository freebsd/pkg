/*-
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <atf-c.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pkg.h>
#include <private/pkg.h>
#include <private/fetch.h>

/*
 * These tests exercise the HTTP-mirror logic in libfetch_open() directly:
 *
 *  - a "dead" mirror is an http:// URL to 127.0.0.1:1, where nothing listens,
 *    so the connection is refused immediately (a retryable FETCH_DOWN);
 *  - an "alive" mirror is a small HTTP server forked on a random localhost
 *    port (see http_server_start()), answering 200, 404 or 304 as the test
 *    requires.
 *
 * The mirror list is pre-populated via pkg_repo_http_mirror_append(), so the
 * mirrorlist-discovery step is skipped; only the server-walking loop runs.
 *
 * While a mirror is in use u->doc points at a stack buffer, so every exit of
 * that loop has to hand libfetch back its own pointer before releasing the
 * URL. Each case below leaves libfetch_open() through a different exit.
 */

static void
make_repo_file(const char *dir, const char *name, const char *content)
{
	int fd;

	fd = openat(open(dir, O_DIRECTORY), name, O_CREAT | O_WRONLY, 0644);
	ATF_REQUIRE_MSG(fd != -1, "openat %s/%s: %s", dir, name,
	    strerror(errno));
	if (content != NULL)
		ATF_REQUIRE(write(fd, content, strlen(content)) != -1);
	close(fd);
}

static struct pkg_repo
make_http_repo(void)
{
	struct pkg_repo repo = {0};

	repo.mirror_type = HTTP;
	/* Only used to compute the relative document path; never fetched. */
	repo.url = "http://pkg.test/";
	return (repo);
}

enum http_reply {
	HTTP_REPLY_OK,
	HTTP_REPLY_NOT_FOUND,
	HTTP_REPLY_NOT_MODIFIED,
};

struct http_server {
	pid_t	pid;
	int	port;
};

/*
 * Child side of the test server: handle one request, answer according to
 * reply, then exit. last_modified, when not NULL, is sent as the
 * Last-Modified header of a 200 reply.
 */
static void
http_server_serve(int listen_fd, const char *dir, enum http_reply reply,
    const char *last_modified)
{
	struct sockaddr_in caddr;
	socklen_t clen = sizeof(caddr);
	char req[4096], path[MAXPATHLEN], fbuf[4096], hdr[512];
	size_t rlen = 0;
	ssize_t flen;
	char *p, *e;
	int cfd, ffd;

	signal(SIGPIPE, SIG_IGN);

	cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
	if (cfd < 0)
		_exit(1);

	/*
	 * Read the full request before replying. libfetch writes the
	 * request line by line (GET, Host, Accept, ...), each with a
	 * separate write(); if we reply after a single read we may
	 * close the socket while libfetch is still writing the
	 * remaining headers, which makes it die of SIGPIPE. Keep
	 * reading until we have the end-of-headers marker.
	 */
	for (;;) {
		ssize_t n = read(cfd, req + rlen, sizeof(req) - 1 - rlen);

		if (n <= 0)
			_exit(1);
		rlen += (size_t)n;
		req[rlen] = '\0';
		if (strstr(req, "\r\n\r\n") != NULL)
			break;
		if (rlen >= sizeof(req) - 1)
			break;
	}

	if (reply == HTTP_REPLY_NOT_MODIFIED) {
		snprintf(hdr, sizeof(hdr),
		    "HTTP/1.1 304 Not Modified\r\n"
		    "Connection: close\r\n\r\n");
		write(cfd, hdr, strlen(hdr));
		close(cfd);
		_exit(0);
	}

	if (reply == HTTP_REPLY_OK) {
		/* Parse "GET /path HTTP/1.x" */
		p = strchr(req, ' ');
		if (p == NULL)
			_exit(1);
		p++;
		e = strchr(p, ' ');
		if (e == NULL)
			_exit(1);
		*e = '\0';

		snprintf(path, sizeof(path), "%s%s", dir, p);
		ffd = open(path, O_RDONLY);
		if (ffd >= 0) {
			flen = read(ffd, fbuf, sizeof(fbuf));
			if (flen < 0)
				flen = 0;
			close(ffd);
			if (last_modified != NULL)
				snprintf(hdr, sizeof(hdr),
				    "HTTP/1.1 200 OK\r\n"
				    "Content-Length: %zd\r\n"
				    "Last-Modified: %s\r\n"
				    "Connection: close\r\n\r\n",
				    flen, last_modified);
			else
				snprintf(hdr, sizeof(hdr),
				    "HTTP/1.1 200 OK\r\n"
				    "Content-Length: %zd\r\n"
				    "Connection: close\r\n\r\n", flen);
			write(cfd, hdr, strlen(hdr));
			write(cfd, fbuf, flen);
			close(cfd);
			_exit(0);
		}
	}

	snprintf(hdr, sizeof(hdr),
	    "HTTP/1.1 404 Not Found\r\n"
	    "Content-Length: 0\r\n"
	    "Connection: close\r\n\r\n");
	write(cfd, hdr, strlen(hdr));
	close(cfd);
	_exit(0);
}

/*
 * Fork a test server on a random localhost port for a single request.
 */
static struct http_server
http_server_start(const char *dir, enum http_reply reply,
    const char *last_modified)
{
	struct sockaddr_in addr;
	struct http_server srv;
	socklen_t addrlen;
	int listen_fd;

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	ATF_REQUIRE_MSG(listen_fd != -1, "socket: %s", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	ATF_REQUIRE_MSG(bind(listen_fd, (struct sockaddr *)&addr,
	    sizeof(addr)) == 0, "bind: %s", strerror(errno));
	ATF_REQUIRE_MSG(listen(listen_fd, 1) == 0,
	    "listen: %s", strerror(errno));

	addrlen = sizeof(addr);
	ATF_REQUIRE_MSG(getsockname(listen_fd, (struct sockaddr *)&addr,
	    &addrlen) == 0, "getsockname: %s", strerror(errno));
	srv.port = ntohs(addr.sin_port);

	srv.pid = fork();
	ATF_REQUIRE_MSG(srv.pid != -1, "fork: %s", strerror(errno));

	if (srv.pid == 0) {
		http_server_serve(listen_fd, dir, reply, last_modified);
		_exit(1);
	}
	close(listen_fd);

	return (srv);
}

static void
http_server_stop(struct http_server *srv)
{
	waitpid(srv->pid, NULL, 0);
}

ATF_TC_WITHOUT_HEAD(http_mirror_failover);
ATF_TC_WITHOUT_HEAD(http_mirror_all_dead);
ATF_TC_WITHOUT_HEAD(http_mirror_zero_retry);
ATF_TC_WITHOUT_HEAD(http_mirror_not_found);
ATF_TC_WITHOUT_HEAD(http_mirror_uptodate_mtime);
ATF_TC_WITHOUT_HEAD(http_mirror_uptodate_not_modified);

/*
 * The first mirror is unreachable; pkg must exhaust its per-server retry
 * budget on it, then advance to the second mirror and succeed. With the
 * pre-fix code the walking pointer was reset to the head every iteration,
 * so the second mirror was never tried and this would fail.
 */
ATF_TC_BODY(http_mirror_failover, tc)
{
	char dir[] = "/tmp/pkg_http_mirror_XXXXXX";
	struct http_server srv;
	struct pkg_repo repo;
	struct fetch_item fi = {0};
	char alive_url[MAXPATHLEN];
	int ret;

	ATF_REQUIRE(mkdtemp(dir) != NULL);
	make_repo_file(dir, "target", "hello\n");

	srv = http_server_start(dir, HTTP_REPLY_OK, NULL);

	/* Ignore SIGPIPE like pkg does, so a write to a socket closed by
	 * the test HTTP server does not terminate the test case. */
	signal(SIGPIPE, SIG_IGN);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));

	repo = make_http_repo();
	pkg_repo_http_mirror_append(&repo, "http://127.0.0.1:1/", false);
	snprintf(alive_url, sizeof(alive_url), "http://127.0.0.1:%d/",
	    srv.port);
	pkg_repo_http_mirror_append(&repo, alive_url, true);

	fi.url = "http://pkg.test/target";

	ret = libfetch_open(&repo, &fi);
	ATF_REQUIRE_EQ_MSG(EPKG_OK, ret,
	    "failover to the second mirror failed: %d", ret);
	ATF_REQUIRE_MSG(repo.fh != NULL, "no fetch handle after success");

	libfetch_cleanup(&repo);
	pkg_shutdown();

	http_server_stop(&srv);
	unlinkat(open(dir, O_DIRECTORY), "target", 0);
	rmdir(dir);
}

/*
 * Every mirror is unreachable; pkg must walk the whole list, give each
 * server its retry budget, then fail cleanly with EPKG_FATAL.
 */
ATF_TC_BODY(http_mirror_all_dead, tc)
{
	struct pkg_repo repo;
	struct fetch_item fi = {0};
	int ret;

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));

	repo = make_http_repo();
	pkg_repo_http_mirror_append(&repo, "http://127.0.0.1:1/", false);
	pkg_repo_http_mirror_append(&repo, "http://127.0.0.1:2/", false);

	fi.url = "http://pkg.test/target";

	ret = libfetch_open(&repo, &fi);
	ATF_REQUIRE_EQ_MSG(EPKG_FATAL, ret,
	    "expected EPKG_FATAL when all mirrors are down, got %d", ret);
	ATF_REQUIRE_MSG(repo.fh == NULL, "handle should not be open on failure");

	libfetch_cleanup(&repo);
	pkg_shutdown();
}

/*
 * With FETCH_RETRY=0 every failed server must be tried exactly once and the
 * loop must terminate. Before the fix, retry was decremented to -1, the
 * "retry > 0" branch was never taken and the "retry <= 0" case was not
 * handled, leading to an infinite loop.
 */
ATF_TC_BODY(http_mirror_zero_retry, tc)
{
	struct pkg_repo repo;
	struct fetch_item fi = {0};
	int ret;

	ATF_REQUIRE_EQ(0, setenv("FETCH_RETRY", "0", 1));
	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));

	repo = make_http_repo();
	pkg_repo_http_mirror_append(&repo, "http://127.0.0.1:1/", false);

	fi.url = "http://pkg.test/target";

	ret = libfetch_open(&repo, &fi);
	ATF_REQUIRE_EQ_MSG(EPKG_FATAL, ret,
	    "expected EPKG_FATAL with FETCH_RETRY=0, got %d", ret);
	ATF_REQUIRE_MSG(repo.fh == NULL, "handle should not be open on failure");

	libfetch_cleanup(&repo);
	pkg_shutdown();
}

/*
 * The mirror answers 404 for the requested document: libfetch_open() must
 * report EPKG_ENOENT and release the URL, whose document path still points
 * at the on-stack buffer of the mirror walk.
 */
ATF_TC_BODY(http_mirror_not_found, tc)
{
	struct http_server srv;
	struct pkg_repo repo;
	struct fetch_item fi = {0};
	char url[MAXPATHLEN];
	int ret;

	srv = http_server_start(NULL, HTTP_REPLY_NOT_FOUND, NULL);
	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));

	repo = make_http_repo();
	repo.silent = true;
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/", srv.port);
	pkg_repo_http_mirror_append(&repo, url, true);
	fi.url = "http://pkg.test/missing";

	ret = libfetch_open(&repo, &fi);
	ATF_REQUIRE_EQ_MSG(EPKG_ENOENT, ret, "expected EPKG_ENOENT, got %d",
	    ret);
	ATF_REQUIRE_MSG(repo.fh == NULL, "handle should not be open on failure");

	libfetch_cleanup(&repo);
	pkg_shutdown();
	http_server_stop(&srv);
}

/*
 * The mirror answers 200 to the conditional request but the document is
 * older than the requested mtime: libfetch_open() must report EPKG_UPTODATE
 * without leaving a handle open.
 */
ATF_TC_BODY(http_mirror_uptodate_mtime, tc)
{
	char dir[] = "/tmp/pkg_http_mirror_XXXXXX";
	struct http_server srv;
	struct pkg_repo repo;
	struct fetch_item fi = {0};
	char url[MAXPATHLEN];
	int ret;

	ATF_REQUIRE(mkdtemp(dir) != NULL);
	make_repo_file(dir, "target", "hello\n");
	srv = http_server_start(dir, HTTP_REPLY_OK,
	    "Wed, 01 Jan 2020 00:00:00 GMT");
	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));

	repo = make_http_repo();
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/", srv.port);
	pkg_repo_http_mirror_append(&repo, url, true);
	fi.url = "http://pkg.test/target";
	fi.mtime = time(NULL);

	ret = libfetch_open(&repo, &fi);
	ATF_REQUIRE_EQ_MSG(EPKG_UPTODATE, ret, "expected EPKG_UPTODATE, got %d",
	    ret);
	ATF_REQUIRE_MSG(repo.fh == NULL,
	    "handle should not be open when up to date");

	libfetch_cleanup(&repo);
	pkg_shutdown();
	http_server_stop(&srv);
	unlinkat(open(dir, O_DIRECTORY), "target", 0);
	rmdir(dir);
}

/*
 * The mirror answers 304: libfetch reports it as FETCH_OK without a handle,
 * so libfetch_open() must report EPKG_UPTODATE.
 */
ATF_TC_BODY(http_mirror_uptodate_not_modified, tc)
{
	struct http_server srv;
	struct pkg_repo repo;
	struct fetch_item fi = {0};
	char url[MAXPATHLEN];
	int ret;

	srv = http_server_start(NULL, HTTP_REPLY_NOT_MODIFIED, NULL);
	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));

	repo = make_http_repo();
	snprintf(url, sizeof(url), "http://127.0.0.1:%d/", srv.port);
	pkg_repo_http_mirror_append(&repo, url, true);
	fi.url = "http://pkg.test/target";
	fi.mtime = time(NULL);

	ret = libfetch_open(&repo, &fi);
	ATF_REQUIRE_EQ_MSG(EPKG_UPTODATE, ret, "expected EPKG_UPTODATE, got %d",
	    ret);
	ATF_REQUIRE_MSG(repo.fh == NULL,
	    "handle should not be open when up to date");

	libfetch_cleanup(&repo);
	pkg_shutdown();
	http_server_stop(&srv);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, http_mirror_failover);
	ATF_TP_ADD_TC(tp, http_mirror_all_dead);
	ATF_TP_ADD_TC(tp, http_mirror_zero_retry);
	ATF_TP_ADD_TC(tp, http_mirror_not_found);
	ATF_TP_ADD_TC(tp, http_mirror_uptodate_mtime);
	ATF_TP_ADD_TC(tp, http_mirror_uptodate_not_modified);

	return (atf_no_error());
}