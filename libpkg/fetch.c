/*-
 * Copyright (c) 2012-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fetch.h>
#include <paths.h>
#include <poll.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

static void
gethttpmirrors(struct pkg_repo *repo, const char *url) {
	FILE *f;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	struct http_mirror *m;
	struct url *u;

	if ((f = fetchGetURL(url, "")) == NULL)
		return;

	while ((linelen = getline(&line, &linecap, f)) > 0) {
		if (strncmp(line, "URL:", 4) == 0) {
			/* trim '\n' */
			if (line[linelen - 1] == '\n')
				line[linelen - 1 ] = '\0';

			line += 4;
			while (isspace(*line)) {
				line++;
			}
			if (*line == '\0')
				continue;

			if ((u = fetchParseURL(line)) != NULL) {
				m = xmalloc(sizeof(struct http_mirror));
				m->url = u;
				LL_APPEND(repo->http, m);
			}
		}
	}
	fclose(f);
	return;
}

int
pkg_fetch_file_tmp(struct pkg_repo *repo, const char *url, char *dest,
	time_t t)
{
	int fd = -1;
	int retcode = EPKG_FATAL;

	fd = mkstemp(dest);

	if (fd == -1) {
		pkg_emit_errno("mkstemp", dest);
		return(EPKG_FATAL);
	}

	retcode = pkg_fetch_file_to_fd(repo, url, fd, &t, 0, -1);

	if (t != 0) {
		struct timeval ftimes[2] = {
			{
			.tv_sec = t,
			.tv_usec = 0
			},
			{
			.tv_sec = t,
			.tv_usec = 0
			}
		};
		futimes(fd, ftimes);
	}

	close(fd);

	/* Remove local file if fetch failed */
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

int
pkg_fetch_file(struct pkg_repo *repo, const char *url, char *dest, time_t t,
    ssize_t offset, int64_t size)
{
	int fd = -1;
	int retcode = EPKG_FATAL;

	fd = open(dest, O_CREAT|O_APPEND|O_WRONLY, 00644);
	if (fd == -1) {
		pkg_emit_errno("open", dest);
		return(EPKG_FATAL);
	}

	retcode = pkg_fetch_file_to_fd(repo, url, fd, &t, offset, size);

	if (t != 0) {
		struct timeval ftimes[2] = {
			{
			.tv_sec = t,
			.tv_usec = 0
			},
			{
			.tv_sec = t,
			.tv_usec = 0
			}
		};
		futimes(fd, ftimes);
	}

	close(fd);

	/* Remove local file if fetch failed */
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

static int
ssh_read(void *data, char *buf, int len)
{
	struct pkg_repo *repo = (struct pkg_repo *) data;
	struct timeval now, timeout, delta;
	struct pollfd pfd;
	ssize_t rlen;
	int deltams;

	pkg_debug(2, "ssh: start reading");

	if (fetchTimeout > 0) {
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += fetchTimeout;
	}

	deltams = -1;
	memset(&pfd, 0, sizeof pfd);
	pfd.fd = repo->sshio.in;
	pfd.events = POLLIN | POLLERR;

	for (;;) {
		rlen = read(pfd.fd, buf, len);
		pkg_debug(2, "read %jd", (intmax_t)rlen);
		if (rlen >= 0) {
			break;
		} else if (rlen == -1) {
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN) {
				pkg_emit_errno("timeout", "ssh");
				return (-1);
			}
		}

		/* only EAGAIN should get here */
		if (fetchTimeout > 0) {
			gettimeofday(&now, NULL);
			if (!timercmp(&timeout, &now, >)) {
				errno = ETIMEDOUT;
				return (-1);
			}
			timersub(&timeout, &now, &delta);
			deltams = delta.tv_sec * 1000 +
			    delta.tv_usec / 1000;
		}

		errno = 0;
		pfd.revents = 0;
		pkg_debug(1, "begin poll()");
		if (poll(&pfd, 1, deltams) < 0) {
			if (errno == EINTR)
				continue;
			return (-1);
		}
		pkg_debug(1, "end poll()");


	}

	pkg_debug(2, "ssh: have read %jd bytes", (intmax_t)rlen);

	return (rlen);
}

static int
ssh_writev(int fd, struct iovec *iov, int iovcnt)
{
	struct timeval now, timeout, delta;
	struct pollfd pfd;
	ssize_t wlen, total;
	int deltams;
	struct msghdr msg;

	memset(&pfd, 0, sizeof pfd);

	if (fetchTimeout) {
		pfd.fd = fd;
		pfd.events = POLLOUT | POLLERR;
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += fetchTimeout;
	}

	total = 0;
	while (iovcnt > 0) {
		while (fetchTimeout && pfd.revents == 0) {
			gettimeofday(&now, NULL);
			if (!timercmp(&timeout, &now, >)) {
				errno = ETIMEDOUT;
				return (-1);
			}
			timersub(&timeout, &now, &delta);
			deltams = delta.tv_sec * 1000 +
				delta.tv_usec / 1000;
			errno = 0;
			pfd.revents = 0;
			while (poll(&pfd, 1, deltams) == -1) {
				if (errno == EINTR)
					continue;

				return (-1);
			}
		}
		errno = 0;
		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = iov;
		msg.msg_iovlen = iovcnt;

		wlen = sendmsg(fd, &msg, 0);
		if (wlen == 0) {
			errno = ECONNRESET;
			return (-1);
		}
		else if (wlen < 0)
			return (-1);

		total += wlen;

		while (iovcnt > 0 && wlen >= (ssize_t)iov->iov_len) {
			wlen -= iov->iov_len;
			iov++;
			iovcnt--;
		}

		if (iovcnt > 0) {
			iov->iov_len -= wlen;
			iov->iov_base = __DECONST(char *, iov->iov_base) + wlen;
		}
	}
	return (total);
}

static int
ssh_write(void *data, const char *buf, int l)
{
	struct pkg_repo *repo = (struct pkg_repo *)data;
	struct iovec iov;

	iov.iov_base = __DECONST(char *, buf);
	iov.iov_len = l;

	pkg_debug(1, "writing data");

	return (ssh_writev(repo->sshio.out, &iov, 1));
}

static int
ssh_close(void *data)
{
	struct pkg_repo *repo = (struct pkg_repo *)data;
	int pstat;

	write(repo->sshio.out, "quit\n", 5);

	while (waitpid(repo->sshio.pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (EPKG_FATAL);
	}

	repo->ssh = NULL;

	return (WEXITSTATUS(pstat));
}

static int
start_ssh(struct pkg_repo *repo, struct url *u, off_t *sz)
{
	char *line = NULL;
	size_t linecap = 0;
	size_t linelen;
	UT_string *cmd = NULL;
	const char *errstr;
	const char *ssh_args;
	int sshin[2];
	int sshout[2];
	int retcode = EPKG_FATAL;
	const char *argv[4];

	ssh_args = pkg_object_string(pkg_config_get("PKG_SSH_ARGS"));

	if (repo->ssh == NULL) {
		/* Use socket pair because pipe have blocking issues */
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sshin) <0 ||
		    socketpair(AF_UNIX, SOCK_STREAM, 0, sshout) < 0)
			return(EPKG_FATAL);

		repo->sshio.pid = fork();
		if (repo->sshio.pid == -1) {
			pkg_emit_errno("Cannot fork", "start_ssh");
			goto ssh_cleanup;
		}

		if (repo->sshio.pid == 0) {
			if (dup2(sshin[0], STDIN_FILENO) < 0 ||
			    close(sshin[1]) < 0 ||
			    close(sshout[0]) < 0 ||
			    dup2(sshout[1], STDOUT_FILENO) < 0) {
				pkg_emit_errno("Cannot prepare pipes", "start_ssh");
				goto ssh_cleanup;
			}

			utstring_new(cmd);
			utstring_printf(cmd, "/usr/bin/ssh -e none -T ");
			if (ssh_args != NULL)
				utstring_printf(cmd, "%s ", ssh_args);
			if ((repo->flags & REPO_FLAGS_USE_IPV4) == REPO_FLAGS_USE_IPV4)
				utstring_printf(cmd, "-4 ");
			else if ((repo->flags & REPO_FLAGS_USE_IPV6) == REPO_FLAGS_USE_IPV6)
				utstring_printf(cmd, "-6 ");
			if (u->port > 0)
				utstring_printf(cmd, "-p %d ", u->port);
			if (u->user[0] != '\0')
				utstring_printf(cmd, "%s@", u->user);
			utstring_printf(cmd, "%s", u->host);
			utstring_printf(cmd, " pkg ssh");
			pkg_debug(1, "Fetch: running '%s'", utstring_body(cmd));
			argv[0] = _PATH_BSHELL;
			argv[1] = "-c";
			argv[2] = utstring_body(cmd);
			argv[3] = NULL;

			if (sshin[0] != STDIN_FILENO)
				close(sshin[0]);
			if (sshout[1] != STDOUT_FILENO)
				close(sshout[1]);
			execvp(argv[0], __DECONST(char **, argv));
			/* NOT REACHED */
		}

		if (close(sshout[1]) < 0 || close(sshin[0]) < 0) {
			pkg_emit_errno("Failed to close pipes", "start_ssh");
			goto ssh_cleanup;
		}

		pkg_debug(1, "SSH> connected");

		repo->sshio.in = sshout[0];
		repo->sshio.out = sshin[1];
		set_nonblocking(repo->sshio.in);

		repo->ssh = funopen(repo, ssh_read, ssh_write, NULL, ssh_close);
		if (repo->ssh == NULL) {
			pkg_emit_errno("Failed to open stream", "start_ssh");
			goto ssh_cleanup;
		}

		if (getline(&line, &linecap, repo->ssh) > 0) {
			if (strncmp(line, "ok:", 3) != 0) {
				pkg_debug(1, "SSH> server rejected, got: %s", line);
				goto ssh_cleanup;
			}
			pkg_debug(1, "SSH> server is: %s", line +4);
		} else {
			pkg_debug(1, "SSH> nothing to read, got: %s", line);
			goto ssh_cleanup;
		}
	}
	pkg_debug(1, "SSH> get %s %" PRIdMAX "", u->doc, (intmax_t)u->ims_time);
	fprintf(repo->ssh, "get %s %" PRIdMAX "\n", u->doc, (intmax_t)u->ims_time);
	if ((linelen = getline(&line, &linecap, repo->ssh)) > 0) {
		if (line[linelen -1 ] == '\n')
			line[linelen -1 ] = '\0';

		pkg_debug(1, "SSH> recv: %s", line);
		if (strncmp(line, "ok:", 3) == 0) {
			*sz = strtonum(line + 4, 0, LONG_MAX, &errstr);
			if (errstr) {
				goto ssh_cleanup;
			}

			if (*sz == 0) {
				retcode = EPKG_UPTODATE;
				goto ssh_cleanup;
			}

			retcode = EPKG_OK;
			goto ssh_cleanup;
		}
	}

ssh_cleanup:
	if (retcode == EPKG_FATAL && repo->ssh != NULL) {
		fclose(repo->ssh);
		repo->ssh = NULL;
	}
	if (cmd != NULL)
		utstring_free(cmd);
	free(line);
	return (retcode);
}

