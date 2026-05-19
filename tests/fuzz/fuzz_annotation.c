/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Fuzz harness for annotation key-value parsing.
 * Feed fuzzed strings as annotation keys and values to pkg_kv_add().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pkg.h"
#include "private/pkg.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct pkg *p = NULL;
	char *key, *val;
	const uint8_t *sep;
	size_t keylen, vallen;

	if (size < 2 || size > 4096)
		return (0);

	/*
	 * Split fuzzed data at the first null byte into key and value.
	 * If no null byte, split at the midpoint.
	 */
	sep = memchr(data, '\0', size);
	if (sep != NULL && sep > data && sep < data + size - 1) {
		keylen = sep - data;
		vallen = size - keylen - 1;
		key = malloc(keylen + 1);
		val = malloc(vallen + 1);
		if (key == NULL || val == NULL) {
			free(key);
			free(val);
			return (0);
		}
		memcpy(key, data, keylen);
		key[keylen] = '\0';
		memcpy(val, sep + 1, vallen);
		val[vallen] = '\0';
	} else {
		size_t half = size / 2;
		if (half == 0)
			return (0);
		keylen = half;
		vallen = size - half;
		key = malloc(keylen + 1);
		val = malloc(vallen + 1);
		if (key == NULL || val == NULL) {
			free(key);
			free(val);
			return (0);
		}
		memcpy(key, data, keylen);
		key[keylen] = '\0';
		memcpy(val, data + half, vallen);
		val[vallen] = '\0';
	}

	if (pkg_new(&p, PKG_FILE) != EPKG_OK) {
		free(key);
		free(val);
		return (0);
	}

	pkg_kv_add(&p->annotations, key, val, "annotation");

	pkg_free(p);
	free(key);
	free(val);
	return (0);
}
