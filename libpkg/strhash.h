/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright(c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 */

#ifndef STRHASH_H
#define STRHASH_H

#include <stdint.h>
#include <string.h>

/*
 * 64-bit string hash: word-at-a-time multiply-xor with a splitmix64
 * finalizer.
 *
 * The finalizer is what guarantees avalanche of the low bits, and the
 * tables use those bits directly as an index (& (capacity - 1)); without
 * it a plain byte/word loop clusters badly on sequential keys.
 *
 */
static inline uint64_t
strhash(const void *data, size_t len)
{
	const unsigned char *p = (const unsigned char *)data;
	uint64_t h = 0x9e3779b97f4a7c15ULL ^ (uint64_t)len;
	uint64_t k;

	while (len >= sizeof(k)) {
		memcpy(&k, p, sizeof(k));
		h = (h ^ k) * 0x9e3779b97f4a7c15ULL;
		p += sizeof(k);
		len -= sizeof(k);
	}
	k = 0;
	while (len-- > 0)
		k = (k << 8) | *p++;
	h = (h ^ k) * 0x9e3779b97f4a7c15ULL;

	h ^= h >> 30;
	h *= 0xbf58476d1ce4e5b9ULL;
	h ^= h >> 27;
	h *= 0x94d049bb133111ebULL;
	h ^= h >> 31;
	return (h);
}

#endif /* STRHASH_H */
