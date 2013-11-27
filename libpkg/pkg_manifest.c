/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

#define PKG_UNKNOWN		-1
#define PKG_DEPS		-2
#define PKG_FILES		-3
#define PKG_DIRS		-4
#define PKG_SCRIPTS		-5
#define PKG_CATEGORIES		-6
#define PKG_LICENSES		-7
#define PKG_OPTIONS		-8
#define PKG_OPTION_DEFAULTS	-9
#define PKG_OPTION_DESCRIPTIONS	-10
#define PKG_USERS		-11
#define PKG_GROUPS		-12
#define PKG_DIRECTORIES		-13
#define PKG_SHLIBS_REQUIRED	-14
#define PKG_SHLIBS_PROVIDED	-15
#define PKG_ANNOTATIONS		-16
#define PKG_INFOS		-17	/* Deprecated field: treat as an annotation for backwards compatibility */

static int pkg_string(struct pkg *, ucl_object_t *, int);
static int pkg_object(struct pkg *, ucl_object_t *, int);
static int pkg_array(struct pkg *, ucl_object_t *, int);
static int pkg_int(struct pkg *, ucl_object_t *, int);
static int pkg_set_deps_from_object(struct pkg *, ucl_object_t *);
static int pkg_set_files_from_object(struct pkg *, ucl_object_t *);
static int pkg_set_dirs_from_object(struct pkg *, ucl_object_t *);

/*
 * Keep sorted
 */
static struct manifest_key {
	const char *key;
	int type;
	enum ucl_type valid_type;
	int (*parse_data)(struct pkg *, ucl_object_t *, int);
} manifest_keys[] = {
	{ "annotations",         PKG_ANNOTATIONS,         UCL_OBJECT, pkg_object},
	{ "arch",                PKG_ARCH,                UCL_STRING, pkg_string},
	{ "categories",          PKG_CATEGORIES,          UCL_ARRAY,  pkg_array},
	{ "comment",             PKG_COMMENT,             UCL_STRING, pkg_string},
	{ "deps",                PKG_DEPS,                UCL_OBJECT, pkg_object},
	{ "desc",                PKG_DESC,                UCL_STRING, pkg_string},
	{ "directories",         PKG_DIRECTORIES,         UCL_OBJECT, pkg_object},
	{ "dirs",                PKG_DIRS,                UCL_ARRAY,  pkg_array},
	{ "files",               PKG_FILES,               UCL_OBJECT, pkg_object},
	{ "flatsize",            PKG_FLATSIZE,            UCL_INT,    pkg_int},
	{ "groups",              PKG_GROUPS,              UCL_OBJECT, pkg_object},
	{ "groups",              PKG_GROUPS,              UCL_ARRAY,  pkg_array},
	{ "infos",               PKG_INFOS,               UCL_STRING, pkg_string}, /* Deprecated: treat as an annotation */
	{ "licenselogic",        PKG_LICENSE_LOGIC,       UCL_STRING, pkg_string},
	{ "licenses",            PKG_LICENSES,            UCL_ARRAY,  pkg_array},
	{ "maintainer",          PKG_MAINTAINER,          UCL_STRING, pkg_string},
	{ "message",             PKG_MESSAGE,             UCL_STRING, pkg_string},
	{ "name",                PKG_NAME,                UCL_STRING, pkg_string},
	{ "name",                PKG_NAME,                UCL_INT,    pkg_string},
	{ "options",             PKG_OPTIONS,             UCL_OBJECT, pkg_object},
	{ "option_defaults",     PKG_OPTION_DEFAULTS,     UCL_OBJECT, pkg_object},
	{ "option_descriptions", PKG_OPTION_DESCRIPTIONS, UCL_OBJECT, pkg_object},
	{ "origin",              PKG_ORIGIN,              UCL_STRING, pkg_string},
	{ "path",                PKG_REPOPATH,            UCL_STRING, pkg_string},
	{ "pkgsize",             PKG_PKGSIZE,             UCL_INT,    pkg_int},
	{ "prefix",              PKG_PREFIX,              UCL_STRING, pkg_string},
	{ "scripts",             PKG_SCRIPTS,             UCL_OBJECT, pkg_object},
	{ "shlibs",              PKG_SHLIBS_REQUIRED,     UCL_ARRAY,  pkg_array}, /* Backwards compat with 1.0.x packages */
	{ "shlibs_provided",     PKG_SHLIBS_PROVIDED,     UCL_ARRAY,  pkg_array},
	{ "shlibs_required",     PKG_SHLIBS_REQUIRED,     UCL_ARRAY,  pkg_array},
	{ "sum",                 PKG_CKSUM,               UCL_STRING, pkg_string},
	{ "users",               PKG_USERS,               UCL_OBJECT, pkg_object},
	{ "users",               PKG_USERS,               UCL_ARRAY,  pkg_array},
	{ "version",             PKG_VERSION,             UCL_STRING, pkg_string},
	{ "version",             PKG_VERSION,             UCL_INT,    pkg_string},
	{ "www",                 PKG_WWW,                 UCL_STRING, pkg_string},
	{ NULL, -99, -99, NULL}
};

