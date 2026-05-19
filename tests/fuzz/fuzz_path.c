/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for path normalization.
 * Feed adversarial file paths to pkg_absolutepath() and pkg_addfile_attr().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *str;
	char dest[MAXPATHLEN];
	struct pkg *p = NULL;

	if (size == 0 || size > 4096)
		return (0);

	str = malloc(size + 1);
	if (str == NULL)
		return (0);
	memcpy(str, data, size);
	str[size] = '\0';

	/* Fuzz the standalone path normalization */
	pkg_absolutepath(str, dest, sizeof(dest), false);
	pkg_absolutepath(str, dest, sizeof(dest), true);

	/* Fuzz path handling inside pkg_addfile_attr */
	if (pkg_new(&p, PKG_FILE) != EPKG_OK) {
		free(str);
		return (0);
	}

	pkg_addfile_attr(p, str, NULL, NULL, NULL, 0, 0, 0, NULL, false);

	pkg_free(p);
	free(str);
	return (0);
}
