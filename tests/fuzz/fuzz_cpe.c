/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for CPE string parsing.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg_cpe.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *str;
	struct pkg_audit_cpe *cpe;

	if (size == 0 || size > 4096)
		return (0);

	str = malloc(size + 1);
	if (str == NULL)
		return (0);
	memcpy(str, data, size);
	str[size] = '\0';

	cpe = pkg_cpe_parse(str);
	pkg_cpe_free(cpe);

	free(str);
	return (0);
}
