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
#include "private/fetch_ssh.h"

static void
gethttpmirrors(struct pkg_repo *repo, const char *url, bool withdoc) {
	FILE *f;
	char *line = NULL, *walk;
	size_t linecap = 0;
	ssize_t linelen;
	struct http_mirror *m;
	struct url *u;

	if ((f = fetchGetURL(url, "")) == NULL)
		return;

	while ((linelen = getline(&line, &linecap, f)) > 0) {
		if (strncmp(line, "URL:", 4) == 0) {
			walk = line;
			/* trim '\n' */
			if (walk[linelen - 1] == '\n')
				walk[linelen - 1 ] = '\0';

			walk += 4;
			while (isspace(*walk)) {
				walk++;
			}
			if (*walk == '\0')
				continue;

			if ((u = fetchParseURL(walk)) != NULL) {
				m = xmalloc(sizeof(struct http_mirror));
				m->reldoc = withdoc;
				m->url = u;
				LL_APPEND(repo->http, m);
			}
		}
	}

	free(line);
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

	retcode = pkg_fetch_file_to_fd(repo, url, fd, &t, 0, -1, false);

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

	retcode = pkg_fetch_file_to_fd(repo, url, fd, &t, offset, size, false);

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

#define URL_SCHEME_PREFIX	"pkg+"

int
pkg_fetch_file_to_fd(struct pkg_repo *repo, const char *url, int dest,
    time_t *t, ssize_t offset, int64_t size, bool silent)
{
	FILE		*remote = NULL;
	struct url	*u = NULL, *repourl;
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
	char		*doc = NULL, *reldoc;
	char		 docpath[MAXPATHLEN];
	int		 retcode = EPKG_OK;
	char		 zone[MAXHOSTNAMELEN + 24];
	struct dns_srvinfo	*srv_current = NULL;
	struct http_mirror	*http_current = NULL;
	off_t		 sz = 0;
	size_t		 buflen = 0;
	size_t		 left = 0;
	bool		 pkg_url_scheme = false;
	UT_string	*fetchOpts = NULL;

	max_retry = pkg_object_int(pkg_config_get("FETCH_RETRY"));
	fetch_timeout = pkg_object_int(pkg_config_get("FETCH_TIMEOUT"));

	fetchConnectionCacheInit(-1, -1);
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
	repourl = fetchParseURL(repo->url);
	if (repourl == NULL) {
		pkg_emit_error("%s: parse error", url);
		fetchFreeURL(u);
		return (EPKG_FATAL);
	}

	doc = u->doc;
	reldoc = doc + strlen(repourl->doc);
	fetchFreeURL(repourl);

	if (t != NULL)
		u->ims_time = *t;

	if (repo != NULL && strcmp(u->scheme, "ssh") == 0) {
		if ((retcode = ssh_open(repo, u, &sz)) != EPKG_OK)
			goto cleanup;
		remote = repo->ssh;
	}

	while (remote == NULL) {
		if (retry == max_retry) {
			if (repo != NULL && repo->mirror_type == SRV &&
			    (strncmp(u->scheme, "http", 4) == 0
			     || strcmp(u->scheme, "ftp") == 0)) {

				if (!pkg_url_scheme)
					pkg_emit_notice(
     "Warning: use of %s:// URL scheme with SRV records is deprecated: "
     "switch to pkg+%s://", u->scheme, u->scheme);

				if (repo->srv == NULL) {
					snprintf(zone, sizeof(zone),
					    "_%s._tcp.%s", u->scheme, u->host);
					repo->srv = dns_getsrvinfo(zone);
				}
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
					gethttpmirrors(repo, zone, false);
				if (repo->http == NULL)
					gethttpmirrors(repo, repo->url, true);

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
			snprintf(docpath, sizeof(docpath), "%s%s",
			    http_current->url->doc, http_current->reldoc ? reldoc : doc);
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

		if (ctx.debug_level >= 4)
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
				if (!silent)
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
