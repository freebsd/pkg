/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for pkg_message_from_str().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct pkg *pkg = NULL;

	if (pkg_new(&pkg, PKG_INSTALLED) != EPKG_OK)
		return (0);

	pkg_message_from_str(pkg, (const char *)data, size);

	pkg_free(pkg);
	return (0);
}
