/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for dependency formula parsing.
 * Feed fuzzed strings to pkg_deps_parse_formula().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/pkg_deps.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *str;
	dep_formulav_t *f;

	str = strndup((const char *)data, size);
	if (str == NULL)
		return (0);

	f = pkg_deps_parse_formula(str);
	if (f != NULL)
		pkg_deps_formula_free(f);

	free(str);
	return (0);
}