#define URL_SCHEME_PREFIX	"pkg+"

int
pkg_fetch_file_to_fd(struct pkg_repo *repo, const char *url, int dest,
    time_t *t, ssize_t offset, int64_t size)
{
	FILE		*remote = NULL;
	struct url	*u = NULL;
	struct url_stat	 st;
	struct pkg_kv	*kv, *kvtmp;
	struct pkg_kv	*envtorestore = NULL;
	struct pkg_kv	*envtounset = NULL;
	char		*tmp;
	off_t		 done = 0;
	off_t		 r;
	int64_t		 max_retry, retry;
	int64_t		 fetch_timeout;
	char		 buf[8192];
	char		*doc = NULL;
	char		 docpath[MAXPATHLEN];
	int		 retcode = EPKG_OK;
	char		 zone[MAXHOSTNAMELEN + 13];
	struct dns_srvinfo	*srv_current = NULL;
	struct http_mirror	*http_current = NULL;
	off_t		 sz = 0;
	size_t		 buflen = 0;
	size_t		 left = 0;
	bool		 pkg_url_scheme = false;
	UT_string	*fetchOpts = NULL;

	max_retry = pkg_object_int(pkg_config_get("FETCH_RETRY"));
	fetch_timeout = pkg_object_int(pkg_config_get("FETCH_TIMEOUT"));

	fetchTimeout = (int) fetch_timeout;

	retry = max_retry;

	/* A URL of the form http://host.example.com/ where
	 * host.example.com does not resolve as a simple A record is
	 * not valid according to RFC 2616 Section 3.2.2.  Our usage
	 * with SRV records is incorrect.  However it is encoded into
	 * /usr/sbin/pkg in various releases so we can't just drop it.
         *
         * Instead, introduce new pkg+http://, pkg+https://,
	 * pkg+ssh://, pkg+ftp://, pkg+file:// to support the
	 * SRV-style server discovery, and also to allow eg. Firefox
	 * to run pkg-related stuff given a pkg+foo:// URL.
	 *
	 * Error if using plain http://, https:// etc with SRV
	 */

	if (repo != NULL &&
		strncmp(URL_SCHEME_PREFIX, url, strlen(URL_SCHEME_PREFIX)) == 0) {
		if (repo->mirror_type != SRV) {
			pkg_emit_error("packagesite URL error for %s -- "
				       URL_SCHEME_PREFIX
				       ":// implies SRV mirror type", url);

			/* Too early for there to be anything to cleanup */
			return(EPKG_FATAL);
		}

		url += strlen(URL_SCHEME_PREFIX);
		pkg_url_scheme = true;
	}

	if (repo != NULL) {
		LL_FOREACH(repo->env, kv) {
			kvtmp = xcalloc(1, sizeof(*kvtmp));
			kvtmp->key = xstrdup(kv->key);
			if ((tmp = getenv(kv->key)) != NULL) {
				kvtmp->value = xstrdup(tmp);
				DL_APPEND(envtorestore, kvtmp);
			} else {
				DL_APPEND(envtounset, kvtmp);
			}
			setenv(kv->key, kv->value, 1);
		}
	}

	u = fetchParseURL(url);
	if (u == NULL) {
		pkg_emit_error("%s: parse error", url);
		/* Too early for there to be anything to cleanup */
		return(EPKG_FATAL);
	}

	if (t != NULL)
		u->ims_time = *t;

	if (repo != NULL && strcmp(u->scheme, "ssh") == 0) {
		if ((retcode = start_ssh(repo, u, &sz)) != EPKG_OK)
			goto cleanup;
		remote = repo->ssh;
	}

	doc = u->doc;
	while (remote == NULL) {
		if (retry == max_retry) {
			if (repo != NULL && repo->mirror_type == SRV &&
			    (strncmp(u->scheme, "http", 4) == 0
			     || strcmp(u->scheme, "ftp") == 0)) {

				if (!pkg_url_scheme)
					pkg_emit_notice(
     "Warning: use of %s:// URL scheme with SRV records is deprecated: "
     "switch to pkg+%s://", u->scheme, u->scheme);

				snprintf(zone, sizeof(zone),
				    "_%s._tcp.%s", u->scheme, u->host);
				if (repo->srv == NULL)
					repo->srv = dns_getsrvinfo(zone);
				srv_current = repo->srv;
			} else if (repo != NULL && repo->mirror_type == HTTP &&
			           strncmp(u->scheme, "http", 4) == 0) {
				if (u->port == 0) {
					if (strcmp(u->scheme, "https") == 0)
						u->port = 443;
					else
						u->port = 80;
				}
				snprintf(zone, sizeof(zone),
				    "%s://%s:%d", u->scheme, u->host, u->port);
				if (repo->http == NULL)
					gethttpmirrors(repo, zone);
				http_current = repo->http;
			}
		}

		if (repo != NULL && repo->mirror_type == SRV && repo->srv != NULL) {
			strlcpy(u->host, srv_current->host, sizeof(u->host));
			u->port = srv_current->port;
		}
		else if (repo != NULL && repo->mirror_type == HTTP && repo->http != NULL) {
			strlcpy(u->scheme, http_current->url->scheme, sizeof(u->scheme));
			strlcpy(u->host, http_current->url->host, sizeof(u->host));
			snprintf(docpath, sizeof(docpath), "%s%s", http_current->url->doc, doc);
			u->doc = docpath;
			u->port = http_current->url->port;
		}

		utstring_new(fetchOpts);
		utstring_printf(fetchOpts, "i");
		if (repo != NULL) {
			if ((repo->flags & REPO_FLAGS_USE_IPV4) ==
			    REPO_FLAGS_USE_IPV4)
				utstring_printf(fetchOpts, "4");
			else if ((repo->flags & REPO_FLAGS_USE_IPV6) ==
			    REPO_FLAGS_USE_IPV6)
				utstring_printf(fetchOpts, "6");
		}

		if (debug_level >= 4)
			utstring_printf(fetchOpts, "v");

		pkg_debug(1,"Fetch: fetching from: %s://%s%s%s%s with opts \"%s\"",
		    u->scheme,
		    u->user,
		    u->user[0] != '\0' ? "@" : "",
		    u->host,
		    u->doc,
		    utstring_body(fetchOpts));

		if (offset > 0)
			u->offset = offset;
		remote = fetchXGet(u, &st, utstring_body(fetchOpts));
		utstring_free(fetchOpts);
		if (remote == NULL) {
			if (fetchLastErrCode == FETCH_OK) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			}
			--retry;
			if (retry <= 0 || fetchLastErrCode == FETCH_UNAVAIL) {
				pkg_emit_error("%s: %s", url,
				    fetchLastErrString);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			if (repo != NULL && repo->mirror_type == SRV && repo->srv != NULL) {
				srv_current = srv_current->next;
				if (srv_current == NULL)
					srv_current = repo->srv;
			} else if (repo != NULL && repo->mirror_type == HTTP && repo->http != NULL) {
				http_current = repo->http->next;
				if (http_current == NULL)
					http_current = repo->http;
			} else {
				sleep(1);
			}
		}
	}

	if (strcmp(u->scheme, "ssh") != 0) {
		if (t != NULL && st.mtime != 0) {
			if (st.mtime <= *t) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			} else
				*t = st.mtime;
		}
		sz = st.size;
	}

	if (sz <= 0 && size > 0)
		sz = size;

	pkg_emit_fetch_begin(url);
	pkg_emit_progress_start(NULL);
	if (offset > 0)
		done += offset;
	buflen = sizeof(buf);
	left = sizeof(buf);
	if (sz > 0)
		left = sz - done;
	while ((r = fread(buf, 1, left < buflen ? left : buflen, remote)) > 0) {
		if (write(dest, buf, r) != r) {
			pkg_emit_errno("write", "");
			retcode = EPKG_FATAL;
			goto cleanup;
		}
		done += r;
		if (sz > 0) {
			left -= r;
			pkg_debug(4, "Read status: %jd over %jd", (intmax_t)done, (intmax_t)sz);
		} else
			pkg_debug(4, "Read status: %jd", (intmax_t)done);
		if (sz > 0)
			pkg_emit_progress_tick(done, sz);
	}

	if (r != 0) {
		pkg_emit_error("An error occurred while fetching package");
		retcode = EPKG_FATAL;
		goto cleanup;
	} else {
		pkg_emit_progress_tick(done, done);
	}
	pkg_emit_fetch_finished(url);

	if (strcmp(u->scheme, "ssh") != 0 && ferror(remote)) {
		pkg_emit_error("%s: %s", url, fetchLastErrString);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

cleanup:
	if (repo != NULL) {
		LL_FOREACH_SAFE(envtorestore, kv, kvtmp) {
			setenv(kv->key, kv->value, 1);
			LL_DELETE(envtorestore, kv);
			pkg_kv_free(kv);
		}
		LL_FOREACH_SAFE(envtounset, kv, kvtmp) {
			unsetenv(kv->key);
			pkg_kv_free(kv);
		}
	}

	if (u != NULL) {
		if (remote != NULL &&  repo != NULL && remote != repo->ssh)
			fclose(remote);
	}

	if (retcode == EPKG_OK) {
		struct timeval ftimes[2] = {
			{
			.tv_sec = *t,
			.tv_usec = 0
			},
			{
			.tv_sec = *t,
			.tv_usec = 0
			}
		};
		futimes(dest, ftimes);
	}

	/* restore original doc */
	u->doc = doc;
	fetchFreeURL(u);

	return (retcode);
}