struct dataparser {
	enum ucl_type type;
	int (*parse_data)(struct pkg *, ucl_object_t *, int);
	UT_hash_handle hh;
};

struct pkg_manifest_key {
	const char *key;
	int type;
	struct dataparser *parser;
	UT_hash_handle hh;
};

int
pkg_manifest_keys_new(struct pkg_manifest_key **key)
{
	int i;
	struct pkg_manifest_key *k;
	struct dataparser *dp;

	if (*key != NULL)
		return (EPKG_OK);

	for (i = 0; manifest_keys[i].key != NULL; i++) {
		HASH_FIND_STR(*key, manifest_keys[i].key, k);
		if (k == NULL) {
			k = calloc(1, sizeof(struct pkg_manifest_key));
			k->key = manifest_keys[i].key;
			k->type = manifest_keys[i].type;
			HASH_ADD_KEYPTR(hh, *key, k->key, strlen(k->key), k);
		}
		HASH_FIND_UCLT(k->parser, &manifest_keys[i].valid_type, dp);
		if (dp != NULL)
			continue;
		dp = calloc(1, sizeof(struct dataparser));
		dp->type = manifest_keys[i].valid_type;
		dp->parse_data = manifest_keys[i].parse_data;
		HASH_ADD_UCLT(k->parser, type, dp);
	}

	return (EPKG_OK);
}

static void
pmk_free(struct pkg_manifest_key *key) {
	HASH_FREE(key->parser, dataparser, free);

	free(key);
}

void
pkg_manifest_keys_free(struct pkg_manifest_key *key)
{
	if (key == NULL)
		return;

	HASH_FREE(key, pkg_manifest_key, pmk_free);
}

static int
urlencode(const char *src, struct sbuf **dest)
{
	size_t len;
	size_t i;

	sbuf_init(dest);

	len = strlen(src);
	for (i = 0; i < len; i++) {
		if (!isascii(src[i]) || src[i] == '%')
			sbuf_printf(*dest, "%%%.2x", (unsigned char)src[i]);
		else
			sbuf_putc(*dest, src[i]);
	}
	sbuf_finish(*dest);

	return (EPKG_OK);
}


static int
urldecode(const char *src, struct sbuf **dest)
{
	size_t len;
	size_t i;
	char c;
	char hex[] = {'\0', '\0', '\0'};

	sbuf_init(dest);

	len = strlen(src);
	for (i = 0; i < len; i++) {
		if (src[i] != '%') {
			sbuf_putc(*dest, src[i]);
		} else {
			if (i + 2 > len) {
				pkg_emit_error("unexpected end of string");
				return (EPKG_FATAL);
			}

			hex[0] = src[++i];
			hex[1] = src[++i];
			errno = 0;
			c = strtol(hex, NULL, 16);
			if (errno != 0) {
				/*
				 * if it fails consider this is not a urlencoded
				 * information
				 */
				sbuf_printf(*dest, "%%%s", hex);
			} else {
				sbuf_putc(*dest, c);
			}
		}
	}
	sbuf_finish(*dest);

	return (EPKG_OK);
}

static int
script_type_str(const char *str)
{
	if (strcmp(str, "pre-install") == 0)
		return (PKG_SCRIPT_PRE_INSTALL);
	if (strcmp(str, "install") == 0)
		return (PKG_SCRIPT_INSTALL);
	if (strcmp(str, "post-install") == 0)
		return (PKG_SCRIPT_POST_INSTALL);
	if (strcmp(str, "pre-upgrade") == 0)
		return (PKG_SCRIPT_PRE_UPGRADE);
	if (strcmp(str, "upgrade") == 0)
		return (PKG_SCRIPT_UPGRADE);
	if (strcmp(str, "post-upgrade") == 0)
		return (PKG_SCRIPT_POST_UPGRADE);
	if (strcmp(str, "pre-deinstall") == 0)
		return (PKG_SCRIPT_PRE_DEINSTALL);
	if (strcmp(str, "deinstall") == 0)
		return (PKG_SCRIPT_DEINSTALL);
	if (strcmp(str, "post-deinstall") == 0)
		return (PKG_SCRIPT_POST_DEINSTALL);
	return (PKG_SCRIPT_UNKNOWN);
}

