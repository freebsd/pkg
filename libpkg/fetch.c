/*-
 * Copyright (c) 2012-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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

#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fetch.h>
#include <paths.h>

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
				m = malloc(sizeof(struct http_mirror));
				m->url = u;
				LL_APPEND(repo->http, m);
			}
		}
	}
	fclose(f);
	return;
}

int
pkg_fetch_file(struct pkg_repo *repo, const char *url, char *dest, time_t t)
{
	int fd = -1;
	int retcode = EPKG_FATAL;
	mode_t mask;

	mask = umask(022);
	fd = mkstemp(dest);
	umask(mask);
	if (fd == -1) {
		pkg_emit_errno("mkstemp", dest);
		return(EPKG_FATAL);
	}

	retcode = pkg_fetch_file_to_fd(repo, url, fd, &t);

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
ssh_cache_data(struct pkg_repo *repo, char *src, size_t nbytes)
{
	char *tmp;

	if (repo->sshio.cache.size < nbytes) {
		tmp = realloc(repo->sshio.cache.buf, nbytes);
		if (tmp == NULL)
			return (-1);

		repo->sshio.cache.buf = tmp;
		repo->sshio.cache.size = nbytes;
	}
	memcpy(repo->sshio.cache.buf, src, nbytes);
	repo->sshio.cache.len = nbytes;
	repo->sshio.cache.pos = 0;

	return (0);
}

static int
ssh_read(void *data, char *buf, int len)
{
	struct pkg_repo *repo = (struct pkg_repo *) data;
	struct timeval now, timeout, delta;
	fd_set readfds;
	ssize_t rlen, total;
	char *start;

	pkg_debug(2, "ssh: start reading");

	if (fetchTimeout > 0) {
		gettimeofday(&timeout, NULL);
		timeout.tv_sec += fetchTimeout;
	}

	total = 0;
	start = buf;

	if (repo->sshio.cache.len > 0) {
		/*
		 * The last invocation of fetch_read was interrupted by a
		 * signal after some data had been read from the socket. Copy
		 * the cached data into the supplied buffer before trying to
		 * read from the socket again.
		 */
		total = (repo->sshio.cache.len < (size_t)len) ? repo->sshio.cache.len : (size_t)len;
		memcpy(buf, repo->sshio.cache.buf, total);

		repo->sshio.cache.len -= total;
		repo->sshio.cache.pos += total;
		len -= total;
		buf += total;
	}


	while (len > 0) {
		if (repo->tofetch > 0 && repo->tofetch == repo->fetched)
			break;

		rlen = read(repo->sshio.in, buf, len);
		if (rlen == 0) {
			break;
		} else if (rlen > 0) {
			len -= rlen;
			buf += rlen;
			if (repo->tofetch > 0)
				repo->fetched += rlen;
			total += rlen;
			continue;
		} else if (rlen == -1) {
			if (errno == EINTR)
				ssh_cache_data(repo, start, total);
			if (errno != EAGAIN) {
				pkg_emit_errno("timeout", "ssh");
				return (-1);
			}
			if (errno == EAGAIN && total > 0) {
				break;
			}
		}

		FD_ZERO(&readfds);
		while (!FD_ISSET(repo->sshio.in, &readfds)) {
			FD_SET(repo->sshio.in, &readfds);
			if (fetchTimeout > 0) {
				gettimeofday(&now, NULL);
				if (!timercmp(&timeout, &now, >)) {
					errno = ETIMEDOUT;
					return (-1);
				}
				timersub(&timeout, &now, &delta);
			}
			errno = 0;
			if (select(repo->sshio.in + 1, &readfds, NULL, NULL,
			    fetchTimeout > 0 ? &delta : NULL) < 0) {
				if (errno == EINTR) {
					/* Save anything that was read. */
					ssh_cache_data(repo, start, total);
					continue;
				}
				return (-1);
			}
		}
	}

	pkg_debug(2, "ssh: have read %d bytes", total);
	return (total);
}

