/*-
 * Copyright (c) 2024-2025 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <private/pkg.h>
#include <private/utils.h>

struct pkg_add_context {
	int rootfd;
	struct pkg *pkg;
	struct pkg *localpkg;
	struct pkg_id_cache ids;
};
struct tempdir *open_tempdir(struct pkg_add_context *, const char *path);