static int
pkg_string(struct pkg *pkg, ucl_object_t *obj, int attr)
{
	int ret = EPKG_OK;
	const char *str;
	str = ucl_object_tostring_forced(obj);

	switch (attr)
	{
	case PKG_INFOS:
		pkg_addannotation(pkg, "_INFOS_", str);
		break;
	case PKG_LICENSE_LOGIC:
		if (!strcmp(str, "single"))
			pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t) LICENSE_SINGLE);
		else if (!strcmp(str, "or") ||
		         !strcmp(str, "dual"))
			pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_OR);
		else if (!strcmp(str, "and") ||
		         !strcmp(str, "multi"))
			pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_AND);
		else {
			pkg_emit_error("Unknown license logic: %s", str);
			ret = EPKG_FATAL;
		}
		break;
	default:
		ret = urldecode(str, &pkg->fields[attr]);
		break;
	}

	return (ret);
}

static int
pkg_int(struct pkg *pkg, ucl_object_t *obj, int attr)
{
	return (pkg_set(pkg, attr, ucl_object_toint(obj)));
}

static int
pkg_array(struct pkg *pkg, ucl_object_t *obj, int attr)
{
	ucl_object_t *cur;
	ucl_object_iter_t it = NULL;

	pkg_debug(3, "%s", "Manifest: parsing array");
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		switch (attr) {
		case PKG_CATEGORIES:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed category");
			else
				pkg_addcategory(pkg, ucl_object_tostring(cur));
			break;
		case PKG_LICENSES:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed license");
			else
				pkg_addlicense(pkg, ucl_object_tostring(cur));
			break;
		case PKG_USERS:
			if (cur->type == UCL_STRING)
				pkg_adduser(pkg, ucl_object_tostring(cur));
			else if (cur->type == UCL_OBJECT)
				pkg_object(pkg, cur, attr);
			else
				pkg_emit_error("Skipping malformed license");
			break;
		case PKG_GROUPS:
			if (cur->type == UCL_STRING)
				pkg_addgroup(pkg, ucl_object_tostring(cur));
			else if (cur->type == UCL_OBJECT)
				pkg_object(pkg, cur, attr);
			else
				pkg_emit_error("Skipping malformed license");
			break;
		case PKG_DIRS:
			if (cur->type == UCL_STRING)
				pkg_adddir(pkg, ucl_object_tostring(cur), 1, false);
			else if (cur->type == UCL_OBJECT)
				pkg_object(pkg, cur, attr);
			else
				pkg_emit_error("Skipping malformed dirs");
			break;
		case PKG_SHLIBS_REQUIRED:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed required shared library");
			else
				pkg_addshlib_required(pkg, ucl_object_tostring(cur));
			break;
		case PKG_SHLIBS_PROVIDED:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed provided shared library");
			else
				pkg_addshlib_provided(pkg, ucl_object_tostring(cur));
			break;
		}
	}

	return (EPKG_OK);
}

static int
pkg_object(struct pkg *pkg, ucl_object_t *obj, int attr)
{
	struct sbuf *tmp = NULL;
	ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	pkg_script script_type;
	const char *key, *buf;
	size_t len;

	pkg_debug(3, "%s", "Manifest: parsing object");
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		switch (attr) {
		case PKG_DEPS:
			if (cur->type != UCL_OBJECT && cur->type != UCL_ARRAY)
				pkg_emit_error("Skipping malformed dependency %s",
				    key);
			else
				pkg_set_deps_from_object(pkg, cur);
			break;
		case PKG_DIRS:
			if (cur->type != UCL_OBJECT)
				pkg_emit_error("Skipping malformed dirs %s",
				    key);
			else
				pkg_set_dirs_from_object(pkg, cur);
			break;
		case PKG_USERS:
			if (cur->type == UCL_STRING)
				pkg_adduid(pkg, key, ucl_object_tostring(cur));
			else
				pkg_emit_error("Skipping malformed users %s",
				    key);
			break;
		case PKG_GROUPS:
			if (cur->type == UCL_STRING)
				pkg_addgid(pkg, key, ucl_object_tostring(cur));
			else
				pkg_emit_error("Skipping malformed groups %s",
				    key);
			break;
		case PKG_DIRECTORIES:
			if (cur->type == UCL_BOOLEAN) {
				urldecode(key, &tmp);
				pkg_adddir(pkg, sbuf_data(tmp), ucl_object_toboolean(cur), false);
			} else if (cur->type == UCL_OBJECT) {
				pkg_set_dirs_from_object(pkg, cur);
			} else if (cur->type == UCL_STRING) {
				urldecode(key, &tmp);
				if (ucl_object_tostring(cur)[0] == 'y')
					pkg_adddir(pkg, sbuf_data(tmp), 1, false);
				else
					pkg_adddir(pkg, sbuf_data(tmp), 0, false);
			} else {
				pkg_emit_error("Skipping malformed directories %s",
				    key);
			}
			break;
		case PKG_FILES:
			if (cur->type == UCL_STRING) {
				buf = ucl_object_tolstring(cur, &len);
				urldecode(key, &tmp);
				pkg_addfile(pkg, sbuf_get(tmp), len == 64 ? buf : NULL, false);
			} else if (cur->type == UCL_OBJECT)
				pkg_set_files_from_object(pkg, cur);
			else
				pkg_emit_error("Skipping malformed files %s",
				   key);
			break;
		case PKG_OPTIONS:
			if (cur->type != UCL_STRING && cur->type != UCL_BOOLEAN)
				pkg_emit_error("Skipping malformed option %s",
				    key);
			else
				pkg_addoption(pkg, key, ucl_object_tostring_forced(cur));
			break;
		case PKG_OPTION_DEFAULTS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed option default %s",
				    key);
			else
				pkg_addoption_default(pkg, key,
				    ucl_object_tostring(cur));
			break;
		case PKG_OPTION_DESCRIPTIONS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed option description %s",
				    key);
			else
				pkg_addoption_description(pkg, key,
				    ucl_object_tostring(cur));
			break;
		case PKG_SCRIPTS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed scripts %s",
				    key);
			else {
				script_type = script_type_str(key);
				if (script_type == PKG_SCRIPT_UNKNOWN) {
					pkg_emit_error("Skipping unknown script "
					    "type: %s", key);
					break;
				}

				urldecode(ucl_object_tostring(cur), &tmp);
				pkg_addscript(pkg, sbuf_data(tmp), script_type);
			}
			break;
		case PKG_ANNOTATIONS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed annotation %s",
				    key);
			else
				pkg_addannotation(pkg, key, ucl_object_tostring(cur));
			break;
		}
	}

	sbuf_free(tmp);

	return (EPKG_OK);
}

