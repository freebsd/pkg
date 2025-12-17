/*-
 * Copyright (c) 2012-2023 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <paths.h>
#include <poll.h>

#include <bsd_compat.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"
#include "private/fetch.h"

static struct fetcher fetchers[] = {
	{
		.scheme = "tcp",
		.open = tcp_open,
		.close = NULL,
		.cleanup = fh_close,
		.fetch = stdio_fetch,
	},
	{
		.scheme = "ssh",
		.open = ssh_open,
		.close = NULL,
		.cleanup = fh_close,
		.fetch = stdio_fetch,
	},
	{
		.scheme = "pkg+https",
		.open = curl_open,
		.close = NULL,
		.cleanup = curl_cleanup,
		.fetch = curl_fetch,
	},
	{
		.scheme = "pkg+http",
		.open = curl_open,
		.close = NULL,
		.cleanup = curl_cleanup,
		.fetch = curl_fetch,
	},
	{
		.scheme = "https",
		.open = curl_open,
		.close = NULL,
		.cleanup = curl_cleanup,
		.fetch = curl_fetch,
	},
	{
		.scheme = "http",
		.open = curl_open,
		.close = NULL,
		.cleanup = curl_cleanup,
		.fetch = curl_fetch,
	},
	{
		.scheme = "file",
		.open = file_open,
		.close = fh_close,
		.cleanup = NULL,
		.fetch = stdio_fetch,
	},
};

int
pkg_fetch_file_tmp(struct pkg_repo *repo, const char *url, char *dest,
	time_t t)
{
	int fd = -1;
	int retcode = EPKG_FATAL;
	struct fetch_item fi;

	memset(&fi, 0, sizeof(struct fetch_item));

	fd = mkstemp(dest);

	if (fd == -1) {
		pkg_emit_errno("mkstemp", dest);
		return(EPKG_FATAL);
	}

	fi.url = url;
	fi.mtime = t;
	retcode = pkg_fetch_file_to_fd(repo, fd, &fi, false);

	if (fi.mtime != 0) {
		struct timespec ts[2] = {
			{
			.tv_sec = fi.mtime,
			.tv_nsec = 0
			},
			{
			.tv_sec = fi.mtime,
			.tv_nsec = 0
			}
		};
		futimens(fd, ts);
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
	struct fetch_item fi;
	char *url_to_free = NULL;

	fd = open(dest, O_CREAT|O_APPEND|O_WRONLY, 00644);
	if (fd == -1) {
		pkg_emit_errno("open", dest);
		return(EPKG_FATAL);
	}

	if (repo != NULL) {
		xasprintf(&url_to_free, "%s/%s", repo->url, url);
		fi.url = url_to_free;
	} else {
		fi.url = url;
	}

	fi.offset = offset;
	fi.size = size;
	fi.mtime = t;

	retcode = pkg_fetch_file_to_fd(repo, fd, &fi, false);
	free(url_to_free);

	if (t != 0) {
		struct timespec ts[2] = {
			{
			.tv_sec = t,
			.tv_nsec = 0
			},
			{
			.tv_sec = t,
			.tv_nsec = 0
			}
		};
		futimens(fd, ts);
	}
	close(fd);

	/* Remove local file if fetch failed */
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}

#define URL_SCHEME_PREFIX	"pkg+"

