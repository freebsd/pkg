/*-
 * Copyright (c) 2020-2022 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <xstring.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/fetch.h"
#include "private/utils.h"

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

static int
fetch_connect(struct pkg_repo *repo, struct url *u)
{
	struct url *repourl;
	xstring *fetchOpts = NULL;
	int64_t max_retry, retry;
	int64_t fetch_timeout;
	int retcode = EPKG_OK;
	char docpath[MAXPATHLEN];
	char zone[MAXHOSTNAMELEN + 24];
	char *doc, *reldoc, *opts;
	struct dns_srvinfo *srv_current = NULL;
	struct http_mirror *http_current = NULL;
	struct url_stat st;

	max_retry = pkg_object_int(pkg_config_get("FETCH_RETRY"));
	fetch_timeout = pkg_object_int(pkg_config_get("FETCH_TIMEOUT"));

	fetchTimeout = (int)MIN(fetch_timeout, INT_MAX);

	repourl = fetchParseURL(repo->url);
	if (repourl == NULL) {
		pkg_emit_error("%s: parse error", repo->url);
		fetchFreeURL(u);
		return (EPKG_FATAL);
	}
	retry = max_retry;
	doc = u->doc;
	reldoc = doc + strlen(repourl->doc);
	fetchFreeURL(repourl);
	pkg_debug(1, "Fetch > libfetch: connecting");

	while (repo->fh == NULL) {
		if (repo != NULL && repo->mirror_type == SRV &&
		    (strncmp(u->scheme, "http", 4) == 0)) {
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
		if (repo != NULL && repo->mirror_type == SRV && repo->srv != NULL) {
			strlcpy(u->host, srv_current->host, sizeof(u->host));
			u->port = srv_current->port;
		} else if (repo != NULL && repo->mirror_type == HTTP && repo->http != NULL) {
			strlcpy(u->scheme, http_current->url->scheme, sizeof(u->scheme));
			strlcpy(u->host, http_current->url->host, sizeof(u->host));
			snprintf(docpath, sizeof(docpath), "%s%s",
			    http_current->url->doc, http_current->reldoc ? reldoc : doc);
			u->doc = docpath;
			u->port = http_current->url->port;
		}
		fetchOpts = xstring_new();
		fputs("i", fetchOpts->fp);
		if (repo != NULL) {
			if ((repo->flags & REPO_FLAGS_USE_IPV4) ==
			    REPO_FLAGS_USE_IPV4)
				fputs("4", fetchOpts->fp);
			else if ((repo->flags & REPO_FLAGS_USE_IPV6) ==
			    REPO_FLAGS_USE_IPV6)
				fputs("6", fetchOpts->fp);
		}

		if (ctx.debug_level >= 4)
			fputs("v", fetchOpts->fp);

		opts = xstring_get(fetchOpts);
		pkg_debug(1,"Fetch: fetching from: %s://%s%s%s%s with opts \"%s\"",
		    u->scheme,
		    u->user,
		    u->user[0] != '\0' ? "@" : "",
		    u->host,
		    u->doc,
		    opts);

		repo->fh = fetchXGet(u, &st, opts);
		u->ims_time = st.mtime;
		if (repo->fh == NULL) {
			if (fetchLastErrCode == FETCH_OK) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			}
			--retry;
			if (retry <= 0 || fetchLastErrCode == FETCH_UNAVAIL) {
				 if (!repo->silent)
					pkg_emit_error("%s://%s%s%s%s: %s",
					    u->scheme,
					    u->user,
					    u->user[0] != '\0' ? "@" : "",
					    u->host,
					    u->doc,
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
			}
		}
	}
cleanup:
	u->doc = doc;
	if (retcode != EPKG_OK && repo->fh != NULL) {
		fclose(repo->fh);
		repo->fh = NULL;
	}
	return (retcode);
}

int
fetch_open(struct pkg_repo *repo, struct url *u, off_t *sz)
{
	int retcode = EPKG_FATAL;

	pkg_debug(1, "opening libfetch fetcher");
	if (repo->fh == NULL)
		retcode = fetch_connect(repo, u);

	if (retcode == EPKG_OK)
		*sz = u->length;

	return (retcode);
}
