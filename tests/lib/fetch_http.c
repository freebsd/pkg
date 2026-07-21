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
#include <unistd.h>

#include <pkg.h>
#include <private/pkg.h>
#include <private/fetch.h>

/*
 * These tests exercise the HTTP-mirror failover logic in libfetch_open()
 * directly, without running a real HTTP server:
 *
 *  - a "dead" mirror is an http:// URL to 127.0.0.1:1, where nothing listens,
 *    so the connection is refused immediately (a retryable FETCH_DOWN);
 *  - an "alive" mirror is a file:// URL pointing at a real local file, which
 *    libfetch can open without any network.
 *
 * The mirror list is pre-populated via pkg_repo_http_mirror_append(), so the
 * mirrorlist-discovery step is skipped; only the server-walking loop runs.
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
make_http_repo(const char *dead_url)
{
	struct pkg_repo repo = {0};

	repo.mirror_type = HTTP;
	/* Only used to compute the relative document path; never fetched. */
	repo.url = "http://pkg.test/";
	(void)dead_url;
	return (repo);
}

ATF_TC_WITHOUT_HEAD(http_mirror_failover);
ATF_TC_WITHOUT_HEAD(http_mirror_all_dead);

/*
 * The first mirror is unreachable; pkg must exhaust its per-server retry
 * budget on it, then advance to the second mirror and succeed. With the
 * pre-fix code the walking pointer was reset to the head every iteration,
 * so the second mirror was never tried and this would fail.
 *
 * A minimal HTTP server is forked on a random localhost port to serve the
 * "target" file; the bundled libfetch only supports http/https, so we
 * cannot use file:// URLs as the alive mirror.
 */
ATF_TC_BODY(http_mirror_failover, tc)
{
	char dir[] = "/tmp/pkg_http_mirror_XXXXXX";
	struct pkg_repo repo;
	struct fetch_item fi = {0};
	char alive_url[MAXPATHLEN];
	int ret, listen_fd, port;
	struct sockaddr_in addr;
	socklen_t addrlen;
	pid_t pid;

	ATF_REQUIRE(mkdtemp(dir) != NULL);
	make_repo_file(dir, "target", "hello\n");

	/* Start a minimal HTTP server on a random localhost port */
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
	port = ntohs(addr.sin_port);

	/* Fork a child to handle one HTTP request then exit */
	pid = fork();
	ATF_REQUIRE_MSG(pid != -1, "fork: %s", strerror(errno));

	if (pid == 0) {
		struct sockaddr_in caddr;
		socklen_t clen = sizeof(caddr);
		int cfd;
		char req[4096];
		ssize_t rlen;
		char path[MAXPATHLEN];
		int ffd;
		char fbuf[4096];
		ssize_t flen;
		char hdr[256];

		cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
		if (cfd < 0)
			_exit(1);
		rlen = read(cfd, req, sizeof(req) - 1);
		if (rlen <= 0)
			_exit(1);
		req[rlen] = '\0';

		/* Parse "GET /path HTTP/1.x" */
		char *p = strchr(req, ' ');
		if (p == NULL)
			_exit(1);
		p++;
		char *e = strchr(p, ' ');
		if (e == NULL)
			_exit(1);
		*e = '\0';

		snprintf(path, sizeof(path), "%s%s", dir, p);
		ffd = open(path, O_RDONLY);
		if (ffd < 0) {
			snprintf(hdr, sizeof(hdr),
			    "HTTP/1.1 404 Not Found\r\n"
			    "Content-Length: 0\r\n"
			    "Connection: close\r\n\r\n");
			write(cfd, hdr, strlen(hdr));
			close(cfd);
			_exit(1);
		}
		flen = read(ffd, fbuf, sizeof(fbuf));
		close(ffd);
		snprintf(hdr, sizeof(hdr),
		    "HTTP/1.1 200 OK\r\n"
		    "Content-Length: %zd\r\n"
		    "Connection: close\r\n\r\n", flen);
		write(cfd, hdr, strlen(hdr));
		write(cfd, fbuf, flen);
		close(cfd);
		_exit(0);
	}

	close(listen_fd);

	ATF_REQUIRE_EQ(EPKG_OK, pkg_ini(NULL, NULL, 0));

	repo = make_http_repo(NULL);
	pkg_repo_http_mirror_append(&repo, "http://127.0.0.1:1/", false);
	snprintf(alive_url, sizeof(alive_url), "http://127.0.0.1:%d/", port);
	pkg_repo_http_mirror_append(&repo, alive_url, true);

	fi.url = "http://pkg.test/target";

	ret = libfetch_open(&repo, &fi);
	ATF_REQUIRE_EQ_MSG(EPKG_OK, ret,
	    "failover to the second mirror failed: %d", ret);
	ATF_REQUIRE_MSG(repo.fh != NULL, "no fetch handle after success");

	libfetch_cleanup(&repo);
	pkg_shutdown();

	waitpid(pid, NULL, 0);
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

	repo = make_http_repo(NULL);
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

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, http_mirror_failover);
	ATF_TP_ADD_TC(tp, http_mirror_all_dead);

	return (atf_no_error());
}