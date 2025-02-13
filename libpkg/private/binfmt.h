/*-
 * Copyright (c) 2024 Keve Müller <kevemueller@users.github.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "private/pkg.h"

/* In the future this will be extended to include
   e.g. PKG_PROVIDE_SHLIB_COMPAT_32 */
enum pkg_provide_flags {
	PKG_PROVIDE_NONE = 0,
	PKG_PROVIDE_SHLIB_NATIVE = 1 << 0,
};

int pkg_elf_abi_from_fd(int fd, struct pkg_abi *abi);
int pkg_analyse_init_elf(const char* stage);
int pkg_analyse_elf(const bool developer_mode, struct pkg *pkg,
    const char *fpath, char **provided, enum pkg_shlib_flags *provided_flags);
int pkg_analyse_close_elf();

int pkg_macho_abi_from_fd(int fd, struct pkg_abi *abi, enum pkg_arch arch_hint);
int pkg_analyse_init_macho(const char* stage);
int pkg_analyse_macho(const bool developer_mode, struct pkg *pkg,
    const char *fpath, char **provided, enum pkg_shlib_flags *provided_flags);
int pkg_analyse_close_macho();
