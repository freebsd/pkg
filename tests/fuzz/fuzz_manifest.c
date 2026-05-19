/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for manifest parsing.
 * Feed malformed/fuzzed data to pkg_parse_manifest().
 */

#include <stdint.h>
#include <stdlib.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct pkg *p = NULL;

	if (pkg_new(&p, PKG_REMOTE) != EPKG_OK)
		return (0);

	/* pkg_parse_manifest() takes a buffer, feed it directly */
	pkg_parse_manifest(p, (const char *)data, size);

	pkg_free(p);

	return (0);
}
