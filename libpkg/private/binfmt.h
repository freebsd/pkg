/*-
 * Copyright (c) 2024 Keve Müller <kevemueller@users.github.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "private/pkg.h"

int pkg_get_myarch_elfparse(int fd, struct os_info *oi);
int pkg_analyse_init_elf(const char* stage);
int pkg_analyse_elf(const bool developer_mode, struct pkg *pkg, const char *fpath);
int pkg_analyse_close_elf();

int pkg_get_myarch_macho(int fd, struct os_info *oi);
int pkg_analyse_init_macho(const char* stage);
int pkg_analyse_macho(const bool developer_mode, struct pkg *pkg, const char *fpath);
int pkg_analyse_close_macho();