static int
pkg_set_files_from_object(struct pkg *pkg, ucl_object_t *obj)
{
	ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *sum = NULL;
	const char *uname = NULL;
	const char *gname = NULL;
	void *set = NULL;
	mode_t perm = 0;
	struct sbuf *fname = NULL;
	const char *key;

	urldecode(ucl_object_key(obj), &fname);
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (!strcasecmp(key, "uname") && cur->type == UCL_STRING)
			uname = ucl_object_tostring(cur);
		else if (!strcasecmp(key, "gname") && cur->type == UCL_STRING)
			gname = ucl_object_tostring(cur);
		else if (!strcasecmp(key, "sum") && cur->type == UCL_STRING &&
		    strlen(ucl_object_tostring(cur)) == 64)
			sum = ucl_object_tostring(cur);
		else if (!strcasecmp(key, "perm") &&
		    (cur->type == UCL_STRING || cur->type == UCL_INT)) {
			if ((set = setmode(ucl_object_tostring_forced(cur))) == NULL)
				pkg_emit_error("Not a valid mode: %s",
				    ucl_object_tostring(cur));
			else
				perm = getmode(set, 0);
		} else {
			pkg_emit_error("Skipping unknown key for file(%s): %s",
			    sbuf_data(fname), ucl_object_tostring(cur));
		}
	}

	pkg_addfile_attr(pkg, sbuf_data(fname), sum, uname, gname, perm, false);
	sbuf_delete(fname);

	return (EPKG_OK);
}

static int
pkg_set_dirs_from_object(struct pkg *pkg, ucl_object_t *obj)
{
	ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *uname = NULL;
	const char *gname = NULL;
	void *set;
	mode_t perm = 0;
	bool try = false;
	struct sbuf *dirname = NULL;
	const char *key;

	urldecode(ucl_object_key(obj), &dirname);
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (!strcasecmp(key, "uname") && cur->type == UCL_STRING)
			uname = ucl_object_tostring(cur);
		else if (!strcasecmp(key, "gname") && cur->type == UCL_STRING)
			gname = ucl_object_tostring(cur);
		else if (!strcasecmp(key, "perm") &&
		    (cur->type == UCL_STRING || cur->type == UCL_INT)) {
			if ((set = setmode(ucl_object_tostring_forced(cur))) == NULL)
				pkg_emit_error("Not a valid mode: %s",
				    ucl_object_tostring(cur));
			else
				perm = getmode(set, 0);
		} else if (!strcasecmp(key, "try") && cur->type == UCL_BOOLEAN) {
				try = ucl_object_toint(cur);
		} else {
			pkg_emit_error("Skipping unknown key for dir(%s): %s",
			    sbuf_data(dirname), key);
		}
	}

	pkg_adddir_attr(pkg, sbuf_data(dirname), uname, gname, perm, try, false);
	sbuf_delete(dirname);

	return (EPKG_OK);
}