static const struct fetcher *
select_fetcher(const char *url)
{
	struct fetcher *f;
	size_t nsz;

	for (size_t i = 0; i < NELEM(fetchers); i++) {
		nsz = strlen(fetchers[i].scheme);

		if ((strncasecmp(url, fetchers[i].scheme, nsz) == 0) &&
		    url[nsz] == ':') {
			f = &fetchers[i];
			f->timeout =
				pkg_object_int(pkg_config_get("FETCH_TIMEOUT"));
			return (f);
		}
	}
	return (NULL);

}
int
pkg_fetch_file_to_fd(struct pkg_repo *repo, int dest, struct fetch_item *fi,
    bool silent)
{
	struct pkg_kv	*kv;
	kvlist_t	 envtorestore = vec_init();
	c_charv_t		 envtounset = vec_init();
	char		*tmp;
	int		 retcode = EPKG_OK;
	struct pkg_repo	*fakerepo = NULL;
	size_t           nsz;

	/* A URL of the form http://host.example.com/ where
	 * host.example.com does not resolve as a simple A record is
	 * not valid according to RFC 2616 Section 3.2.2.  Our usage
	 * with SRV records is incorrect.  However it is encoded into
	 * /usr/sbin/pkg in various releases so we can't just drop it.
         *
         * Instead, introduce new pkg+http://, pkg+https://,
	 * pkg+ssh://, pkg+file:// to support the
	 * SRV-style server discovery, and also to allow eg. Firefox
	 * to run pkg-related stuff given a pkg+foo:// URL.
	 *
	 * Error if using plain http://, https:// etc with SRV
	 */

	pkg_dbg(PKG_DBG_FETCH, 1, "Request to fetch %s", fi->url);
	if (repo == NULL) {
		fakerepo = xcalloc(1, sizeof(struct pkg_repo));
		fakerepo->url = xstrdup(fi->url);
		fakerepo->mirror_type = NOMIRROR;
		repo = fakerepo;
	}

	if (repo->fetcher == NULL)
		repo->fetcher = select_fetcher(fi->url);
	if (repo->fetcher == NULL) {
		pkg_emit_error("Unknown scheme: %s", fi->url);
		return (EPKG_FATAL);
	}

	nsz = strlen(URL_SCHEME_PREFIX);
	if (strncasecmp(URL_SCHEME_PREFIX, fi->url, nsz) == 0) {
		if (repo->mirror_type != SRV) {
			pkg_emit_error("packagesite URL error for %s -- "
					URL_SCHEME_PREFIX
					":// implies SRV mirror type", fi->url);

			/* Too early for there to be anything to cleanup */
			return(EPKG_FATAL);
		}
		fi->url += nsz;
	}

	repo->silent = silent;
	vec_foreach(repo->env, i) {
		if ((tmp = getenv(repo->env.d[i]->key)) != NULL) {
			kv = xcalloc(1, sizeof(*kv));
			kv->key = xstrdup(repo->env.d[i]->key);
			kv->value = xstrdup(tmp);
			vec_push(&envtorestore, kv);
		} else {
			vec_push(&envtounset, repo->env.d[i]->key);
		}
		setenv(repo->env.d[i]->key, repo->env.d[i]->value, 1);
	}

	if ((retcode = repo->fetcher->open(repo, fi)) != EPKG_OK)
		goto cleanup;
	pkg_dbg(PKG_DBG_FETCH, 1, "Fetch: fetcher used: %s", repo->fetcher->scheme);

	retcode = repo->fetcher->fetch(repo, dest, fi);
	if (retcode == EPKG_OK)
		pkg_emit_fetch_finished(fi->url);

cleanup:
	vec_foreach(envtorestore, i) {
		setenv(envtorestore.d[i]->key, envtorestore.d[i]->value, 1);
		vec_remove_and_free(&envtorestore, i, pkg_kv_free);
	}
	vec_free(&envtorestore);
	while (vec_len(&envtounset) > 0)
		unsetenv(vec_pop(&envtounset));
	vec_free(&envtounset);

	if (repo->fetcher != NULL && repo->fetcher->close != NULL)
		repo->fetcher->close(repo);
	free(fakerepo);

	if (retcode == EPKG_OK) {
		struct timespec ts[2] = {
			{
			.tv_sec = fi->mtime,
			.tv_nsec = 0
			},
			{
			.tv_sec = fi->mtime,
			.tv_nsec = 0
			}
		};
		futimens(dest, ts);
	}

	return (retcode);
}
