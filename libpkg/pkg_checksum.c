/* Copyright (c) 2014, Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2014, Google Inc.
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

/* Separate checksum parts */
#define PKG_CKSUM_SEPARATOR '$'

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
	const char *name;
	size_t hlen;
	pkg_checksum_hash_func hfunc;
	pkg_checksum_encode_func encfunc;
} checksum_types[] = {
	[PKG_HASH_TYPE_SHA256_BASE32] = {
		"sha256_base32",
		PKG_CHECKSUM_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_encode_base32
	},
	[PKG_HASH_TYPE_SHA256_HEX] = {
		"sha256_hex",
		PKG_CHECKSUM_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_encode_hex
	},
	[PKG_HASH_TYPE_UNKNOWN] = {
		NULL,
		-1,
		NULL,
		NULL
	}
};

static void
pkg_checksum_add_entry(const char *key,
	const char *value,
	struct pkg_checksum_entry **entries)
{
	struct pkg_checksum_entry *e;

	e = malloc(sizeof(*e));
	if (e == NULL) {
		pkg_emit_errno("malloc", "pkg_checksum_entry");
		return;
	}

	e->field = key;
	e->value = value;
	DL_APPEND(*entries, e);
}

static int
pkg_checksum_entry_cmp(struct pkg_checksum_entry *e1,
	struct pkg_checksum_entry *e2)
{
	int r;

	/* Compare field names first. */
	r = strcmp(e1->field, e2->field);
	if (r != 0)
		return r;

	/* If field names are the same, compare values. */
	return (strcmp(e1->value, e2->value));
}

/*
 * At the moment we use the following fields to calculate the unique checksum
 * of the following fields:
 * - name
 * - origin
 * - version
 * - arch
 * - options
 * - required_shlibs
 * - provided_shlibs
 * - users
 * - groups
 */

int
pkg_checksum_generate(struct pkg *pkg, char *dest, size_t destlen,
	pkg_checksum_type_t type)
{
	const char *key;
	unsigned char *bdigest;
	size_t blen;
	struct pkg_checksum_entry *entries = NULL;
	const ucl_object_t *o;
	struct pkg_option *option = NULL;
	struct pkg_shlib *shlib = NULL;
	struct pkg_user *user = NULL;
	struct pkg_group *group = NULL;
	struct pkg_dep *dep = NULL;
	int i;
	int recopies[] = {
		PKG_NAME,
		PKG_ORIGIN,
		PKG_VERSION,
		PKG_ARCH,
		-1
	};

	if (pkg == NULL || type >= PKG_HASH_TYPE_UNKNOWN ||
					destlen < checksum_types[type].hlen)
		return (EPKG_FATAL);

	for (i = 0; recopies[i] != -1; i++) {
		key = pkg_keys[recopies[i]].name;
		if ((o = ucl_object_find_key(pkg->fields, key)))
			pkg_checksum_add_entry(key, ucl_object_tostring(o), &entries);
	}

	while (pkg_options(pkg, &option) == EPKG_OK) {
		pkg_checksum_add_entry(pkg_option_opt(option), pkg_option_value(option),
			&entries);
	}

	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		pkg_checksum_add_entry("required_shlib", pkg_shlib_name(shlib), &entries);
	}

	shlib = NULL;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
		pkg_checksum_add_entry("provided_shlib", pkg_shlib_name(shlib), &entries);
	}

	while (pkg_users(pkg, &user) == EPKG_OK) {
		pkg_checksum_add_entry("user", pkg_user_name(user), &entries);
	}

	while (pkg_groups(pkg, &group) == EPKG_OK) {
		pkg_checksum_add_entry("group", pkg_group_name(group), &entries);
	}

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		pkg_checksum_add_entry("depend", dep->uid, &entries);
	}

	/* Sort before hashing */
	DL_SORT(entries, pkg_checksum_entry_cmp);

	checksum_types[type].hfunc(entries, &bdigest, &blen);
	if (blen == 0 || bdigest == NULL) {
		LL_FREE(entries, free);
		return (EPKG_FATAL);
	}

	i = snprintf(dest, destlen, "%d%c%d%c", PKG_CHECKSUM_CUR_VERSION,
		PKG_CKSUM_SEPARATOR, type, PKG_CKSUM_SEPARATOR);
	assert(i < destlen);
	checksum_types[type].encfunc(bdigest, blen, dest + i, destlen - i);

	LL_FREE(entries, free);

	return (EPKG_OK);
}

bool
pkg_checksum_is_valid(const char *cksum, size_t clen)
{
	const char *sep;
	unsigned int value;

	if (clen < 4)
		return (false);

	sep = strchr(cksum, PKG_CKSUM_SEPARATOR);
	if (sep == NULL || *sep == '\0')
		return (false);

	/* Test version */
	value = strtoul(cksum, NULL, 10);
	if (value != PKG_CHECKSUM_CUR_VERSION)
		return (false);

	cksum = sep + 1;
	sep = strchr(cksum, PKG_CKSUM_SEPARATOR);
	if (sep == NULL || *sep == '\0')
		return (false);

	/* Test type */
	value = strtoul(cksum, NULL, 10);
	if (value >= PKG_HASH_TYPE_UNKNOWN)
		return (false);

	return (true);
}