static int
pkg_set_deps_from_object(struct pkg *pkg, ucl_object_t *obj)
{
	ucl_object_t *cur, *self;
	ucl_object_iter_t it = NULL, it2;
	const char *origin = NULL;
	const char *version = NULL;
	const char *key;
	int64_t vint = 0;
	char vinteger[BUFSIZ];

	pkg_debug(2, "Found %s", ucl_object_key(obj));
	while ((self = ucl_iterate_object(obj, &it, (obj->type == UCL_ARRAY)))) {
		it2 = NULL;
		while ((cur = ucl_iterate_object(self, &it2, true))) {
			key = ucl_object_key(cur);
			if (cur->type != UCL_STRING) {
				/* accept version to be an integer */
				if (cur->type == UCL_INT && strcasecmp(key, "version") == 0) {
					vint = ucl_object_toint(cur);
					snprintf(vinteger, sizeof(vinteger), "%"PRId64, vint);
					continue;
				}

				pkg_emit_error("Skipping malformed dependency entry "
						"for %s", ucl_object_key(obj));
				continue;
			}
			if (strcasecmp(key, "origin") == 0)
				origin = ucl_object_tostring(cur);
			if (strcasecmp(key, "version") == 0)
				version = ucl_object_tostring(cur);
		}
		if (origin != NULL && (version != NULL || vint > 0))
			pkg_adddep(pkg, ucl_object_key(obj), origin, vint > 0 ? vinteger : version, false);
		else
			pkg_emit_error("Skipping malformed dependency %s", ucl_object_key(obj));
	}

	return (EPKG_OK);
}

static int
parse_manifest(struct pkg *pkg, struct pkg_manifest_key *keys, ucl_object_t *obj)
{
	ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	struct pkg_manifest_key *selected_key;
	struct dataparser *dp;

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		pkg_debug(2, "Manifest: found key: '%s'", ucl_object_key(cur));
		HASH_FIND_STR(keys, ucl_object_key(cur), selected_key);
		if (selected_key != NULL) {
			HASH_FIND_UCLT(selected_key->parser, &cur->type, dp);
			if (dp != NULL) {
				pkg_debug(2, "Manifest: key is valid");
				dp->parse_data(pkg, cur, selected_key->type);
			}
		}
	}

	return (EPKG_OK);
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf, size_t len, struct pkg_manifest_key *keys)
{
	struct ucl_parser *p = NULL;
	ucl_object_t *obj = NULL, *cur;
	ucl_object_iter_t it = NULL;
	int rc;
	struct pkg_manifest_key *sk;
	struct dataparser *dp;
	bool fallback = false;

	assert(pkg != NULL);
	assert(buf != NULL);

	pkg_debug(2, "%s", "Parsing manifest from buffer");

	p = ucl_parser_new(0);
	if (!ucl_parser_add_chunk(p, buf, len))
		fallback = true;

	if (!fallback) {
		obj = ucl_parser_get_object(p);
		if (obj != NULL) {
			while ((cur = ucl_iterate_object(obj, &it, true))) {
				HASH_FIND_STR(keys, ucl_object_key(cur), sk);
				if (sk != NULL) {
					HASH_FIND_UCLT(sk->parser, &cur->type, dp);
					if (dp == NULL) {
						fallback = true;
						break;
					}
				}
			}
		} else {
			fallback = true;
		}
	}

	if (fallback) {
		pkg_debug(2, "Falling back on yaml");
		ucl_parser_free(p);
		p = NULL;
		if (obj != NULL)
			ucl_object_free(obj);
		obj = yaml_to_ucl(NULL, buf, len);
		if (obj == NULL)
			return (EPKG_FATAL);
	}

	rc = parse_manifest(pkg, keys, obj);

	ucl_object_free(obj);
	if (p != NULL)
		ucl_parser_free(p);

	return (rc);
}

int
pkg_parse_manifest_file(struct pkg *pkg, const char *file, struct pkg_manifest_key *keys)
{
	struct ucl_parser *p = NULL;
	ucl_object_t *obj = NULL, *cur;
	ucl_object_iter_t it = NULL;
	int rc;
	bool fallback = false;
	struct pkg_manifest_key *sk;
	struct dataparser *dp;

	assert(pkg != NULL);
	assert(file != NULL);

	pkg_debug(1, "Parsing manifest from '%s'", file);

	errno = 0;
	p = ucl_parser_new(0);
	if (ucl_parser_add_file(p, file)) {
		if (errno == ENOENT) {
			ucl_parser_free(p);
			return (EPKG_FATAL);
		}
		fallback = true;
	}

	if (!fallback) {
		obj = ucl_parser_get_object(p);
		if (obj != NULL) {
			while ((cur = ucl_iterate_object(obj, &it, true))) {
				HASH_FIND_STR(keys, ucl_object_key(cur), sk);
				if (sk != NULL) {
					HASH_FIND_UCLT(sk->parser, &cur->type, dp);
					if (dp == NULL) {
						fallback = true;
						break;
					}
				}
			}

		} else {
			fallback = true;
		}
	}

	if (fallback) {
		pkg_debug(2, "Falling back on yaml");
		ucl_parser_free(p);
		p = NULL;
		if (obj != NULL)
			ucl_object_free(obj);
		obj = yaml_to_ucl(file, NULL, 0);
		if (obj == NULL)
			return (EPKG_FATAL);
	}

	rc = parse_manifest(pkg, keys, obj);

	if (p != NULL)
		ucl_parser_free(p);
	ucl_object_free(obj);

	return (rc);
}

