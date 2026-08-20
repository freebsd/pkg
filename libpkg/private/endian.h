/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#ifndef _PKG_ENDIAN_H_
#define _PKG_ENDIAN_H_

#include <stdint.h>

static inline uint32_t
pkg_le32dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return ((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static inline uint32_t
pkg_be32dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

static inline uint64_t
pkg_le64dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return ((uint64_t)pkg_le32dec(p) |
	    ((uint64_t)pkg_le32dec(p + 4) << 32));
}

static inline uint64_t
pkg_be64dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return (((uint64_t)pkg_be32dec(p) << 32) |
	    (uint64_t)pkg_be32dec(p + 4));
}

#endif	/* _PKG_ENDIAN_H_ */