static int
ssh_write(void *data, const char *buf, int l)
{
	struct pkg_repo *repo = (struct pkg_repo *)data;

	return (write(repo->sshio.out, buf, l));
}

static int
ssh_close(void *data)
{
	struct pkg_repo *repo = (struct pkg_repo *)data;
	int pstat;

	free(repo->sshio.cache.buf);

	write(repo->sshio.out, "quit\n", 5);

	while (waitpid(repo->sshio.pid, &pstat, 0) == -1) {
		if (errno != EINTR)
			return (EPKG_FATAL);
	}

	repo->ssh = NULL;
	repo->tofetch = 0;
	repo->fetched = 0;

	return (WEXITSTATUS(pstat));
}

static int
start_ssh(struct pkg_repo *repo, struct url *u, off_t *sz)
{
	char *line = NULL;
	ssize_t linecap = 0;
	size_t linelen;
	struct sbuf *cmd = NULL;
	const char *errstr;
	const char *ssh_args;
	int sshin[2];
	int sshout[2];
	const char *argv[4];

	pkg_config_string(PKG_CONFIG_SSH_ARGS, &ssh_args);

	if (repo->ssh == NULL) {
		/* Use socket pair because pipe have blocking issues */
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sshin) <0 ||
		    socketpair(AF_UNIX, SOCK_STREAM, 0, sshout) < 0)
			return(EPKG_FATAL);

		set_nonblocking(sshout[0]);
		set_nonblocking(sshout[1]);
		set_nonblocking(sshin[0]);
		set_nonblocking(sshin[1]);

		repo->sshio.pid = vfork();
		if (repo->sshio.pid == -1) {
			pkg_emit_errno("Cannot fork", "start_ssh");
			return (EPKG_FATAL);
		}

		if (repo->sshio.pid == 0) {
			if (dup2(sshin[0], STDIN_FILENO) < 0 ||
			    close(sshin[1]) < 0 ||
			    close(sshout[0]) < 0 ||
			    dup2(sshout[1], STDOUT_FILENO) < 0) {
				pkg_emit_errno("Cannot prepare pipes", "start_ssh");
				return (EPKG_FATAL);
			}

			cmd = sbuf_new_auto();
			sbuf_cat(cmd, "/usr/bin/ssh -e none -T ");
			if (ssh_args != NULL)
				sbuf_printf(cmd, "%s ", ssh_args);
			if (u->port > 0)
				sbuf_printf(cmd, "-P %d ", u->port);
			if (u->user[0] != '\0')
				sbuf_printf(cmd, "%s@", u->user);
			sbuf_cat(cmd, u->host);
			sbuf_printf(cmd, " pkg ssh");
			sbuf_finish(cmd);
			pkg_debug(1, "Fetch: running '%s'", sbuf_data(cmd));
			argv[0] = _PATH_BSHELL;
			argv[1] = "-c";
			argv[2] = sbuf_data(cmd);
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
			return (EPKG_FATAL);
		}

		repo->sshio.in = sshout[0];
		repo->sshio.out = sshin[1];
		set_nonblocking(repo->sshio.in);

		repo->ssh = funopen(repo, ssh_read, ssh_write, NULL, ssh_close);

		if (getline(&line, &linecap, repo->ssh) > 0) {
			if (strncmp(line, "ok:", 3) != 0) {
				fclose(repo->ssh);
				free(line);
				return (EPKG_FATAL);
			}
		} else {
			fclose(repo->ssh);
			return (EPKG_FATAL);
		}
	}
	fprintf(repo->ssh, "get %s %" PRIdMAX "\n", u->doc, (intmax_t)u->ims_time);
	repo->tofetch = 0;
	if ((linelen = getline(&line, &linecap, repo->ssh)) > 0) {
		if (line[linelen -1 ] == '\n')
			line[linelen -1 ] = '\0';
		if (strncmp(line, "ok:", 3) == 0) {
			*sz = strtonum(line + 4, 0, LONG_MAX, &errstr);
			if (errstr) {
				free(line);
				return (EPKG_FATAL);
			}

			if (*sz == 0) {
				free(line);
				return (EPKG_UPTODATE);
			}

			free(line);
			return (EPKG_OK);
		}

	}

	free(line);
	return (EPKG_FATAL);
}