int
pkg_emit_filelist(struct pkg *pkg, FILE *f)
{
	ucl_object_t *obj = NULL, *seq;
	struct pkg_file *file = NULL;
	char *output;
	const char *name, *origin, *version;
	struct sbuf *b = NULL;

	pkg_get(pkg, PKG_NAME, &name, PKG_ORIGIN, &origin, PKG_VERSION, &version);
	obj = ucl_object_insert_key(obj, ucl_object_fromstring(origin), "origin", 6, false);
	obj = ucl_object_insert_key(obj, ucl_object_fromstring(name), "name", 4, false);
	obj = ucl_object_insert_key(obj, ucl_object_fromstring(version), "version", 7, false);

	seq = NULL;
	while (pkg_files(pkg, &file) == EPKG_OK) {
		urlencode(pkg_file_path(file), &b);
		seq = ucl_array_append(seq, ucl_object_fromlstring(sbuf_data(b), sbuf_len(b)));
	}
	obj = ucl_object_insert_key(obj, seq, "files", 5, false);

	output = ucl_object_emit(obj, UCL_EMIT_JSON_COMPACT);
	fprintf(f, "%s", output);
	free(output);

	if (b != NULL)
		sbuf_delete(b);

	ucl_object_free(obj);

	return (EPKG_OK);
}

