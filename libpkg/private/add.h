/*-
 * Copyright (c) 2024-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <private/pkg.h>

struct pkg_add_context {
	int rootfd;
	charv_t *symlinks_allowed;
	struct pkgdb *db;
	struct pkg *pkg;
};

struct tempdir *open_tempdir(struct pkg_add_context *, const char *path);

