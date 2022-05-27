/*-
 * Copyright (c) 2012-2020 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include "private/fetch.h"

static struct fetcher {
	const char *scheme;
	int (*open)(struct pkg_repo *, struct url *, off_t *);
} fetchers [] = {
	{
		"tcp",
		tcp_open,
	},
	{
		"ssh",
		ssh_open,
	},
	{
		"pkg+https",
		fetch_open,
	},
	{
		"pkg+http",
		fetch_open,
	},
	{
		"https",
		fetch_open,
	},
	{
		"http",
		fetch_open,
	},
	{
		"file",
		file_open,
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

int
pkg_fetch_file_to_fd(struct pkg_repo *repo, const char *url, int dest,
    time_t *t, ssize_t offset, int64_t size, bool silent)
{
	struct url	*u = NULL;
	struct pkg_kv	*kv;
	kvlist_t	 envtorestore = tll_init();
	stringlist_t	 envtounset = tll_init();
	char		*tmp;
	off_t		 done = 0;
	off_t		 r;
	char		 buf[8192];
	int		 retcode = EPKG_OK;
	off_t		 sz = 0;
	size_t		 buflen = 0;
	size_t		 left = 0;
	struct fetcher	*fetcher = NULL;
	struct pkg_repo	*fakerepo = NULL;

	FILE *remote = NULL;

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
		u = fetchParseURL(url);
	}

	if (u == NULL)
		u = fetchParseURL(url);

	if (offset > 0)
		u->offset = offset;

	if (repo != NULL) {
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
	} else {
		fakerepo = xcalloc(1, sizeof(struct pkg_repo));
		fakerepo->url = xstrdup(url);
		repo = fakerepo;
	}

	if (u == NULL) {
		pkg_emit_error("%s: parse error", url);
		/* Too early for there to be anything to cleanup */
		return(EPKG_FATAL);
	}

	if (t != NULL)
		u->ims_time = *t;

	for (int i = 0; i < nitems(fetchers); i++) {
		if (strcmp(u->scheme, fetchers[i].scheme) == 0) {
			fetcher = &fetchers[i];
			if ((retcode = fetcher->open(repo, u, &sz)) != EPKG_OK)
				goto cleanup;
			remote = repo->ssh ? repo->ssh : repo->fh;
			break;
		}
	}
	if (fetcher == NULL) {
		pkg_emit_error("Unknown scheme: %s", u->scheme);
		return (EPKG_FATAL);
	}
	pkg_debug(1, "Fetch: fetcher chosen: %s", fetcher->scheme);

	if (strcmp(u->scheme, "ssh") != 0 && strcmp(u->scheme, "tcp") != 0 ) {
		if (t != NULL && u->ims_time != 0) {
			if (u->ims_time <= *t) {
				retcode = EPKG_UPTODATE;
				goto cleanup;
			} else
				*t = u->ims_time;
		}
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

	if (strcmp(u->scheme, "ssh") != 0 & strcmp(u->scheme, "tcp") != 0
	    && ferror(remote)) {
		pkg_emit_error("%s: %s", url, fetchLastErrString);
		retcode = EPKG_FATAL;
		goto cleanup;
	}

cleanup:
	if (repo != NULL) {
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
	}

	if (u != NULL) {
		if (remote != NULL &&  repo != NULL && remote != repo->ssh) {
			fclose(remote);
			repo->fh = NULL;
		}
	}
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

	/* restore original doc */
	fetchFreeURL(u);

	return (retcode);
}
