/*-
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef PKG_CHECKSUM_H
#define PKG_CHECKSUM_H

/*
 * The numeric values are stored in the checksum strings, so existing
 * entries must keep their value and new ones can only be appended
 * before PKG_HASH_TYPE_UNKNOWN.
 *
 * The *_BASE32 entries encode the bit stream in a pkg specific
 * (non-standard) order and are kept as is for compatibility.  The
 * *_ZBASE32 entries implement the real z-base-32 encoding described in
 * http://philzimmermann.com/docs/human-oriented-base-32-encoding.txt
 */
typedef enum {
	PKG_HASH_TYPE_SHA256_BASE32 = 0,
	PKG_HASH_TYPE_SHA256_HEX,
	PKG_HASH_TYPE_BLAKE2_BASE32,
	PKG_HASH_TYPE_SHA256_RAW,
	PKG_HASH_TYPE_BLAKE2_RAW,
	PKG_HASH_TYPE_BLAKE2S_BASE32,
	PKG_HASH_TYPE_BLAKE2S_RAW,
	PKG_HASH_TYPE_SHA512_BASE32,
	PKG_HASH_TYPE_SHA512_HEX,
	PKG_HASH_TYPE_SHA512_RAW,
	PKG_HASH_TYPE_BLAKE2_HEX,
	PKG_HASH_TYPE_BLAKE2S_HEX,
	PKG_HASH_TYPE_SHA256_ZBASE32,
	PKG_HASH_TYPE_BLAKE2_ZBASE32,
	PKG_HASH_TYPE_BLAKE2S_ZBASE32,
	PKG_HASH_TYPE_UNKNOWN
} pkg_checksum_type_t;

unsigned char *pkg_checksum_file(const char *path, pkg_checksum_type_t type);
char *pkg_checksum_generate_file(const char *path, pkg_checksum_type_t type);
int pkg_checksum_validate_file(const char *path, const char *sum);
pkg_checksum_type_t pkg_checksum_type_from_string(const char *name);

#endif
