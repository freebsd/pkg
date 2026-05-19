/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for shared library name parsing.
 * Feed fuzzed shared library name strings to pkg_addshlib_required().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *name;
	struct pkg *p = NULL;

	name = strndup((const char *)data, size);
	if (name == NULL)
		return (0);

	if (pkg_new(&p, PKG_FILE) != EPKG_OK) {
		free(name);
		return (0);
	}

	(void)pkg_addshlib_required(p, name, PKG_SHLIB_FLAGS_NONE);

	pkg_free(p);
	free(name);
	return (0);
}
