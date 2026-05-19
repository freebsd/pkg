/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for chmod-style permission string parsing.
 * Feed fuzzed strings to parse_mode().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/utils.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *str;
	void *set;

	if (size == 0 || size > 1024)
		return (0);

	str = malloc(size + 1);
	if (str == NULL)
		return (0);
	memcpy(str, data, size);
	str[size] = '\0';

	set = parse_mode(str);
	free(set);

	free(str);
	return (0);
}
