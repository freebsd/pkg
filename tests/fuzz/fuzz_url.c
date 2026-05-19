/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for yuarel URL parsing.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "yuarel.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct yuarel url;
	struct yuarel_param params[16];
	char *copy;

	if (size == 0 || size > 8192)
		return (0);

	copy = malloc(size + 1);
	if (copy == NULL)
		return (0);
	memcpy(copy, data, size);
	copy[size] = '\0';

	if (yuarel_parse(&url, copy) == 0) {
		/* Also fuzz query parsing if a query is present */
		if (url.query != NULL) {
			yuarel_parse_query(url.query, '&', params, 16);
		}
	}

	free(copy);
	return (0);
}
