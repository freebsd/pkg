/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for plist line parsing.
 * Feed fuzzed lines to plist_parse_line().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *line;
	struct pkg *p = NULL;
	struct plist *pl;

	line = strndup((const char *)data, size);
	if (line == NULL)
		return (0);

	if (pkg_new(&p, PKG_FILE) != EPKG_OK) {
		free(line);
		return (0);
	}

	pkg_set(p, PKG_ATTR_PREFIX, "/");
	pl = plist_new(p, "/");
	if (pl == NULL) {
		pkg_free(p);
		free(line);
		return (0);
	}

	plist_parse_line(pl, line);

	plist_free(pl);
	pkg_free(p);
	free(line);
	return (0);
}
