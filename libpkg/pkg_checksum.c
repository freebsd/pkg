/* Copyright (c) 2014, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/sbuf.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ucl.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/utils.h"
#include "private/event.h"

struct pkg_checksum_entry {
	const char *field;
	const char *value;
	struct pkg_checksum_entry *next, *prev;
};

typedef void (*pkg_checksum_hash_func)(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
typedef void (*pkg_checksum_encode_func)(unsigned char *in, size_t inlen,
				char *out, size_t outlen);

static void pkg_checksum_hash_sha256(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_encode_base32(unsigned char *in, size_t inlen,
				char *out, size_t outlen);
static void pkg_checksum_encode_hex(unsigned char *in, size_t inlen,
				char *out, size_t outlen);

static const struct _pkg_cksum_type {
	size_t hlen;
	pkg_checksum_hash_func hfunc;
	pkg_checksum_encode_func encfunc;
} checksum_types[] = {
	[PKG_HASH_TYPE_SHA256_BASE32] = {
		PKG_HASH_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_encode_base32
	},
	[PKG_HASH_TYPE_SHA256_HEX] = {
		PKG_HASH_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_encode_hex
	},
	[PKG_HASH_TYPE_UNKNOWN] = {
		-1,
		NULL,
		NULL
	}
};

static void
pkg_checksum_add_object(const ucl_object_t *o, const char *key,
	struct pkg_checksum_entry **entries)
{
	struct pkg_checksum_entry *e;

	e = malloc(sizeof(*e));
	if (e == NULL) {
		pkg_emit_errno("malloc", "pkg_checksum_entry");
		return;
	}

	e->field = key;
	e->value = ucl_object_tostring(o);
	DL_APPEND(*entries, e);
}

static void
pkg_checksum_add_option(const struct pkg_option *o,
	struct pkg_checksum_entry **entries)
{
	struct pkg_checksum_entry *e;

	e = malloc(sizeof(*e));
	if (e == NULL) {
		pkg_emit_errno("malloc", "pkg_checksum_entry");
		return;
	}

	e->field = pkg_option_opt(o);
	e->value = pkg_option_value(o);
	DL_APPEND(*entries, e);
}

static int
pkg_checksum_entry_cmp(struct pkg_checksum_entry *e1,
	struct pkg_checksum_entry *e2)
{
	return (strcmp(e1->field, e2->field));
}

/*
 * At the moment we use the following fields to calculate the unique checksum
 * of the following fields:
 * - name
 * - origin
 * - version
 * - arch
 * - maintainer
 * - www
 * - message
 * - comment
 * - options
 */

int
pkg_checksum_generate(struct pkg *pkg, char *dest, size_t destlen,
	pkg_checksum_type_t type)
{
	const char *key;
	struct pkg_checksum_entry *entries = NULL;
	const ucl_object_t *o;
	struct pkg_option *option = NULL;
	int i;
	int recopies[] = {
		PKG_NAME,
		PKG_ORIGIN,
		PKG_VERSION,
		PKG_ARCH,
		PKG_MAINTAINER,
		PKG_WWW,
		PKG_MESSAGE,
		PKG_COMMENT,
		-1
	};

	if (pkg == NULL || type >= PKG_HASH_TYPE_UNKNOWN ||
					destlen < checksum_types[type].hlen)
		return (EPKG_FATAL);

	for (i = 0; recopies[i] != -1; i++) {
		key = pkg_keys[recopies[i]].name;
		if ((o = ucl_object_find_key(pkg->fields, key)))
			pkg_checksum_add_object(o, key, &entries);
	}

	while (pkg_options(pkg, &option) == EPKG_OK) {
		pkg_checksum_add_option(option, &entries);
	}

	/* Sort before hashing */
	DL_SORT(entries, pkg_checksum_entry_cmp);

	LL_FREE(entries, free);

	return (EPKG_OK);
}

bool
pkg_checksum_is_valid(const char *cksum, size_t clen)
{
	return (true);
}


pkg_checksum_type_t
pkg_checksum_get_type(const char *cksum, size_t clen)
{
	return (PKG_HASH_TYPE_UNKNOWN);
}


static void
pkg_checksum_hash_sha256(struct pkg_checksum_entry *entries,
		unsigned char **out, size_t *outlen)
{
	SHA256_CTX sign_ctx;

	SHA256_Init(&sign_ctx);

	while(entries) {
		SHA256_Update(&sign_ctx, entries->value, strlen(entries->value));
		entries = entries->next;
	}
	*out = malloc(SHA256_DIGEST_LENGTH);
	if (*out != NULL) {
		SHA256_Final(*out, &sign_ctx);
		*outlen = SHA256_DIGEST_LENGTH;
	}
	else {
		pkg_emit_errno("malloc", "pkg_checksum_hash_sha256");
		*outlen = 0;
	}
}


static void
pkg_checksum_encode_base32(unsigned char *in, size_t inlen,
				char *out, size_t outlen)
{

}

static void
pkg_checksum_encode_hex(unsigned char *in, size_t inlen,
				char *out, size_t outlen)
{

}
