/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for pkg_checksum_is_valid().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *str = malloc(size + 1);
	if (str == NULL)
		return (0);
	memcpy(str, data, size);
	str[size] = '\0';

	pkg_checksum_is_valid(str, size);

	free(str);
	return (0);
}
