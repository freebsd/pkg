/*-
 * Copyright (c) 2024 Keve Müller <kevemueller@users.github.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "private/pkg.h"

int pkg_elf_abi_from_fd(int fd, struct pkg_abi *abi);
int pkg_analyse_init_elf(const char* stage);
int pkg_analyse_elf(const bool developer_mode, struct pkg *pkg, const char *fpath);
int pkg_analyse_close_elf();

int pkg_macho_abi_from_fd(int fd, struct pkg_abi *abi, enum pkg_arch arch_hint);
int pkg_analyse_init_macho(const char* stage);
int pkg_analyse_macho(const bool developer_mode, struct pkg *pkg, const char *fpath);
int pkg_analyse_close_macho();
