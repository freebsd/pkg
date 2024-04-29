/*-
 * Copyright (c) 2020-2023 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/stat.h>
#include <sys/param.h>

#include <stdio.h>
#include <errno.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "private/utils.h"
#include "private/fetch.h"

int
file_open(struct pkg_repo *repo, struct fetch_item *fi)
{
	struct stat st;
	const char *u = fi->url;

	if (strlen(u) > 5)
		u += 5; /* file: */
	if (*u != '/') {
		pkg_emit_error("invalid url: '%s'\n", fi->url);
		return (EPKG_FATAL);
	}
	if (stat(u, &st) == -1) {
		if (!repo->silent)
			pkg_emit_error("%s: %s", fi->url,
			    strerror(errno));
		return (EPKG_FATAL);
	}
	fi->size = st.st_size;
	if (st.st_mtime <= fi->mtime)
		return (EPKG_UPTODATE);

	repo->fh = fopen(u, "re");
	if (repo->fh == NULL)
		return (EPKG_FATAL);
	return (EPKG_OK);
}

void
fh_close(struct pkg_repo *repo)
{
	if (repo->fh != NULL)
		fclose(repo->fh);
	repo->fh = NULL;
}

int
stdio_fetch(struct pkg_repo *repo, int dest, struct fetch_item *fi)
{
	char buf[8192];
	size_t buflen = 0, left = 0;
	off_t done = 0, r;

	pkg_emit_fetch_begin(fi->url);
	pkg_emit_progress_start(NULL);
	if (fi->offset > 0)
		done += fi->offset;
	buflen = sizeof(buf);
	left = sizeof(buf);
	if (fi->size > 0)
		left = fi->size - done;

	while ((r = fread(buf, 1, left < buflen ? left : buflen, repo->fh)) > 0) {
		if (write(dest, buf, r) != r) {
			pkg_emit_errno("write", "");
			return (EPKG_FATAL);
		}
		done += r;
		if (fi->size > 0) {
			left -= r;
			pkg_dbg(PKG_DBG_FETCH, 1, "Read status: %jd over %jd", (intmax_t)done, (intmax_t)fi->size);
		} else
			pkg_dbg(PKG_DBG_FETCH, 1,  "Read status: %jd", (intmax_t)done);
		if (fi->size > 0)
			pkg_emit_progress_tick(done, fi->size);
	}
	if (ferror(repo->fh)) {
		pkg_emit_error("An error occurred while fetching package");
		return(EPKG_FATAL);
	}
	return (EPKG_OK);
}
