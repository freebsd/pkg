/*-
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef PKG_CHECKSUM_H
#define PKG_CHECKSUM_H

typedef enum {
	PKG_HASH_TYPE_SHA256_BASE32 = 0,
	PKG_HASH_TYPE_SHA256_HEX,
	PKG_HASH_TYPE_BLAKE2_BASE32,
	PKG_HASH_TYPE_SHA256_RAW,
	PKG_HASH_TYPE_BLAKE2_RAW,
	PKG_HASH_TYPE_BLAKE2S_BASE32,
	PKG_HASH_TYPE_BLAKE2S_RAW,
	PKG_HASH_TYPE_UNKNOWN
} pkg_checksum_type_t;

unsigned char *pkg_checksum_file(const char *path, pkg_checksum_type_t type);
char *pkg_checksum_generate_file(const char *path, pkg_checksum_type_t type);
int pkg_checksum_validate_file(const char *path, const char *sum);
pkg_checksum_type_t pkg_checksum_type_from_string(const char *name);

#endif