static int
emit_manifest(struct pkg *pkg, char **out, short flags)
{
	struct pkg_dep		*dep      = NULL;
	struct pkg_option	*option   = NULL;
	struct pkg_file		*file     = NULL;
	struct pkg_dir		*dir      = NULL;
	struct pkg_category	*category = NULL;
	struct pkg_license	*license  = NULL;
	struct pkg_user		*user     = NULL;
	struct pkg_group	*group    = NULL;
	struct pkg_shlib	*shlib    = NULL;
	struct pkg_note		*note     = NULL;
	struct sbuf		*tmpsbuf  = NULL;
	int i;
	const char *comment, *desc, *message, *name, *pkgarch;
	const char *pkgmaintainer, *pkgorigin, *prefix, *version, *www;
	const char *repopath, *pkgsum;
	const char *script_types = NULL;
	lic_t licenselogic;
	int64_t flatsize, pkgsize;
	ucl_object_t *obj, *map, *seq, *submap;
	ucl_object_t *top = NULL;

	pkg_get(pkg, PKG_NAME, &name, PKG_ORIGIN, &pkgorigin,
	    PKG_COMMENT, &comment, PKG_ARCH, &pkgarch, PKG_WWW, &www,
	    PKG_MAINTAINER, &pkgmaintainer, PKG_PREFIX, &prefix,
	    PKG_LICENSE_LOGIC, &licenselogic, PKG_DESC, &desc,
	    PKG_FLATSIZE, &flatsize, PKG_MESSAGE, &message,
	    PKG_VERSION, &version, PKG_REPOPATH, &repopath,
	    PKG_CKSUM, &pkgsum, PKG_PKGSIZE, &pkgsize);

	pkg_debug(1, "Emitting basic metadata");
	top = ucl_object_insert_key(top, ucl_object_fromstring(name), "name", 4, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(pkgorigin), "origin", 6, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(version), "version", 7, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(comment), "comment", 7, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(pkgarch), "arch", 4, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(pkgmaintainer), "maintainer", 10, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(prefix), "prefix", 6, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(www), "www", 3, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(repopath), "path", 4, false);
	obj = ucl_object_insert_key(top, ucl_object_fromstring(pkgsum), "sum", 3, false);

	switch (licenselogic) {
	case LICENSE_SINGLE:
		obj = ucl_object_insert_key(top, ucl_object_fromlstring("single", 6), "licenselogic", 12, false);
		break;
	case LICENSE_AND:
		obj = ucl_object_insert_key(top, ucl_object_fromlstring("and", 3), "licenselogic", 12, false);
		break;
	case LICENSE_OR:
		obj = ucl_object_insert_key(top, ucl_object_fromlstring("or", 2), "licenselogic", 12, false);
		break;
	}

	pkg_debug(1, "Emitting licenses");
	seq = NULL;
	while (pkg_licenses(pkg, &license) == EPKG_OK)
		seq = ucl_array_append(seq, ucl_object_fromstring(pkg_license_name(license)));
	obj = ucl_object_insert_key(top, seq, "licenses", 8, false);

	obj = ucl_object_insert_key(top, ucl_object_fromint(flatsize), "flatsize", 8, false);
	if (pkgsize > 0)
		obj = ucl_object_insert_key(top, ucl_object_fromint(pkgsize), "pkgsize", 7, false);

	urlencode(desc, &tmpsbuf);
	obj = ucl_object_insert_key(top, ucl_object_fromlstring(sbuf_data(tmpsbuf), sbuf_len(tmpsbuf)), "desc", 4, false);

	pkg_debug(1, "Emitting deps");
	map = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		submap = NULL;
		submap = ucl_object_insert_key(submap, ucl_object_fromstring(pkg_dep_origin(dep)), "origin", 6, false);
		submap = ucl_object_insert_key(submap, ucl_object_fromstring(pkg_dep_version(dep)), "version", 7, false);
		map = ucl_object_insert_key(map, submap, pkg_dep_name(dep), 0, false);
	}
	obj = ucl_object_insert_key(top, map, "deps", 4, false);

	pkg_debug(1, "Emitting categories");
	seq = NULL;
	while (pkg_categories(pkg, &category) == EPKG_OK)
		seq = ucl_array_append(seq, ucl_object_fromstring(pkg_category_name(category)));
	obj = ucl_object_insert_key(top, seq, "categories", 10, false);

	pkg_debug(1, "Emitting users");
	seq = NULL;
	while (pkg_users(pkg, &user) == EPKG_OK)
		seq = ucl_array_append(seq, ucl_object_fromstring(pkg_user_name(user)));
	obj = ucl_object_insert_key(top, seq, "users", 5, false);

	pkg_debug(1, "Emitting groups");
	seq = NULL;
	while (pkg_groups(pkg, &group) == EPKG_OK) 
		seq = ucl_array_append(seq, ucl_object_fromstring(pkg_group_name(group)));
	obj = ucl_object_insert_key(top, seq, "groups", 6, false);

	pkg_debug(1, "Emitting required");
	seq = NULL;
	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK)
		seq = ucl_array_append(seq, ucl_object_fromstring(pkg_shlib_name(shlib)));
	obj = ucl_object_insert_key(top, seq, "shlibs_required", 15, false);

	pkg_debug(1, "Emitting shlibs_provided");
	seq = NULL;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK)
		seq = ucl_array_append(seq, ucl_object_fromstring(pkg_shlib_name(shlib)));
	obj = ucl_object_insert_key(top, seq, "shlibs_provided", 15, false);

	pkg_debug(1, "Emitting options");
	map = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		pkg_debug(2, "Emiting option: %s", pkg_option_value(option));
		map = ucl_object_insert_key(map,
		    ucl_object_fromstring(pkg_option_value(option)),
		    pkg_option_opt(option), 0, false);
	}
	obj = ucl_object_insert_key(top, map, "options", 7, false);

	pkg_debug(1, "Emitting annotations");
	map = NULL;
	while (pkg_annotations(pkg, &note) == EPKG_OK) {
		map = ucl_object_insert_key(map,
		    ucl_object_fromstring(pkg_annotation_value(note)),
		    pkg_annotation_tag(note), 0, false);
	}
	obj = ucl_object_insert_key(top, map, "annotations", 11, false);

	if ((flags & PKG_MANIFEST_EMIT_COMPACT) == 0) {
		if ((flags & PKG_MANIFEST_EMIT_NOFILES) == 0) {
			pkg_debug(1, "Emitting files");
			map = NULL;
			while (pkg_files(pkg, &file) == EPKG_OK) {
				const char *pkg_sum = pkg_file_cksum(file);

				if (pkg_sum == NULL || pkg_sum[0] == '\0')
					pkg_sum = "-";

				urlencode(pkg_file_path(file), &tmpsbuf);
				map = ucl_object_insert_key(map,
				    ucl_object_fromstring(pkg_sum),
				    sbuf_data(tmpsbuf), sbuf_len(tmpsbuf), true);
			}
			obj = ucl_object_insert_key(top, map, "files", 5, false);

			pkg_debug(1, "Emitting directories");
			map = NULL;
			while (pkg_dirs(pkg, &dir) == EPKG_OK) {
				urlencode(pkg_dir_path(dir), &tmpsbuf);
				/* For now append y/n to stay compatible with libyaml version 
				 * obj_append_boolean(map, sbuf_get(tmpsbuf), pkg_dir_try(dir));
				 */
				map = ucl_object_insert_key(map,
				    ucl_object_fromstring(pkg_dir_try(dir) ? "y" : "n"),
				    sbuf_data(tmpsbuf), sbuf_len(tmpsbuf), true);
			}
			obj = ucl_object_insert_key(top, map, "directories", 11, false);
		}

		pkg_debug(1, "Emitting scripts");
		map = NULL;
		for (i = 0; i < PKG_NUM_SCRIPTS; i++) {
			if (pkg_script_get(pkg, i) == NULL)
				continue;

			switch (i) {
			case PKG_SCRIPT_PRE_INSTALL:
				script_types = "pre-install";
				break;
			case PKG_SCRIPT_INSTALL:
				script_types = "install";
				break;
			case PKG_SCRIPT_POST_INSTALL:
				script_types = "post-install";
				break;
			case PKG_SCRIPT_PRE_UPGRADE:
				script_types = "pre-upgrade";
				break;
			case PKG_SCRIPT_UPGRADE:
				script_types = "upgrade";
				break;
			case PKG_SCRIPT_POST_UPGRADE:
				script_types = "post-upgrade";
				break;
			case PKG_SCRIPT_PRE_DEINSTALL:
				script_types = "pre-deinstall";
				break;
			case PKG_SCRIPT_DEINSTALL:
				script_types = "deinstall";
				break;
			case PKG_SCRIPT_POST_DEINSTALL:
				script_types = "post-deinstall";
				break;
			}
			urlencode(pkg_script_get(pkg, i), &tmpsbuf);
			map = ucl_object_insert_key(map,
			    ucl_object_fromstring_common(sbuf_data(tmpsbuf),
			        sbuf_len(tmpsbuf), UCL_STRING_TRIM),
			    script_types, 0, true);
		}
		obj = ucl_object_insert_key(top, map, "scripts", 7, false);
	}

	pkg_debug(1, "Emitting message");
	if (message != NULL && *message != '\0') {
		urlencode(message, &tmpsbuf);
		obj = ucl_object_insert_key(top,
		    ucl_object_fromstring_common(sbuf_data(tmpsbuf), sbuf_len(tmpsbuf), UCL_STRING_TRIM),
		    "message", 7, false);
	}

	if ((flags & PKG_MANIFEST_EMIT_PRETTY) == PKG_MANIFEST_EMIT_PRETTY)
		*out = ucl_object_emit(top, UCL_EMIT_YAML);
	else
		*out = ucl_object_emit(top, UCL_EMIT_JSON_COMPACT);

	ucl_object_free(top);

	/* FIXME: avoid gcc to complain about -Werror=unused-but-set-variable */
	(void)obj;

	return (EPKG_OK);
}

