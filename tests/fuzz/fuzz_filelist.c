/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for file list emission.
 * Build a package with fuzzed file entries, then serialize via
 * pkg_emit_filelist().
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct pkg *p = NULL;
	FILE *f = NULL;
	pkghash *dirs = NULL;
	int ndirs = 0;
	const uint8_t *cur, *end;

	if (size < 4 || size > 8192)
		return (0);

	if (pkg_new(&p, PKG_FILE) != EPKG_OK)
		return (0);

	pkg_set(p, PKG_ATTR_NAME, "fuzz-pkg");
	pkg_set(p, PKG_ATTR_VERSION, "1.0");
	pkg_set(p, PKG_ATTR_PREFIX, "/usr/local");

	/*
	 * Split the fuzzed data into file path entries separated by newlines.
	 * Each entry becomes a file in the package.
	 */
	cur = data;
	end = data + size;
	while (cur < end) {
		const uint8_t *nl = memchr(cur, '\n', end - cur);
		size_t len;
		char *path;

		if (nl == NULL)
			len = end - cur;
		else
			len = nl - cur;

		if (len > 0 && len < 1024) {
			path = malloc(len + 2);
			if (path != NULL) {
				/* Ensure path starts with / */
				if (*cur != '/') {
					path[0] = '/';
					memcpy(path + 1, cur, len);
					path[len + 1] = '\0';
				} else {
					memcpy(path, cur, len);
					path[len] = '\0';
				}
				pkg_addfile_attr(p, path, NULL, NULL, NULL,
				    0, 0, 0, NULL, false);
				free(path);
			}
		}

		if (nl == NULL)
			break;
		cur = nl + 1;
	}

	f = fopen("/dev/null", "w");
	if (f != NULL) {
		pkg_emit_filelist(p, f, &dirs, &ndirs);
		fclose(f);
	}

	pkghash_destroy(dirs);
	pkg_free(p);
	return (0);
}
