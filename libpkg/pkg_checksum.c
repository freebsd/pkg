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

#include <sys/stat.h>

#include <fcntl.h>
#include <errno.h>
#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"
#include "sha256.h"
#include "blake2.h"

struct pkg_checksum_entry {
	const char *field;
	char *value;
	struct pkg_checksum_entry *next, *prev;
};

/* Separate checksum parts */
#define PKG_CKSUM_SEPARATOR '$'

/* Hash is in format <version>:<typeid>:<hexhash> */
#define PKG_CHECKSUM_SHA256_LEN (SHA256_BLOCK_SIZE * 2 + 1)
#define PKG_CHECKSUM_BLAKE2_LEN (BLAKE2B_OUTBYTES * 8 / 5 + sizeof("100") * 2 + 2)
#define PKG_CHECKSUM_BLAKE2S_LEN (BLAKE2S_OUTBYTES * 8 / 5 + sizeof("100") * 2 + 2)
#define PKG_CHECKSUM_CUR_VERSION 2

typedef void (*pkg_checksum_hash_func)(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
typedef void (*pkg_checksum_hash_bulk_func)(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen);
typedef void (*pkg_checksum_encode_func)(unsigned char *in, size_t inlen,
				char *out, size_t outlen);

typedef void (*pkg_checksum_hash_file_func)(int fd, unsigned char **out,
    size_t *outlen);

static void pkg_checksum_hash_sha256(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_sha256_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_sha256_file(int fd, unsigned char **out,
    size_t *outlen);
static void pkg_checksum_hash_blake2(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_blake2_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_blake2_file(int fd, unsigned char **out,
    size_t *outlen);
static void pkg_checksum_hash_blake2s(struct pkg_checksum_entry *entries,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_blake2s_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen);
static void pkg_checksum_hash_blake2s_file(int fd, unsigned char **out,
    size_t *outlen);
static void pkg_checksum_encode_base32(unsigned char *in, size_t inlen,
				char *out, size_t outlen);
static void pkg_checksum_encode_hex(unsigned char *in, size_t inlen,
				char *out, size_t outlen);

static const struct _pkg_cksum_type {
	const char *name;
	size_t hlen;
	pkg_checksum_hash_func hfunc;
	pkg_checksum_hash_bulk_func hbulkfunc;
	pkg_checksum_hash_file_func hfilefunc;
	pkg_checksum_encode_func encfunc;
} checksum_types[] = {
	[PKG_HASH_TYPE_SHA256_BASE32] = {
		"sha256_base32",
		PKG_CHECKSUM_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_hash_sha256_bulk,
		pkg_checksum_hash_sha256_file,
		pkg_checksum_encode_base32
	},
	[PKG_HASH_TYPE_SHA256_HEX] = {
		"sha256_hex",
		PKG_CHECKSUM_SHA256_LEN,
		pkg_checksum_hash_sha256,
		pkg_checksum_hash_sha256_bulk,
		pkg_checksum_hash_sha256_file,
		pkg_checksum_encode_hex
	},
	[PKG_HASH_TYPE_BLAKE2_BASE32] = {
		"blake2_base32",
		PKG_CHECKSUM_BLAKE2_LEN,
		pkg_checksum_hash_blake2,
		pkg_checksum_hash_blake2_bulk,
		pkg_checksum_hash_blake2_file,
		pkg_checksum_encode_base32
	},
	[PKG_HASH_TYPE_SHA256_RAW] = {
		"sha256_raw",
		SHA256_BLOCK_SIZE,
		pkg_checksum_hash_sha256,
		pkg_checksum_hash_sha256_bulk,
		pkg_checksum_hash_sha256_file,
		NULL
	},
	[PKG_HASH_TYPE_BLAKE2_RAW] = {
		"blake2_raw",
		BLAKE2B_OUTBYTES,
		pkg_checksum_hash_blake2,
		pkg_checksum_hash_blake2_bulk,
		pkg_checksum_hash_blake2_file,
		NULL
	},
	[PKG_HASH_TYPE_BLAKE2S_BASE32] = {
		"blake2s_base32",
		PKG_CHECKSUM_BLAKE2S_LEN,
		pkg_checksum_hash_blake2s,
		pkg_checksum_hash_blake2s_bulk,
		pkg_checksum_hash_blake2s_file,
		pkg_checksum_encode_base32
	},
	[PKG_HASH_TYPE_BLAKE2S_RAW] = {
		"blake2_raw",
		BLAKE2S_OUTBYTES,
		pkg_checksum_hash_blake2s,
		pkg_checksum_hash_blake2s_bulk,
		pkg_checksum_hash_blake2s_file,
		NULL
	},
	[PKG_HASH_TYPE_UNKNOWN] = {
		NULL,
		-1,
		NULL,
		NULL,
		NULL
	}
};

static void
pkg_checksum_free_entry(struct pkg_checksum_entry *e)
{
	if (e != NULL) {
		if (e->value) {
			free(e->value);
		}
		free(e);
	}
}

static void
pkg_checksum_add_entry(const char *key,
	const char *value,
	struct pkg_checksum_entry **entries)
{
	struct pkg_checksum_entry *e;

	e = xmalloc(sizeof(*e));
	e->field = key;
	e->value = xstrdup(value);
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
       pkg_checksum_type_t type, bool inc_scripts, bool inc_version,
       bool inc_files __unused)
{
	unsigned char *bdigest;
	char *olduid, *buf;
	size_t blen;
	struct pkg_checksum_entry *entries = NULL;
	struct pkg_option *option = NULL;
	struct pkg_dep *dep = NULL;
	struct pkg_file *f = NULL;
	int i;

	if (pkg == NULL || type >= PKG_HASH_TYPE_UNKNOWN ||
					destlen < checksum_types[type].hlen)
		return (EPKG_FATAL);

	pkg_checksum_add_entry("name", pkg->name, &entries);
	pkg_checksum_add_entry("origin", pkg->origin, &entries);
	if (inc_version)
		pkg_checksum_add_entry("version", pkg->version, &entries);
	pkg_checksum_add_entry("arch", pkg->arch, &entries);

	while (pkg_options(pkg, &option) == EPKG_OK) {
		pkg_checksum_add_entry(option->key, option->value, &entries);
	}

	buf = NULL;
	while (pkg_shlibs_required(pkg, &buf) == EPKG_OK) {
		pkg_checksum_add_entry("required_shlib", buf, &entries);
	}

	buf = NULL;
	while (pkg_shlibs_provided(pkg, &buf) == EPKG_OK) {
		pkg_checksum_add_entry("provided_shlib", buf, &entries);
	}

	buf = NULL;
	while (pkg_users(pkg, &buf) == EPKG_OK) {
		pkg_checksum_add_entry("user", buf, &entries);
	}

	buf = NULL;
	while (pkg_groups(pkg, &buf) == EPKG_OK) {
		pkg_checksum_add_entry("group", buf, &entries);
	}

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		xasprintf(&olduid, "%s~%s", dep->name, dep->origin);
		pkg_checksum_add_entry("depend", olduid, &entries);
		free(olduid);
	}

	buf = NULL;
	while (pkg_provides(pkg, &buf) == EPKG_OK) {
		pkg_checksum_add_entry("provide", buf, &entries);
	}

	buf = NULL;
	while (pkg_requires(pkg, &buf) == EPKG_OK) {
		pkg_checksum_add_entry("require", buf, &entries);
	}

	if (inc_scripts) {
		for (int i = 0; i < PKG_NUM_SCRIPTS; i++) {
			if (pkg->scripts[i] != NULL) {
				fflush(pkg->scripts[i]->fp);
				pkg_checksum_add_entry("script",
				    pkg->scripts[i]->buf,
				    &entries);
			}
		}
		for (int i = 0; i < PKG_NUM_LUA_SCRIPTS; i++) {
			if (pkg->lua_scripts[i] != NULL)
				pkg_checksum_add_entry("lua_script",
				    pkg->lua_scripts[i]->script,
				    &entries);
		}
	}

	while (pkg_files(pkg, &f) == EPKG_OK) {
		pkg_checksum_add_entry(f->path, f->sum, &entries);
	}

	/* Sort before hashing */
	DL_SORT(entries, pkg_checksum_entry_cmp);

	checksum_types[type].hfunc(entries, &bdigest, &blen);
	if (blen == 0 || bdigest == NULL) {
		LL_FREE(entries, pkg_checksum_free_entry);
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
	LL_FREE(entries, pkg_checksum_free_entry);

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

/* <hashtype>$<hash> */
pkg_checksum_type_t
pkg_checksum_file_get_type(const char *cksum, size_t clen __unused)
{
	unsigned int value;

	if (strchr(cksum, PKG_CKSUM_SEPARATOR) == NULL)
		return (PKG_HASH_TYPE_UNKNOWN);

	value = strtoul(cksum, NULL, 10);
	if (value < PKG_HASH_TYPE_UNKNOWN)
		return (value);

	return (PKG_HASH_TYPE_UNKNOWN);
}

/* <version>$<hashtype>$<hash> */
pkg_checksum_type_t
pkg_checksum_get_type(const char *cksum, size_t clen __unused)
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

	sha256_init(&sign_ctx);

	while(entries) {
		sha256_update(&sign_ctx, entries->field, strlen(entries->field));
		sha256_update(&sign_ctx, entries->value, strlen(entries->value));
		entries = entries->next;
	}
	*out = xmalloc(SHA256_BLOCK_SIZE);
	sha256_final(&sign_ctx, *out);
	*outlen = SHA256_BLOCK_SIZE;
}

static void
pkg_checksum_hash_sha256_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen)
{
	SHA256_CTX sign_ctx;

	*out = xmalloc(SHA256_BLOCK_SIZE);
	sha256_init(&sign_ctx);
	sha256_update(&sign_ctx, in, inlen);
	sha256_final(&sign_ctx, *out);
	*outlen = SHA256_BLOCK_SIZE;
}

static void
pkg_checksum_hash_sha256_file(int fd, unsigned char **out, size_t *outlen)
{
	char buffer[8192];
	ssize_t r;

	SHA256_CTX sign_ctx;
	*out = xmalloc(SHA256_BLOCK_SIZE);
	sha256_init(&sign_ctx);
	while ((r = read(fd, buffer, sizeof(buffer))) > 0)
		sha256_update(&sign_ctx, buffer, r);
	if (r < 0) {
		pkg_emit_errno(__func__, "read failed");
		free(*out);
		*out = NULL;
		return;
	}
	sha256_final(&sign_ctx, *out);
	*outlen = SHA256_BLOCK_SIZE;
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
	*out = xmalloc(BLAKE2B_OUTBYTES);
	blake2b_final (&st, *out, BLAKE2B_OUTBYTES);
	*outlen = BLAKE2B_OUTBYTES;
}

static void
pkg_checksum_hash_blake2_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen)
{
	*out = xmalloc(BLAKE2B_OUTBYTES);
	blake2b(*out, BLAKE2B_OUTBYTES,  in, inlen, NULL, 0);
	*outlen = BLAKE2B_OUTBYTES;
}

static void
pkg_checksum_hash_blake2_file(int fd, unsigned char **out, size_t *outlen)
{
	char buffer[8192];
	ssize_t r;

	blake2b_state st;
	blake2b_init(&st, BLAKE2B_OUTBYTES);

	while ((r = read(fd, buffer, sizeof(buffer))) > 0)
		blake2b_update(&st, buffer, r);
	if (r < 0) {
		pkg_emit_errno(__func__, "read failed");
		free(*out);
		*out = NULL;
		return;
	}
	*out = xmalloc(BLAKE2B_OUTBYTES);
	blake2b_final(&st, *out, BLAKE2B_OUTBYTES);
	*outlen = BLAKE2B_OUTBYTES;
}

static void
pkg_checksum_hash_blake2s(struct pkg_checksum_entry *entries,
		unsigned char **out, size_t *outlen)
{
	blake2s_state st;

	blake2s_init (&st, BLAKE2S_OUTBYTES);

	while(entries) {
		blake2s_update (&st, entries->field, strlen(entries->field));
		blake2s_update (&st, entries->value, strlen(entries->value));
		entries = entries->next;
	}
	*out = xmalloc(BLAKE2S_OUTBYTES);
	blake2s_final (&st, *out, BLAKE2S_OUTBYTES);
	*outlen = BLAKE2S_OUTBYTES;
}

static void
pkg_checksum_hash_blake2s_bulk(const unsigned char *in, size_t inlen,
				unsigned char **out, size_t *outlen)
{
	*out = xmalloc(BLAKE2S_OUTBYTES);
	blake2s(*out, BLAKE2S_OUTBYTES,  in, inlen, NULL, 0);
	*outlen = BLAKE2S_OUTBYTES;
}

static void
pkg_checksum_hash_blake2s_file(int fd, unsigned char **out, size_t *outlen)
{
	char buffer[8192];
	ssize_t r;

	blake2s_state st;
	blake2s_init(&st, BLAKE2S_OUTBYTES);

	while ((r = read(fd, buffer, sizeof(buffer))) > 0)
		blake2s_update(&st, buffer, r);
	if (r < 0) {
		pkg_emit_errno(__func__, "read failed");
		free(*out);
		*out = NULL;
		return;
	}
	*out = xmalloc(BLAKE2S_OUTBYTES);
	blake2s_final(&st, *out, BLAKE2S_OUTBYTES);
	*outlen = BLAKE2S_OUTBYTES;
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
pkg_checksum_calculate(struct pkg *pkg, struct pkgdb *db, bool inc_scripts,
    bool inc_version, bool inc_files)
{
	char *new_digest;
	struct pkg_repo *repo;
	int rc = EPKG_OK;
	pkg_checksum_type_t type;

	if (sizeof(void *) == 8)
		type = PKG_HASH_TYPE_BLAKE2_BASE32;
	else
		type = PKG_HASH_TYPE_BLAKE2S_BASE32;

	if (pkg->reponame != NULL) {
		repo = pkg_repo_find(pkg->reponame);

		if (repo != NULL)
			type = repo->meta->digest_format;
	}

	new_digest = xmalloc(pkg_checksum_type_size(type));
	if (pkg_checksum_generate(pkg, new_digest, pkg_checksum_type_size(type),
	    type, inc_scripts, inc_version, inc_files)
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
			res = xmalloc(cksum->hlen);
			cksum->encfunc(out, outlen, res, cksum->hlen);
			free(out);
		}
		else {
			res = out;
		}
	}

	return (res);
}

unsigned char *
pkg_checksum_fileat(int rootfd, const char *path, pkg_checksum_type_t type)
{
	int fd;
	unsigned char *ret;

	if ((fd = openat(rootfd, path, O_RDONLY)) == -1) {
		pkg_emit_errno("open", path);
		return (NULL);
	}

	ret = pkg_checksum_fd(fd, type);

	close(fd);

	return (ret);
}

unsigned char *
pkg_checksum_file(const char *path, pkg_checksum_type_t type)
{
	int fd;
	unsigned char *ret;

	if ((fd = open(path, O_RDONLY)) == -1) {
		pkg_emit_errno("open", path);
		return (NULL);
	}

	ret = pkg_checksum_fd(fd, type);

	close(fd);

	return (ret);
}

unsigned char *
pkg_checksum_fd(int fd, pkg_checksum_type_t type)
{
	const struct _pkg_cksum_type *cksum;
	unsigned char *out, *res = NULL;
	size_t outlen;

	if (type >= PKG_HASH_TYPE_UNKNOWN || fd < 0)
		return (NULL);

	cksum = &checksum_types[type];
	cksum->hfilefunc(fd, &out, &outlen);
	if (out != NULL) {
		if (cksum->encfunc != NULL) {
			res = xmalloc(cksum->hlen);
			cksum->encfunc(out, outlen, res, cksum->hlen);
			free(out);
		} else {
			res = out;
		}
	}

	return (res);
}

static unsigned char *
pkg_checksum_symlink_readlink(const char *linkbuf, int linklen,
    pkg_checksum_type_t type)
{
	const char *lnk;

	lnk = linkbuf;

	/*
	 * It is known that \0 is added to the checksum in case the symlink
	 * targets an absolute path but the behaviour is kept for compat
	 */
	return (pkg_checksum_data(RELATIVE_PATH(lnk), linklen, type));
}

unsigned char *
pkg_checksum_symlink(const char *path, pkg_checksum_type_t type)
{
	char linkbuf[MAXPATHLEN];
	int linklen;

	if ((linklen = readlink(path, linkbuf, sizeof(linkbuf) - 1)) == -1) {
		pkg_emit_errno("pkg_checksum_symlink", "readlink failed");
		return (NULL);
	}
	linkbuf[linklen] = '\0';

	return (pkg_checksum_symlink_readlink(linkbuf, linklen, type));
}

unsigned char *
pkg_checksum_symlinkat(int fd, const char *path, pkg_checksum_type_t type)
{
	char linkbuf[MAXPATHLEN];
	int linklen;

	if ((linklen = readlinkat(fd, path, linkbuf, sizeof(linkbuf) - 1)) == -1) {
		pkg_emit_errno("pkg_checksum_symlinkat", "readlink failed");
		return (NULL);
	}
	linkbuf[linklen] = '\0';

	return (pkg_checksum_symlink_readlink(linkbuf, linklen, type));
}

int
pkg_checksum_validate_file(const char *path, const char *sum)
{
	struct stat st;
	char *newsum;
	pkg_checksum_type_t type;

	type = pkg_checksum_file_get_type(sum, strlen(sum));
	if (type == PKG_HASH_TYPE_UNKNOWN) {
		type = PKG_HASH_TYPE_SHA256_HEX;
	} else {
		sum = strchr(sum, PKG_CKSUM_SEPARATOR);
		if (sum != NULL)
			sum++;
	}

	if (lstat(path, &st) == -1) {
		return (errno);
	}

	if (S_ISLNK(st.st_mode))
		newsum = pkg_checksum_symlink(path, type);
	else
		newsum = pkg_checksum_file(path, type);

	if (newsum == NULL)
		return (-1);

	if (strcmp(sum, newsum) != 0) {
		free(newsum);
		return (-1);
	}

	free(newsum);

	return (0);
}

char *
pkg_checksum_generate_file(const char *path, pkg_checksum_type_t type)
{
	struct stat st;
	unsigned char *sum;
	char *cksum;

	if (lstat(path, &st) == -1) {
		pkg_emit_errno("pkg_checksum_generate_file", "lstat");
		return (NULL);
	}

	if (S_ISLNK(st.st_mode))
		sum = pkg_checksum_symlink(path, type);
	else
		sum = pkg_checksum_file(path, type);

	if (sum == NULL)
		return (NULL);

	xasprintf(&cksum, "%d%c%s", type, PKG_CKSUM_SEPARATOR, sum);
	free(sum);

	return (cksum);
}

int
pkg_checksum_validate_fileat(int rootfd, const char *path, const char *sum)
{
	struct stat st;
	char *newsum;
	pkg_checksum_type_t type;

	type = pkg_checksum_file_get_type(sum, strlen(sum));
	if (type == PKG_HASH_TYPE_UNKNOWN) {
		type = PKG_HASH_TYPE_SHA256_HEX;
	} else {
		sum = strchr(sum, PKG_CKSUM_SEPARATOR);
		if (sum != NULL)
			sum++;
	}

	if (fstatat(rootfd, path, &st, AT_SYMLINK_NOFOLLOW) == -1) {
		return (errno);
	}

	if (S_ISLNK(st.st_mode))
		newsum = pkg_checksum_symlinkat(rootfd, path, type);
	else
		newsum = pkg_checksum_fileat(rootfd, path, type);

	if (newsum == NULL)
		return (-1);

	if (strcmp(sum, newsum) != 0) {
		free(newsum);
		return (-1);
	}

	free(newsum);

	return (0);
}

char *
pkg_checksum_generate_fileat(int rootfd, const char *path,
    pkg_checksum_type_t type)
{
	struct stat st;
	unsigned char *sum;
	char *cksum;

	if (fstatat(rootfd, path, &st, AT_SYMLINK_NOFOLLOW) == -1) {
		pkg_emit_errno("pkg_checksum_generate_file", "lstat");
		return (NULL);
	}

	if (S_ISLNK(st.st_mode))
		sum = pkg_checksum_symlinkat(rootfd, path, type);
	else
		sum = pkg_checksum_fileat(rootfd, path, type);

	if (sum == NULL)
		return (NULL);

	xasprintf(&cksum, "%d%c%s", type, PKG_CKSUM_SEPARATOR, sum);
	free(sum);

	return (cksum);
}
