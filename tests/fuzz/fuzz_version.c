/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for version string comparison.
 * Feed fuzzed version pairs to pkg_version_cmp().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *v1, *v2;
	size_t mid;

	if (size < 2)
		return (0);

	mid = size / 2;
	v1 = strndup((const char *)data, mid);
	v2 = strndup((const char *)(data + mid), size - mid);
	if (v1 == NULL || v2 == NULL) {
		free(v1);
		free(v2);
		return (0);
	}

	(void)pkg_version_cmp(v1, v2);

	free(v1);
	free(v2);
	return (0);
}
