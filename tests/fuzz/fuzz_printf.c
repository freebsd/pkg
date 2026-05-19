/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for pkg_printf format strings.
 * Feed fuzzed format strings to pkg_fprintf_pkg() with a minimal package.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *fmt;
	struct pkg *p = NULL;
	FILE *devnull;

	fmt = strndup((const char *)data, size);
	if (fmt == NULL)
		return (0);

	if (pkg_new(&p, PKG_FILE) != EPKG_OK) {
		free(fmt);
		return (0);
	}

	pkg_set(p, PKG_ATTR_NAME, "fuzz");
	pkg_set(p, PKG_ATTR_VERSION, "1.0");
	pkg_set(p, PKG_ATTR_ORIGIN, "fuzz/fuzz");
	pkg_set(p, PKG_ATTR_MAINTAINER, "fuzz@localhost");
	pkg_set(p, PKG_ATTR_COMMENT, "fuzz test package");
	pkg_set(p, PKG_ATTR_ARCH, "freebsd:16:amd64");

	devnull = fopen("/dev/null", "w");
	if (devnull != NULL) {
		pkg_fprintf_pkg(devnull, fmt, p);
		fclose(devnull);
	}

	pkg_free(p);
	free(fmt);
	return (0);
}