static void
pkg_emit_manifest_digest(const unsigned char *digest, size_t len, char *hexdigest)
{
	unsigned int i;

	for (i = 0; i < len; i ++)
		sprintf(hexdigest + (i * 2), "%02x", digest[i]);

	hexdigest[len * 2] = '\0';
}

/*
 * This routine is able to output to either a (FILE *) or a (struct sbuf *). It
 * exist only to avoid code duplication and should not be called except from
 * pkg_emit_manifest_file() and pkg_emit_manifest_sbuf().
 */
static int
pkg_emit_manifest_generic(struct pkg *pkg, void *out, short flags,
	    char **pdigest, bool out_is_a_sbuf)
{
	char *output;
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256_CTX *sign_ctx = NULL;
	int rc;

	if (pdigest != NULL) {
		*pdigest = malloc(sizeof(digest) * 2 + 1);
		sign_ctx = malloc(sizeof(SHA256_CTX));
		SHA256_Init(sign_ctx);
	}

	rc = emit_manifest(pkg, &output, flags);

	if (sign_ctx != NULL)
		SHA256_Update(sign_ctx, output, strlen(output));

	if (out_is_a_sbuf)
		sbuf_cat(out, output);
	else
		fprintf(out, "%s\n", output);

	if (pdigest != NULL) {
		SHA256_Final(digest, sign_ctx);
		pkg_emit_manifest_digest(digest, sizeof(digest), *pdigest);
		free(sign_ctx);
	}

	free (output);

	return (rc);
}

int
pkg_emit_manifest_file(struct pkg *pkg, FILE *f, short flags, char **pdigest)
{

	return (pkg_emit_manifest_generic(pkg, f, flags, pdigest, false));
}

int
pkg_emit_manifest_sbuf(struct pkg *pkg, struct sbuf *b, short flags, char **pdigest)
{

	return (pkg_emit_manifest_generic(pkg, b, flags, pdigest, true));
}

int
pkg_emit_manifest(struct pkg *pkg, char **dest, short flags, char **pdigest)
{
	struct sbuf *b = sbuf_new_auto();
	int rc;

	rc = pkg_emit_manifest_sbuf(pkg, b, flags, pdigest);

	if (rc != EPKG_OK) {
		sbuf_delete(b);
		return (rc);
	}

	sbuf_finish(b);
	*dest = strdup(sbuf_get(b));
	sbuf_delete(b);

	return (rc);
}

