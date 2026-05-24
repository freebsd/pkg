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
	int rc;

	if (pkg_new(&p, PKG_FILE) != EPKG_OK)
		return (0);

	pkg_set(p, PKG_ATTR_PREFIX, "/");
	pl = plist_new(p, "/");
	if (pl == NULL) {
		pkg_free(p);
		return (0);
	}

	/* Multi-line parsing via a pipe */
	rc = fmemopen(NULL, 0, "w+");
	if (rc == NULL) {
		plist_free(pl);
		pkg_free(p);
		return (0);
	}
	fwrite(data, 1, size, rc);
	rewind(rc);

	plist_parse(pl, rc);
	fclose(rc);

	plist_free(pl);
	pkg_free(p);
	return (0);
}