#define URL_SCHEME_PREFIX	"pkg+"

int
pkg_fetch_file_to_fd(struct pkg_repo *repo, const char *url, int dest, time_t *t)
{
	FILE		*remote = NULL;
	struct url	*u = NULL;
	struct url_stat	 st;
	off_t		 done = 0;
	off_t		 r;

	int64_t		 max_retry, retry;
	int64_t		 fetch_timeout;
	time_t		 begin_dl;
	time_t		 now;
	time_t		 last = 0;
	char		 buf[10240];
	char		*doc = NULL;
	char		 docpath[MAXPATHLEN];
	int		 retcode = EPKG_OK;
	char		 zone[MAXHOSTNAMELEN + 13];
	struct dns_srvinfo	*srv_current = NULL;
	struct http_mirror	*http_current = NULL;
	off_t		 sz = 0;
	bool		 pkg_url_scheme = false;

	if (pkg_config_int64(PKG_CONFIG_FETCH_RETRY, &max_retry) == EPKG_FATAL)
		max_retry = 3;

	if (pkg_config_int64(PKG_CONFIG_FETCH_TIMEOUT, &fetch_timeout) == EPKG_FATAL)
		fetch_timeout = 30;

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
	 * Warn if using plain http://, https:// etc with SRV
	 */

	if (strncmp(URL_SCHEME_PREFIX, url, strlen(URL_SCHEME_PREFIX)) == 0) {
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

	u = fetchParseURL(url);
	if (t != NULL)
		u->ims_time = *t;

	if (strcmp(u->scheme, "ssh") == 0) {
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
				snprintf(zone, sizeof(zone),
				    "%s://%s", u->scheme, u->host);
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
			snprintf(docpath, MAXPATHLEN, "%s%s", http_current->url->doc, doc);
			u->doc = docpath;
			u->port = http_current->url->port;
		}

		pkg_debug(1,"Fetch: fetching from: %s://%s%s%s%s",
		    u->scheme,
		    u->user,
		    u->user[0] != '\0' ? "@" : "",
		    u->host,
		    u->doc);
		remote = fetchXGet(u, &st, "i");
		if (remote == NULL) {
			if (fetchLastErrCode == FETCH_OK) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			}
			--retry;
			if (retry <= 0) {
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
			if (st.mtime < *t) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			} else if (strncmp(u->scheme, "http", 4) == 0)
				*t = st.mtime;
		}
		sz = st.size;
	}

	now = begin_dl = time(NULL);
	if (repo != NULL) {
		repo->tofetch = sz;
		repo->fetched = 0;
	}
	while (done < sz) {
		if ((r = fread(buf, 1, sizeof(buf), remote)) < 1)
			break;

		if (write(dest, buf, r) != r) {
			pkg_emit_errno("write", "");
			retcode = EPKG_FATAL;
			goto cleanup;
		}

		done += r;
		now = time(NULL);
		/* Only call the callback every second */
		if (now > last || done == sz) {
			pkg_emit_fetching(url, sz, done, (now - begin_dl));
			last = now;
		}
	}
	if (repo != NULL)
		repo->tofetch = 0;

	if (done < sz) {
		pkg_emit_error("An error occurred while fetching package");
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	if (strcmp(u->scheme, "ssh") != 0 && ferror(remote)) {
		pkg_emit_error("%s: %s", url, fetchLastErrString);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	cleanup:

	if (u != NULL) {
		if (remote != NULL &&  repo != NULL && remote != repo->ssh)
			fclose(remote);
	}

	/* restore original doc */
	u->doc = doc;

	fetchFreeURL(u);

	return (retcode);
}
