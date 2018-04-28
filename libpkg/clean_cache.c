/*-
 * Copyright (c) 2015 Baptiste Daroussin <bapt@FreeBSD.org>
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

#include <sys/stat.h>

#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

static void
rm_rf(int basefd, const char *path)
{
	int dirfd;
	DIR *d;
	struct dirent *e;
	struct stat st;

	if (basefd != -1) {
		while (*path == '/')
			path++;

		dirfd = openat(basefd, path, O_DIRECTORY|O_CLOEXEC);
		if (dirfd == -1) {
			pkg_emit_errno("openat", path);
			return;
		}
	} else {
		dirfd = dup(pkg_get_cachedirfd());
		if (dirfd == -1) {
			pkg_emit_error("Cannot open the cache directory");
			return;
		}
	}

	d = fdopendir(dirfd);
	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		if (fstatat(dirfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			pkg_emit_errno("fstatat", path);
			continue;
		}
		if (S_ISDIR(st.st_mode))
			rm_rf(dirfd, e->d_name);
		else
			unlinkat(dirfd, e->d_name, 0);
	}
	closedir(d);
	if (basefd == -1)
		return;
	if (fstatat(basefd, path, &st, AT_SYMLINK_NOFOLLOW) != 0)
		return;
	unlinkat(basefd, path, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0);
}

void
pkg_cache_full_clean(void)
{

	if (!pkg_object_bool(pkg_config_get("AUTOCLEAN")))
		return;

	pkg_debug(1, "Cleaning up cachedir");

	return (rm_rf(-1, NULL));
}
