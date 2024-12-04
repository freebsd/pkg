/*-
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was developed by Isaac Freund <ifreund@freebsdfoundation.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * This enum is not intended to contain all operating systems in the universe.
 * It is intended to contain only the operating systems for which pkg supports
 * ABI detection.
 * Adding a new OS to this enum should also add test cases for detection of the
 * OS through parsing ELF/Mach-O/etc.
 */
enum pkg_os {
	PKG_OS_UNKNOWN = 0,
	PKG_OS_FREEBSD,
	PKG_OS_NETBSD,
	PKG_OS_DRAGONFLY,
	PKG_OS_LINUX,
	PKG_OS_DARWIN,
};

/*
 * Return the canonical string for the given operating system.
 */
const char *pkg_os_to_string(enum pkg_os os);

/*
 * This enum is not intended to contain all architectures in the universe.
 * It is intended to contain only the architectures for which pkg supports
 * ABI detection.
 * Adding a new architecture to this enum should also test cases for detection
 * of the architecture through parsing ELF/Mach-O/etc.
 */
enum pkg_arch {
	PKG_ARCH_UNKNOWN = 0,
	PKG_ARCH_I386,
	PKG_ARCH_AMD64,
	PKG_ARCH_ARMV6,
	PKG_ARCH_ARMV7,
	PKG_ARCH_AARCH64,
	PKG_ARCH_POWERPC,
	PKG_ARCH_POWERPC64,
	PKG_ARCH_POWERPC64LE,
	PKG_ARCH_RISCV32,
	PKG_ARCH_RISCV64,
};

/*
 * Return the canonical string for the given architecture.
 *
 * The string used for the arch depends on the OS in some cases.
 * For example, "amd64" is used for FreeBSD while "x86_64" is used for Linux
 * operating systems though both refer to the same physical architecture.
 */
const char *pkg_arch_to_string(enum pkg_os os, enum pkg_arch arch);

struct pkg_abi {
	enum pkg_os os;

	int major;
	int minor;
	int patch;

	enum pkg_arch arch;
};

/*
 * Attempts to determine the ABI by parsing /usr/bin/uname or /bin/sh.
 * If ABI_FILE is set in the environment, that file path is parsed instead.
 */
int pkg_abi_from_file(struct pkg_abi *abi);

/*
 * Serializes the ABI to a string with format OS:VERSION:ARCH.
 *
 * The caller is responsible for freeing the returned string.
 */
char *pkg_abi_to_string(const struct pkg_abi *abi);

/*
 * Validate and parse a string into a pkg_abi struct.
 * Returns false if the string is not a complete and valid ABI string
 * in the format OS:VERSION:ARCH
 */
bool pkg_abi_from_string(struct pkg_abi *abi, const char *string);

/*
 * Return true if the canonical ABI string format for the given OS uses only
 * the major version rather than both the the major and minor version.
 */
bool pkg_abi_string_only_major_version(enum pkg_os os);

/*
 * Set the version fields of the provided pkg_abi struct from the
 * FreeBSD-specific osversion value.
 *
 * Asserts that abi->os == PKG_OS_FREEBSD.
 *
 * See __FreeBSD_version in /usr/include/sys/param.h for a description
 * of the format.
 */
void pkg_abi_set_freebsd_osversion(struct pkg_abi *abi, int osversion);

/*
 * Returns the FreeBSD-specific osversion value derived from the
 * major/minor/patch fields of the provided pkg_abi struct.
 *
 * Asserts that abi->os == PKG_OS_FREEBSD.
 *
 * See __FreeBSD_version in /usr/include/sys/param.h for a description
 * of the format.
 */
int pkg_abi_get_freebsd_osversion(struct pkg_abi *abi);
