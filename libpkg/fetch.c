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

static struct fetcher fetchers [] = {
	{
		"tcp",
		0,
		tcp_open,
		NULL,
		fh_close,
		stdio_fetch,
	},
	{
		"ssh",
		0,
		ssh_open,
		NULL,
		fh_close,
		stdio_fetch,
	},
	{
		"pkg+https",
		0,
		curl_open,
		NULL,
		curl_cleanup,
		curl_fetch,
	},
	{
		"pkg+http",
		0,
		curl_open,
		NULL,
		curl_cleanup,
		curl_fetch,
	},
	{
		"https",
		0,
		curl_open,
		NULL,
		curl_cleanup,
		curl_fetch,
	},
	{
		"http",
		0,
		curl_open,
		NULL,
		curl_cleanup,
		curl_fetch,
	},
	{
		"file",
		0,
		file_open,
		fh_close,
		NULL,
		stdio_fetch,
	},
};

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

static struct fetcher *
select_fetcher(const char *url)
{
	struct fetcher *f;

	for (size_t i = 0; i < nitems(fetchers); i++) {
		if ((strncasecmp(url, fetchers[i].scheme,
		    strlen(fetchers[i].scheme)) == 0) &&
		    url[strlen(fetchers[i].scheme)] == ':') {
			f = &fetchers[i];
			f->timeout =
			    pkg_object_int(pkg_config_get("FETCH_TIMEOUT"));
			return (f);
		}
	}
	return (NULL);

}
int
pkg_fetch_file_to_fd(struct pkg_repo *repo, const char *url, int dest,
    time_t *t, ssize_t offset, int64_t size, bool silent)
{
	struct pkg_kv	*kv;
	kvlist_t	 envtorestore = tll_init();
	stringlist_t	 envtounset = tll_init();
	char		*tmp;
	int		 retcode = EPKG_OK;
	off_t		 sz = 0;
	struct pkg_repo	*fakerepo = NULL;

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

	pkg_debug(1, "Request to fetch %s", url);
	if (repo == NULL) {
		fakerepo = xcalloc(1, sizeof(struct pkg_repo));
		fakerepo->url = xstrdup(url);
		repo = fakerepo;
	}

	if (repo->fetcher == NULL)
		repo->fetcher = select_fetcher(url);
	if (repo->fetcher == NULL) {
		pkg_emit_error("Unknown scheme: %s", url);
		return (EPKG_FATAL);
	}

	if (strncasecmp(URL_SCHEME_PREFIX, url,
	    strlen(URL_SCHEME_PREFIX)) == 0) {
		if (repo->fetcher == NULL && repo->mirror_type != SRV) {
			pkg_emit_error("packagesite URL error for %s -- "
					URL_SCHEME_PREFIX
					":// implies SRV mirror type", url);

			/* Too early for there to be anything to cleanup */
			return(EPKG_FATAL);
		}
		url += strlen(URL_SCHEME_PREFIX);
	}

	repo->silent = silent;
	tll_foreach(repo->env, k) {
		if ((tmp = getenv(k->item->key)) != NULL) {
			kv = xcalloc(1, sizeof(*kv));
			kv->key = xstrdup(k->item->key);
			kv->value = xstrdup(tmp);
			tll_push_back(envtorestore, kv);
		} else {
			tll_push_back(envtounset, k->item->key);
		}
		setenv(k->item->key, k->item->value, 1);
	}

	if ((retcode = repo->fetcher->open(repo, url, &sz, t)) != EPKG_OK)
		goto cleanup;
	pkg_debug(1, "Fetch: fetcher used: %s", repo->fetcher->scheme);

	if (sz <= 0 && size > 0)
		sz = size;

	retcode = repo->fetcher->fetch(repo, dest, url, sz, offset, t);
	if (retcode == EPKG_OK)
		pkg_emit_fetch_finished(url);

cleanup:
	tll_foreach(envtorestore, k) {
		setenv(k->item->key, k->item->value, 1);
		tll_remove_and_free(envtorestore, k, pkg_kv_free);
	}
	tll_free(envtorestore);
	tll_foreach(envtounset, k) {
		unsetenv(k->item);
		tll_remove(envtounset, k);
	}
	tll_free(envtounset);

	if (repo->fetcher != NULL && repo->fetcher->close != NULL)
		repo->fetcher->close(repo);
	free(fakerepo);

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

	return (retcode);
}
