/*-
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
#include <sys/stat.h>

#include <ctype.h>
#include <fcntl.h>
#define _WITH_GETLINE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fetch.h>
#include <pthread.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

struct http_mirror {
	struct url *url;
	STAILQ_ENTRY(http_mirror) next;
};

static struct dns_srvinfo *srv_mirrors = NULL;
static STAILQ_HEAD(,http_mirror) http_mirrors = STAILQ_HEAD_INITIALIZER(http_mirrors);

static void
gethttpmirrors(const char *url) {
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
				STAILQ_INSERT_TAIL(&http_mirrors, m, next);
			}
		}
	}
	fclose(f);
	return;
}

int
pkg_fetch_file(const char *url, const char *dest, time_t t)
{
	int fd = -1;
	int retcode = EPKG_FATAL;

	if ((fd = open(dest, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0600)) == -1) {
		pkg_emit_errno("open", dest);
		return(EPKG_FATAL);
	}

	retcode = pkg_fetch_file_to_fd(url, fd, t);

	close(fd);

	/* Remove local file if fetch failed */
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

int
pkg_fetch_file_to_fd(const char *url, int dest, time_t t)
{
	FILE *remote = NULL;
	struct url *u;
	struct url_stat st;
	off_t done = 0;
	off_t r;

	int64_t max_retry, retry;
	time_t begin_dl;
	time_t now;
	time_t last = 0;
	char buf[10240];
	char *doc;
	char docpath[MAXPATHLEN];
	int retcode = EPKG_OK;
	bool srv = false;
	bool http = false;
	char zone[MAXHOSTNAMELEN + 13];
	struct dns_srvinfo *srv_current = NULL;
	struct http_mirror *http_current = NULL;
	const char *mt;

	fetchTimeout = 30;

	if (pkg_config_int64(PKG_CONFIG_FETCH_RETRY, &max_retry) == EPKG_FATAL)
		max_retry = 3;

	retry = max_retry;

	u = fetchParseURL(url);
	if (t != 0)
		u->ims_time = t;

	doc = u->doc;
	while (remote == NULL) {
		if (retry == max_retry) {
			pkg_config_string(PKG_CONFIG_MIRRORS, &mt);
			if (mt != NULL && strncasecmp(mt, "srv", 3) == 0 && \
			    strcmp(u->scheme, "file") != 0) {
				srv = true;
				snprintf(zone, sizeof(zone),
				    "_%s._tcp.%s", u->scheme, u->host);
				pthread_mutex_lock(&mirror_mtx);
				if (srv_mirrors == NULL)
					srv_mirrors = dns_getsrvinfo(zone);
				pthread_mutex_unlock(&mirror_mtx);
				srv_current = srv_mirrors;
			} else if (mt != NULL && strncasecmp(mt, "http", 4) == 0 && \
			           strcmp(u->scheme, "file") != 0 && \
			           strcmp(u->scheme, "ftp") != 0) {
				http = true;
				snprintf(zone, sizeof(zone),
				    "%s://%s", u->scheme, u->host);
				pthread_mutex_lock(&mirror_mtx);
				if (STAILQ_EMPTY(&http_mirrors))
					gethttpmirrors(zone);
				pthread_mutex_unlock(&mirror_mtx);
				http_current = STAILQ_FIRST(&http_mirrors);
			}
		}

		if (srv && srv_mirrors != NULL)
			strlcpy(u->host, srv_current->host, sizeof(u->host));
		else if (http && !STAILQ_EMPTY(&http_mirrors)) {
			strlcpy(u->scheme, http_current->url->scheme, sizeof(u->scheme));
			strlcpy(u->host, http_current->url->host, sizeof(u->host));
			snprintf(docpath, MAXPATHLEN, "%s%s", http_current->url->doc, doc);
			u->doc = docpath;
			u->port = http_current->url->port;
		}

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
			if (srv && srv_mirrors != NULL) {
				srv_current = srv_current->next;
				if (srv_current == NULL)
					srv_current = srv_mirrors;
			} else if (http && !STAILQ_EMPTY(&http_mirrors)) {
				http_current = STAILQ_NEXT(http_current, next);
				if (http_current == NULL)
					http_current = STAILQ_FIRST(&http_mirrors);
			} else {
				sleep(1);
			}
		}
	}
	if (t != 0) {
		if (st.mtime <= t) {
			retcode = EPKG_UPTODATE;
			goto cleanup;
		}
	}

	begin_dl = time(NULL);
	while (done < st.size) {
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
		if (now > last || done == st.size) {
			pkg_emit_fetching(url, st.size, done, (now - begin_dl));
			last = now;
		}
	}

	if (ferror(remote)) {
		pkg_emit_error("%s: %s", url, fetchLastErrString);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

	cleanup:

	if (remote != NULL)
		fclose(remote);

	/* restore original doc */
	u->doc = doc;

	fetchFreeURL(u);

	return (retcode);
}