pkg_checksum_type_t
pkg_checksum_get_type(const char *cksum, size_t clen)
{
	const char *sep;
	unsigned int value;

	sep = strchr(cksum, PKG_CKSUM_SEPARATOR);
	if (sep != NULL && *sep != '\0') {
		value = strtoul(sep + 1, NULL, 10);
		if (value < PKG_HASH_TYPE_UNKNOWN)
			return (value);
	}

	return (PKG_HASH_TYPE_UNKNOWN);
}


static void
pkg_checksum_hash_sha256(struct pkg_checksum_entry *entries,
		unsigned char **out, size_t *outlen)
{
	SHA256_CTX sign_ctx;

	SHA256_Init(&sign_ctx);

	while(entries) {
		SHA256_Update(&sign_ctx, entries->field, strlen(entries->field));
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

/*
 * We use here z-base32 encoding described here:
 * http://philzimmermann.com/docs/human-oriented-base-32-encoding.txt
 */
static const char b32[]="ybndrfg8ejkmcpqxot1uwisza345h769";


static void
pkg_checksum_encode_base32(unsigned char *in, size_t inlen,
				char *out, size_t outlen)
{
	int i, remain = -1, r, x;

	if (outlen < inlen * 8 / 5) {
		pkg_emit_error("cannot encode base32 as outlen is not sufficient");
		return;
	}

	for (i = 0, r = 0; i < inlen; i++) {
		switch (i % 5) {
		case 0:
			/* 8 bits of input and 3 to remain */
			x = in[i];
			remain = in[i] >> 5;
			out[r++] = b32[x & 0x1F];
			break;
		case 1:
			/* 11 bits of input, 1 to remain */
			x = remain | in[i] << 3;
			out[r++] = b32[x & 0x1F];
			out[r++] = b32[x >> 5 & 0x1F];
			remain = x >> 10;
			break;
		case 2:
			/* 9 bits of input, 4 to remain */
			x = remain | in[i] << 1;
			out[r++] = b32[x & 0x1F];
			remain = x >> 5;
			break;
		case 3:
			/* 12 bits of input, 2 to remain */
			x = remain | in[i] << 4;
			out[r++] = b32[x & 0x1F];
			out[r++] = b32[x >> 5 & 0x1F];
			remain = x >> 10 & 0x3;
			break;
		case 4:
			/* 10 bits of output, nothing to remain */
			x = remain | in[i] << 2;
			out[r++] = b32[x & 0x1F];
			out[r++] = b32[x >> 5 & 0x1F];
			remain = -1;
			break;
		default:
			/* Not to be happen */
			break;
		}

	}
	if (remain >= 0)
		out[r++] = b32[remain];

	out[r] = 0;
}

static void
pkg_checksum_encode_hex(unsigned char *in, size_t inlen,
				char *out, size_t outlen)
{
	int i;

	if (outlen < inlen * 2) {
		pkg_emit_error("cannot encode hex as outlen is not sufficient");
		return;
	}

	for (i = 0; i < inlen; i++)
		sprintf(out + (i * 2), "%02x", in[i]);

	out[inlen * 2] = '\0';
}

pkg_checksum_type_t
pkg_checksum_type_from_string(const char *name)
{
	int i;
	for (i = 0; i < PKG_HASH_TYPE_UNKNOWN; i ++) {
		if (strcasecmp(name, checksum_types[i].name) == 0)
			return (i);
	}

	return (PKG_HASH_TYPE_UNKNOWN);
}

const char*
pkg_checksum_type_to_string(pkg_checksum_type_t type)
{
	return (checksum_types[type].name);
}

size_t
pkg_checksum_type_size(pkg_checksum_type_t type)
{
	return (checksum_types[type].hlen);
}

int
pkg_checksum_calculate(struct pkg *pkg, struct pkgdb *db)
{
	char *new_digest;
	struct pkg_repo *repo;
	const char *reponame;
	int rc = EPKG_OK;
	pkg_checksum_type_t type = 0;

	pkg_get(pkg, PKG_REPONAME, &reponame);
	if (reponame != NULL) {
		repo = pkg_repo_find(reponame);

		if (repo != NULL)
			type = repo->meta->digest_format;
	}

	new_digest = malloc(pkg_checksum_type_size(type));
	if (new_digest == NULL) {
		pkg_emit_errno("malloc", "pkg_checksum_type_t");
		return (EPKG_FATAL);
	}

	if (pkg_checksum_generate(pkg, new_digest, pkg_checksum_type_size(type), type)
			!= EPKG_OK) {
		free(new_digest);
		return (EPKG_FATAL);
	}

	pkg_set(pkg, PKG_DIGEST, new_digest);

	if (db != NULL)
		pkgdb_set_pkg_digest(db, pkg);

	free(new_digest);

	return (rc);
}
