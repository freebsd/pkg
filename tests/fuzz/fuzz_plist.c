/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for plist parsing.
 * Feed fuzzed plist content (multi-line) to plist_parse()
 * to cover @for/@end and other multi-line constructs.
 * Also exercises single-line parsing via plist_parse_line().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct pkg *p = NULL;
	struct plist *pl;
	FILE *f;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	if (pkg_new(&p, PKG_FILE) != EPKG_OK)
		return (0);

	pkg_set(p, PKG_ATTR_PREFIX, "/");
	pl = plist_new(p, "/");
	if (pl == NULL) {
		pkg_free(p);
		return (0);
	}

	/* Multi-line parsing via a pipe */
	f = fmemopen(NULL, 0, "w+");
	if (f == NULL) {
		plist_free(pl);
		pkg_free(p);
		return (0);
	}
	fwrite(data, 1, size, f);
	rewind(f);

	/* Multi-line parsing via the exported per-line API */
	while ((linelen = getline(&line, &linecap, f)) > 0) {
		if (line[linelen - 1] == '\n')
			line[linelen - 1] = '\0';
		plist_parse_line(pl, line);
	}
	free(line);
	fclose(f);

	plist_free(pl);
	pkg_free(p);
	return (0);
}
