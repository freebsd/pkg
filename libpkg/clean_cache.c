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
#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

static void
rm_rf(const char *path)
{
	DIR *d;
	struct dirent *e;
	struct stat st;
	char filepath[MAXPATHLEN];

	if ((d = opendir(path)) == NULL) {
		pkg_emit_errno("opendir", path);
		return;
	}

	while ((e = readdir(d)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		snprintf(filepath, sizeof(filepath), "%s/%s", path, e->d_name);
		if (lstat(filepath, &st) != 0) {
			pkg_emit_errno("lstat", filepath);
			continue;
		}
		if (S_ISDIR(st.st_mode))
			rm_rf(filepath);
		remove(filepath);
	}
	closedir(d);
}

void
pkg_cache_full_clean(void)
{
	const char *cachedir;

	if (!pkg_object_bool(pkg_config_get("AUTOCLEAN")))
		return;

	pkg_debug(1, "Cleaning up cachedir");

	cachedir = pkg_object_string(pkg_config_get("PKG_CACHEDIR"));

	return (rm_rf(cachedir));
}
