/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for ABI string parsing.
 * Feed fuzzed strings to pkg_abi_from_string().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *str;
	struct pkg_abi abi;

	str = strndup((const char *)data, size);
	if (str == NULL)
		return (0);

	memset(&abi, 0, sizeof(abi));
	(void)pkg_abi_from_string(&abi, str);

	free(str);
	return (0);
}
