/*
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

#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fetch.h>

#include "pkg.h"
#include "private/event.h"

int
pkg_fetch_file(const char *url, const char *dest, time_t t)
{
	int fd = -1;
	FILE *remote = NULL;
	struct url_stat st;
	off_t done = 0;
	off_t r;
	int retry = 3;
	time_t begin_dl;
	time_t now;
	time_t last = 0;
	char buf[10240];
	int retcode = EPKG_OK;

	if ((fd = open(dest, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0600)) == -1) {
		pkg_emit_errno("open", dest);
		return(EPKG_FATAL);
	}

	while (remote == NULL) {
		remote = fetchXGetURL(url, &st, "");
		if (remote == NULL) {
			--retry;
			if (retry == 0) {
				pkg_emit_error("%s: %s", url, fetchLastErrString);
				retcode = EPKG_FATAL;
				goto cleanup;
			}
			sleep(1);
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

		if (write(fd, buf, r) != r) {
			pkg_emit_errno("write", dest);
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

	if (fd > 0)
		close(fd);

	if (remote != NULL)
		fclose(remote);

	/* Remove local file if fetch failed */
	if (retcode != EPKG_OK)
		unlink(dest);

	return (retcode);
}
