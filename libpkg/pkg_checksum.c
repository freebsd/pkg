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

#include <assert.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "blake2.h"

struct pkg_checksum_entry {
	const char *field;
	const char *value;
	struct pkg_checksum_entry *next, *prev;
};

/* Separate checksum parts */
#define PKG_CKSUM_SEPARATOR '$'

typedef void (*pkg_checksum_hash_func)(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
typedef void (*pkg_checksum_hash_bulk_func)(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen);
typedef void (*pkg_checksum_encode_func)(unsigned char *in, size_t inlen,
				char *out, size_t outlen);

static void pkg_checksum_hash_sha256(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_sha256_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_blake2(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_blake2_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_encode_base32(unsigned char *in, size_t inlen,
				char *out, size_t outlen);
static void pkg_checksum_encode_hex(unsigned char *in, size_t inlen,
				char *out, size_t outlen);

static const struct _pkg_cksum_type {
	const char *name;
	size_t hlen;
	pkg_checksum_hash_func hfunc;
	pkg_checksum_hash_bulk_func hbulkfunc;
	pkg_checksum_encode_func encfunc;
} checksum_types[] = {
	[PKG_HASH_TYPE_SHA256_BASE32] = {
		"sha256_base32",
		PKG_CHECKSUM_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_hash_sha256_bulk,
		pkg_checksum_encode_base32
	},
	[PKG_HASH_TYPE_SHA256_HEX] = {
		"sha256_hex",
		PKG_CHECKSUM_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_hash_sha256_bulk,
		pkg_checksum_encode_hex
	},
	[PKG_HASH_TYPE_BLAKE2_BASE32] = {
		"blake2_base32",
		PKG_CHECKSUM_BLAKE2_LEN,
		pkg_checksum_hash_blake2,
		pkg_checksum_hash_blake2_bulk,
		pkg_checksum_encode_hex
	},
	[PKG_HASH_TYPE_SHA256_RAW] = {
		"sha256_raw",
		SHA256_DIGEST_LENGTH,
		pkg_checksum_hash_sha256,
		pkg_checksum_hash_sha256_bulk,
		NULL
	},
	[PKG_HASH_TYPE_BLAKE2_RAW] = {
		"blake2_raw",
		BLAKE2B_OUTBYTES,
		pkg_checksum_hash_blake2,
		pkg_checksum_hash_blake2_bulk,
		NULL
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
 * - dependencies
 */

int
pkg_checksum_generate(struct pkg *pkg, char *dest, size_t destlen,
	pkg_checksum_type_t type)
{
	unsigned char *bdigest;
	char *olduid;
	size_t blen;
	struct pkg_checksum_entry *entries = NULL;
	struct pkg_option *option = NULL;
	struct pkg_shlib *shlib = NULL;
	struct pkg_user *user = NULL;
	struct pkg_group *group = NULL;
	struct pkg_dep *dep = NULL;
	int i;

	if (pkg == NULL || type >= PKG_HASH_TYPE_UNKNOWN ||
					destlen < checksum_types[type].hlen)
		return (EPKG_FATAL);

	pkg_checksum_add_entry("name", pkg->name, &entries);
	pkg_checksum_add_entry("origin", pkg->origin, &entries);
	pkg_checksum_add_entry("version", pkg->version, &entries);
	pkg_checksum_add_entry("arch", pkg->arch, &entries);

	while (pkg_options(pkg, &option) == EPKG_OK) {
		pkg_checksum_add_entry(option->key, option->value, &entries);
	}

	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		pkg_checksum_add_entry("required_shlib", shlib->name, &entries);
	}

	shlib = NULL;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
		pkg_checksum_add_entry("provided_shlib", shlib->name, &entries);
	}

	while (pkg_users(pkg, &user) == EPKG_OK) {
		pkg_checksum_add_entry("user", user->name, &entries);
	}

	while (pkg_groups(pkg, &group) == EPKG_OK) {
		pkg_checksum_add_entry("group", group->name, &entries);
	}

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		asprintf(&olduid, "%s~%s", dep->name, dep->origin);
		pkg_checksum_add_entry("depend", olduid, &entries);
		free(olduid);
	}

	/* Sort before hashing */
	DL_SORT(entries, pkg_checksum_entry_cmp);

	checksum_types[type].hfunc(entries, &bdigest, &blen);
	if (blen == 0 || bdigest == NULL) {
		LL_FREE(entries, free);
		return (EPKG_FATAL);
	}

	if (checksum_types[type].encfunc) {
		i = snprintf(dest, destlen, "%d%c%d%c", PKG_CHECKSUM_CUR_VERSION,
				PKG_CKSUM_SEPARATOR, type, PKG_CKSUM_SEPARATOR);
		assert(i < destlen);
		checksum_types[type].encfunc(bdigest, blen, dest + i, destlen - i);
	}
	else {
		/* For raw formats we just output digest */
		assert(destlen >= blen);
		memcpy(dest, bdigest, blen);
	}

	free(bdigest);
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

static void
pkg_checksum_hash_sha256_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen)
{
	SHA256_CTX sign_ctx;

	*out = malloc(SHA256_DIGEST_LENGTH);
	SHA256_Init(&sign_ctx);
	SHA256_Update(&sign_ctx, in, inlen);
	SHA256_Final(*out, &sign_ctx);
	*outlen = SHA256_DIGEST_LENGTH;
}

static void
pkg_checksum_hash_blake2(struct pkg_checksum_entry *entries,
		unsigned char **out, size_t *outlen)
{
	blake2b_state st;

	blake2b_init (&st, BLAKE2B_OUTBYTES);

	while(entries) {
		blake2b_update (&st, entries->field, strlen(entries->field));
		blake2b_update (&st, entries->value, strlen(entries->value));
		entries = entries->next;
	}
	*out = malloc(BLAKE2B_OUTBYTES);
	if (*out != NULL) {
		blake2b_final (&st, *out, BLAKE2B_OUTBYTES);
		*outlen = BLAKE2B_OUTBYTES;
	}
	else {
		pkg_emit_errno("malloc", "pkg_checksum_hash_blake2");
		*outlen = 0;
	}
}

static void
pkg_checksum_hash_blake2_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen)
{
	*out = malloc(BLAKE2B_OUTBYTES);
	blake2b(*out, in, NULL, BLAKE2B_OUTBYTES, inlen, 0);
	*outlen = BLAKE2B_OUTBYTES;
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
	int rc = EPKG_OK;
	pkg_checksum_type_t type = 0;

	if (pkg->reponame != NULL) {
		repo = pkg_repo_find(pkg->reponame);

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

	free(pkg->digest);
	pkg->digest = new_digest;

	if (db != NULL)
		pkgdb_set_pkg_digest(db, pkg);

	return (rc);
}


unsigned char *
pkg_checksum_data(const unsigned char *in, size_t inlen,
	pkg_checksum_type_t type)
{
	const struct _pkg_cksum_type *cksum;
	unsigned char *out, *res = NULL;
	size_t outlen;

	if (type >= PKG_HASH_TYPE_UNKNOWN || in == NULL)
		return (NULL);

	/* Zero terminated string */
	if (inlen == 0) {
		inlen = strlen(in);
	}

	cksum = &checksum_types[type];

	cksum->hbulkfunc(in, inlen, &out, &outlen);
	if (out != NULL) {
		if (cksum->encfunc != NULL) {
			res = malloc(cksum->hlen);
			cksum->encfunc(out, outlen, res, cksum->hlen);
			free(out);
		}
		else {
			res = out;
		}
	}

	return (res);
}
