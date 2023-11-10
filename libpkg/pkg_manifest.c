/*-
 * Copyright (c) 2011-2015 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2013-2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
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
#include <stddef.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ucl.h>

#include "sha256.h"
#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

enum {
	MANIFEST_ANNOTATIONS,
	MANIFEST_CATEGORIES,
	MANIFEST_CONFIG_FILES,
	MANIFEST_CONFLICTS,
	MANIFEST_DEPS,
	MANIFEST_DIRECTORIES,
	MANIFEST_DIRS,
	MANIFEST_FILES,
	MANIFEST_GROUPS,
	MANIFEST_LICENSES,
	MANIFEST_LUA_SCRIPTS,
	MANIFEST_OPTIONS,
	MANIFEST_OPTION_DEFAULTS,
	MANIFEST_OPTION_DESCRIPTIONS,
	MANIFEST_PROVIDES,
	MANIFEST_REQUIRES,
	MANIFEST_SCRIPTS,
	MANIFEST_SHLIBS_PROVIDED,
	MANIFEST_SHLIBS_REQUIRED,
	MANIFEST_USERS,
};

#define PKG_MESSAGE_LEGACY	1
#define PKG_MESSAGE_NEW 2

static int pkg_string(struct pkg *, const ucl_object_t *, uint32_t);
static int pkg_obj(struct pkg *, const ucl_object_t *, uint32_t);
static int pkg_array(struct pkg *, const ucl_object_t *, uint32_t);
static int pkg_int(struct pkg *, const ucl_object_t *, uint32_t);
static int pkg_boolean(struct pkg *, const ucl_object_t *, uint32_t);
static int pkg_message(struct pkg *, const ucl_object_t *, uint32_t);
static int pkg_set_deps_from_object(struct pkg *, const ucl_object_t *);
static int pkg_set_files_from_object(struct pkg *, const ucl_object_t *);
static int pkg_set_dirs_from_object(struct pkg *, const ucl_object_t *);

/*
 * Keep sorted
 */
#define TYPE_SHIFT(x) (1 << (x))
#define STRING_FLAG_LICENSE (1U << 31)
#define STRING_FLAG_URLDECODE (1U << 30)
#define STRING_FLAG_MASK ~(STRING_FLAG_LICENSE|STRING_FLAG_URLDECODE)

static struct pkg_manifest_key {
	const char *key;
	uint32_t type;
	uint16_t valid_type;
	int (*parse_data)(struct pkg *, const ucl_object_t *, uint32_t);
} manifest_keys[] = {
	{ "annotations",         MANIFEST_ANNOTATIONS,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "abi",                 offsetof(struct pkg, abi),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "arch",                offsetof(struct pkg, arch),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "categories",          MANIFEST_CATEGORIES,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "comment",             offsetof(struct pkg, comment),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "conflicts",           MANIFEST_CONFLICTS,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "config",              MANIFEST_CONFIG_FILES,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "dep_formula",         offsetof(struct pkg, dep_formula),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "deps",                MANIFEST_DEPS,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "desc",                offsetof(struct pkg, desc) | STRING_FLAG_URLDECODE,
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "directories",         MANIFEST_DIRECTORIES,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "dirs",                MANIFEST_DIRS,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "files",               MANIFEST_FILES,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "flatsize",            offsetof(struct pkg, flatsize),
			TYPE_SHIFT(UCL_INT),    pkg_int},

	{ "groups",              MANIFEST_GROUPS,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "licenselogic",        offsetof(struct pkg, licenselogic) | STRING_FLAG_LICENSE,
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "licenses",            MANIFEST_LICENSES,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "lua_scripts",         MANIFEST_LUA_SCRIPTS,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "maintainer",          offsetof(struct pkg, maintainer),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "messages",            PKG_MESSAGE_NEW,
			TYPE_SHIFT(UCL_STRING)|TYPE_SHIFT(UCL_ARRAY), pkg_message},

	{ "message",             PKG_MESSAGE_LEGACY,
			TYPE_SHIFT(UCL_STRING)|TYPE_SHIFT(UCL_ARRAY), pkg_message},

	{ "name",                offsetof(struct pkg, name),
			TYPE_SHIFT(UCL_STRING)|TYPE_SHIFT(UCL_INT), pkg_string},

	{ "options",             MANIFEST_OPTIONS,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "option_defaults",     MANIFEST_OPTION_DEFAULTS,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "option_descriptions", MANIFEST_OPTION_DESCRIPTIONS,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "origin",              offsetof(struct pkg, origin),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "path",                offsetof(struct pkg, repopath),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "repopath",            offsetof(struct pkg, repopath),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "pkgsize",             offsetof(struct pkg, pkgsize),
			TYPE_SHIFT(UCL_INT),    pkg_int},

	{ "prefix",              offsetof(struct pkg, prefix),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "provides",            MANIFEST_PROVIDES,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "requires",            MANIFEST_REQUIRES,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "scripts",             MANIFEST_SCRIPTS,
			TYPE_SHIFT(UCL_OBJECT), pkg_obj},

	{ "shlibs",              MANIFEST_SHLIBS_REQUIRED,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array}, /* Backwards compat with 1.0.x packages */

	{ "shlibs_provided",     MANIFEST_SHLIBS_PROVIDED,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "shlibs_required",     MANIFEST_SHLIBS_REQUIRED,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "sum",                 offsetof(struct pkg, sum),
			TYPE_SHIFT(UCL_STRING), pkg_string},

	{ "users",               MANIFEST_USERS,
			TYPE_SHIFT(UCL_ARRAY),  pkg_array},

	{ "version",             offsetof(struct pkg, version),
			TYPE_SHIFT(UCL_STRING)|TYPE_SHIFT(UCL_INT), pkg_string},

	{ "vital",            offsetof(struct pkg, vital),
			TYPE_SHIFT(UCL_BOOLEAN),    pkg_boolean},

	{ "www",                 offsetof(struct pkg, www),
			TYPE_SHIFT(UCL_STRING), pkg_string},

};

static int
urlencode(const char *src, xstring **dest)
{
	size_t len;
	size_t i;

	xstring_renew(*dest);

	len = strlen(src);
	for (i = 0; i < len; i++) {
		if (!isascii(src[i]) || src[i] == '%')
			fprintf((*dest)->fp, "%%%.2x", (unsigned char)src[i]);
		else
			fputc(src[i], (*dest)->fp);
	}

	fflush((*dest)->fp);
	return (EPKG_OK);
}

static int
urldecode(const char *src, xstring **dest)
{
	size_t len;
	size_t i;
	char c;
	char hex[] = {'\0', '\0', '\0'};

	xstring_renew(*dest);

	len = strlen(src);
	for (i = 0; i < len; i++) {
		if (src[i] != '%') {
			fputc(src[i], (*dest)->fp);
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
				fprintf((*dest)->fp, "%%%s", hex);
			} else {
				fputc(c,(*dest)->fp);
			}
		}
	}

	fflush((*dest)->fp);
	return (EPKG_OK);
}

static int
lua_script_type_str(const char *str)
{
	if (strcmp(str, "pre-install") == 0)
		return (PKG_LUA_PRE_INSTALL);
	if (strcmp(str, "post-install") == 0)
		return (PKG_LUA_POST_INSTALL);
	if (strcmp(str, "pre-deinstall") == 0)
		return (PKG_LUA_PRE_DEINSTALL);
	if (strcmp(str, "post-deinstall") == 0)
		return (PKG_LUA_POST_DEINSTALL);
	return (PKG_LUA_UNKNOWN);
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
	if (strcmp(str, "pre-deinstall") == 0)
		return (PKG_SCRIPT_PRE_DEINSTALL);
	if (strcmp(str, "deinstall") == 0)
		return (PKG_SCRIPT_DEINSTALL);
	if (strcmp(str, "post-deinstall") == 0)
		return (PKG_SCRIPT_POST_DEINSTALL);
	return (PKG_SCRIPT_UNKNOWN);
}

static int
pkg_string(struct pkg *pkg, const ucl_object_t *obj, uint32_t offset)
{
	const char *str;
	char **dest;
	xstring *buf = NULL;

	str = ucl_object_tostring_forced(obj);

	if (offset & STRING_FLAG_LICENSE) {
		if (!strcmp(str, "single"))
			pkg->licenselogic = LICENSE_SINGLE;
		else if (!strcmp(str, "or") ||
				!strcmp(str, "dual"))
			pkg->licenselogic = LICENSE_OR;
		else if (!strcmp(str, "and") ||
				!strcmp(str, "multi"))
			pkg->licenselogic = LICENSE_AND;
		else {
			pkg_emit_error("Unknown license logic: %s", str);
			return (EPKG_FATAL);
		}
	}
	else {

		if (offset & STRING_FLAG_URLDECODE) {
			urldecode(str, &buf);
			str = buf->buf;
		}

		/* Remove flags from the offset */
		offset &= STRING_FLAG_MASK;
		dest = (char **) ((unsigned char *)pkg + offset);
		*dest = xstrdup(str);

		xstring_free(buf);
	}

	return (EPKG_OK);
}

static int
pkg_int(struct pkg *pkg, const ucl_object_t *obj, uint32_t offset)
{
	int64_t *dest;

	dest = (int64_t *)((unsigned char *)pkg + offset);
	*dest = ucl_object_toint(obj);

	return (EPKG_OK);
}

static int
pkg_boolean(struct pkg *pkg, const ucl_object_t *obj, uint32_t offset)
{
	bool *dest;

	dest = (bool *)((unsigned char *)pkg + offset);
	*dest = ucl_object_toboolean(obj);

	return (EPKG_OK);
}

static int
pkg_array(struct pkg *pkg, const ucl_object_t *obj, uint32_t attr)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	int ret;

	pkg_debug(3, "%s", "Manifest: parsing array");
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		switch (attr) {
		case MANIFEST_CATEGORIES:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed category");
			else
				pkg_addstring(&pkg->categories,
				    ucl_object_tostring(cur), "category");
			break;
		case MANIFEST_LICENSES:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed license");
			else
				pkg_addstring(&pkg->licenses,
				    ucl_object_tostring(cur), "license");
			break;
		case MANIFEST_USERS:
			if (cur->type == UCL_STRING)
				pkg_adduser(pkg, ucl_object_tostring(cur));
			else if (cur->type == UCL_OBJECT)
				pkg_obj(pkg, cur, attr);
			else
				pkg_emit_error("Skipping malformed license");
			break;
		case MANIFEST_GROUPS:
			if (cur->type == UCL_STRING)
				pkg_addgroup(pkg, ucl_object_tostring(cur));
			else if (cur->type == UCL_OBJECT)
				pkg_obj(pkg, cur, attr);
			else
				pkg_emit_error("Skipping malformed license");
			break;
		case MANIFEST_DIRS:
			if (cur->type == UCL_STRING)
				pkg_adddir(pkg, ucl_object_tostring(cur), false);
			else if (cur->type == UCL_OBJECT)
				pkg_obj(pkg, cur, attr);
			else
				pkg_emit_error("Skipping malformed dirs");
			break;
		case MANIFEST_SHLIBS_REQUIRED:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed required shared library");
			else
				pkg_addshlib_required(pkg, ucl_object_tostring(cur));
			break;
		case MANIFEST_SHLIBS_PROVIDED:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed provided shared library");
			else
				pkg_addshlib_provided(pkg, ucl_object_tostring(cur));
			break;
		case MANIFEST_CONFLICTS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed conflict name");
			else
				pkg_addconflict(pkg, ucl_object_tostring(cur));
			break;
		case MANIFEST_PROVIDES:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed provide name");
			else
				pkg_addprovide(pkg, ucl_object_tostring(cur));
			break;
		case MANIFEST_CONFIG_FILES:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed config file name");
			else {
				ret = pkg_addconfig_file(pkg, ucl_object_tostring(cur), NULL);
				if (ret != EPKG_OK)
					return (ret);
			}
			break;
		case MANIFEST_REQUIRES:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed require name");
			else
				pkg_addrequire(pkg, ucl_object_tostring(cur));
			break;
		}
	}

	return (EPKG_OK);
}

static int
pkg_obj(struct pkg *pkg, const ucl_object_t *obj, uint32_t attr)
{
	xstring *tmp = NULL;
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	pkg_script script_type;
	pkg_lua_script lua_script_type;
	const char *key, *buf;
	size_t len;

	pkg_debug(3, "%s", "Manifest: parsing object");
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;
		switch (attr) {
		case MANIFEST_DEPS:
			if (cur->type != UCL_OBJECT && cur->type != UCL_ARRAY)
				pkg_emit_error("Skipping malformed dependency %s",
				    key);
			else
				pkg_set_deps_from_object(pkg, cur);
			break;
		case MANIFEST_DIRS:
			if (cur->type != UCL_OBJECT)
				pkg_emit_error("Skipping malformed dirs %s",
				    key);
			else
				pkg_set_dirs_from_object(pkg, cur);
			break;
		case MANIFEST_DIRECTORIES:
			if (cur->type == UCL_BOOLEAN) {
				urldecode(key, &tmp);
				pkg_adddir(pkg, tmp->buf, false);
			} else if (cur->type == UCL_OBJECT) {
				pkg_set_dirs_from_object(pkg, cur);
			} else if (cur->type == UCL_STRING) {
				urldecode(key, &tmp);
				pkg_adddir(pkg, tmp->buf, false);
			} else {
				pkg_emit_error("Skipping malformed directories %s",
				    key);
			}
			break;
		case MANIFEST_FILES:
			if (cur->type == UCL_STRING) {
				buf = ucl_object_tolstring(cur, &len);
				urldecode(key, &tmp);
				pkg_addfile(pkg, tmp->buf, len >= 2 ? buf : NULL, false);
			} else if (cur->type == UCL_OBJECT)
				pkg_set_files_from_object(pkg, cur);
			else
				pkg_emit_error("Skipping malformed files %s",
				   key);
			break;
		case MANIFEST_OPTIONS:
			if (cur->type != UCL_STRING && cur->type != UCL_BOOLEAN)
				pkg_emit_error("Skipping malformed option %s",
				    key);
			else if (cur->type == UCL_STRING) {
				pkg_addoption(pkg, key, ucl_object_tostring(cur));
			} else {
				pkg_addoption(pkg, key, ucl_object_toboolean(cur) ? "on" : "off");
			}
			break;
		case MANIFEST_OPTION_DEFAULTS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed option default %s",
				    key);
			else
				pkg_addoption_default(pkg, key,
				    ucl_object_tostring(cur));
			break;
		case MANIFEST_OPTION_DESCRIPTIONS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed option description %s",
				    key);
			else
				pkg_addoption_description(pkg, key,
				    ucl_object_tostring(cur));
			break;
		case MANIFEST_SCRIPTS:
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
				pkg_addscript(pkg, tmp->buf, script_type);
			}
			break;
		case MANIFEST_LUA_SCRIPTS:
			if (cur->type != UCL_ARRAY) {
				pkg_emit_error("Skipping malformed dependency %s",
				    key);
				break;
			}
			lua_script_type = lua_script_type_str(key);
			if (lua_script_type == PKG_LUA_UNKNOWN) {
				pkg_emit_error("Skipping unknown script "
				    "type: %s", key);
				break;
			}
			pkg_lua_script_from_ucl(pkg, cur, lua_script_type);
			break;
		case MANIFEST_ANNOTATIONS:
			if (cur->type != UCL_STRING)
				pkg_emit_error("Skipping malformed annotation %s",
				    key);
			else
				pkg_kv_add(&pkg->annotations, key, ucl_object_tostring(cur), "annotation");
			break;
		}
	}

	xstring_free(tmp);

	return (EPKG_OK);
}

static int
pkg_message(struct pkg *pkg, const ucl_object_t *obj, uint32_t attr __unused)
{
	return pkg_message_from_ucl(pkg, obj);
}

static int
pkg_set_files_from_object(struct pkg *pkg, const ucl_object_t *obj)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *sum = NULL;
	const char *uname = NULL;
	const char *gname = NULL;
	void *set = NULL;
	mode_t perm = 0;
	xstring *fname = NULL;
	const char *key, *okey;

	okey = ucl_object_key(obj);
	if (okey == NULL)
		return (EPKG_FATAL);
	urldecode(okey, &fname);
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;
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
			pkg_debug(1, "Skipping unknown key for file(%s): %s",
			    fname->buf, key);
		}
	}

	pkg_addfile_attr(pkg, fname->buf, sum, uname, gname, perm, 0,
	    false);
	xstring_free(fname);

	return (EPKG_OK);
}

static int
pkg_set_dirs_from_object(struct pkg *pkg, const ucl_object_t *obj)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *uname = NULL;
	const char *gname = NULL;
	void *set;
	mode_t perm = 0;
	xstring *dirname = NULL;
	const char *key, *okey;

	okey = ucl_object_key(obj);
	if (okey == NULL)
		return (EPKG_FATAL);
	urldecode(okey, &dirname);
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;
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
			/* ignore on purpose : compatibility*/
		} else {
			pkg_debug(1, "Skipping unknown key for dir(%s): %s",
			    dirname->buf, key);
		}
	}

	pkg_adddir_attr(pkg, dirname->buf, uname, gname, perm, 0, false);
	xstring_free(dirname);

	return (EPKG_OK);
}

static int
pkg_set_deps_from_object(struct pkg *pkg, const ucl_object_t *obj)
{
	const ucl_object_t *cur, *self;
	ucl_object_iter_t it = NULL, it2;
	const char *origin = NULL;
	const char *version = NULL;
	const char *key, *okey;
	bool noversion = false;

	noversion = (getenv("PKG_NO_VERSION_FOR_DEPS") != NULL);
	okey = ucl_object_key(obj);
	if (okey == NULL)
		return (EPKG_FATAL);
	pkg_debug(2, "Found %s", okey);
	while ((self = ucl_iterate_object(obj, &it, (obj->type == UCL_ARRAY)))) {
		it2 = NULL;
		while ((cur = ucl_iterate_object(self, &it2, true))) {
			key = ucl_object_key(cur);
			if (key == NULL)
				continue;
			if (cur->type != UCL_STRING) {
				/* accept version to be an integer */
				if (cur->type == UCL_INT && strcasecmp(key, "version") == 0) {
					if (!noversion)
						version = ucl_object_tostring_forced(cur);
					continue;
				}

				pkg_emit_error("Skipping malformed dependency entry "
						"for %s", okey);
				continue;
			}
			if (strcasecmp(key, "origin") == 0)
				origin = ucl_object_tostring(cur);
			if (strcasecmp(key, "version") == 0 && !noversion)
				version = ucl_object_tostring(cur);
		}
		if (origin != NULL)
			pkg_adddep(pkg, okey, origin, version, false);
		else
			pkg_emit_error("Skipping malformed dependency %s", okey);
	}

	return (EPKG_OK);
}

static struct pkg_manifest_key *
select_manifest_key(const char *key)
{
	for (int i = 0; i < NELEM(manifest_keys); i++)
		if (strcmp(manifest_keys[i].key, key) == 0)
			return (&(manifest_keys[i]));
	return (NULL);
}
static int
parse_manifest(struct pkg *pkg, ucl_object_t *obj)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	struct pkg_manifest_key *selected_key = NULL;
	const char *key;
	int ret = EPKG_OK;

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;
		pkg_debug(3, "Manifest: found key: '%s'", key);
		if ((selected_key = select_manifest_key(key)) == NULL) {
			pkg_debug(1, "Skipping unknown key '%s'", key);
			continue;
		}
		if (TYPE_SHIFT(ucl_object_type(cur)) & selected_key->valid_type) {
			ret = selected_key->parse_data(pkg, cur, selected_key->type);
			if (ret != EPKG_OK)
				return (ret);
		} else {
			pkg_emit_error("Skipping malformed key '%s'", key);
		}
	}

	return (EPKG_OK);
}

int
pkg_parse_manifest_ucl(struct pkg *pkg, ucl_object_t *obj)
{
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	struct pkg_manifest_key *sk = NULL;
	const char *key;

	/* do a minimal validation */
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		if (key == NULL)
			continue;
		if ((sk = select_manifest_key(key)) == NULL)
			continue;
		if (!(sk->valid_type & TYPE_SHIFT(ucl_object_type(cur)))) {
			pkg_emit_error("Bad format in manifest for key:"
				" %s", key);
			return (EPKG_FATAL);
		}
	}

	return (parse_manifest(pkg, obj));
}

int
pkg_parse_manifest(struct pkg *pkg, const char *buf, size_t len)
{
	struct ucl_parser *p = NULL;
	ucl_object_t *obj = NULL;
	int rc;

	assert(pkg != NULL);
	assert(buf != NULL);

	pkg_debug(2, "%s", "Parsing manifest from buffer");

	p = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_chunk(p, buf, len)) {
		pkg_emit_error("Error parsing manifest: %s",
		    ucl_parser_get_error(p));
		ucl_parser_free(p);

		return (EPKG_FATAL);
	}

	if ((obj = ucl_parser_get_object(p)) == NULL) {
		ucl_parser_free(p);
		return (EPKG_FATAL);
	}

	ucl_parser_free(p);
	rc = pkg_parse_manifest_ucl(pkg, obj);
	ucl_object_unref(obj);

	return (rc);
}

int
pkg_parse_manifest_fileat(int dfd, struct pkg *pkg, const char *file)
{
	struct ucl_parser *p = NULL;
	ucl_object_t *obj = NULL;
	int rc;
	char *data;
	off_t sz = 0;

	assert(pkg != NULL);
	assert(file != NULL);

	pkg_debug(1, "Parsing manifest from '%s'", file);

	errno = 0;

	if (file_to_bufferat(dfd, file, &data, &sz) != EPKG_OK)
		return (EPKG_FATAL);

	p = ucl_parser_new(UCL_PARSER_NO_FILEVARS);
	if (!ucl_parser_add_string(p, data, sz)) {
		pkg_emit_error("manifest parsing error: %s", ucl_parser_get_error(p));
		ucl_parser_free(p);
		free(data);
		return (EPKG_FATAL);
	}

	if ((obj = ucl_parser_get_object(p)) == NULL) {
		ucl_parser_free(p);
		free(data);
		return (EPKG_FATAL);
	}
	ucl_parser_free(p);

	rc = pkg_parse_manifest_ucl(pkg, obj);
	ucl_object_unref(obj);
	free(data);

	return (rc);
}

int
pkg_parse_manifest_file(struct pkg *pkg, const char *file)
{
	return pkg_parse_manifest_fileat(AT_FDCWD, pkg, file);
}

int
pkg_emit_filelist(struct pkg *pkg, FILE *f)
{
	ucl_object_t *obj = NULL, *seq;
	struct pkg_file *file = NULL;
	xstring *b = NULL;

	obj = ucl_object_typed_new(UCL_OBJECT);
	ucl_object_insert_key(obj, ucl_object_fromstring(pkg->origin), "origin", 6, false);
	ucl_object_insert_key(obj, ucl_object_fromstring(pkg->name), "name", 4, false);
	ucl_object_insert_key(obj, ucl_object_fromstring(pkg->version), "version", 7, false);

	seq = NULL;
	while (pkg_files(pkg, &file) == EPKG_OK) {
		char dpath[MAXPATHLEN];
		const char *dp = file->path;

		if (pkg->oprefix != NULL) {
			size_t l = strlen(pkg->prefix);
			if (strncmp(file->path, pkg->prefix, l) == 0 &&
			    (file->path[l] == '/' || l == 1)) {
				snprintf(dpath, sizeof(dpath), "%s%s%s",
				    pkg->oprefix, l == 1 ? "/" : "", file->path + l);
				dp = dpath;
			}
		}
		urlencode(dp, &b);
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromlstring(b->buf, strlen(b->buf)));
	}
	if (seq != NULL)
		ucl_object_insert_key(obj, seq, "files", 5, false);

	ucl_object_emit_file(obj, UCL_EMIT_JSON_COMPACT, f);

	xstring_free(b);
	ucl_object_unref(obj);

	return (EPKG_OK);
}

pkg_object*
pkg_emit_object(struct pkg *pkg, short flags)
{
	struct pkg_kv		*kv;
	struct pkg_dep		*dep      = NULL;
	struct pkg_option	*option   = NULL;
	struct pkg_file		*file     = NULL;
	struct pkg_dir		*dir      = NULL;
	struct pkg_conflict	*conflict = NULL;
	struct pkg_config_file	*cf       = NULL;
	xstring		*tmpsbuf  = NULL;
	int i;
	const char *script_types = NULL;
	char legacyarch[BUFSIZ];
	ucl_object_t *map, *seq, *submap;
	ucl_object_t *top = ucl_object_typed_new(UCL_OBJECT);

	if (pkg->abi == NULL && pkg->arch != NULL)
		pkg->abi = xstrdup(pkg->arch);
	pkg_arch_to_legacy(pkg->abi, legacyarch, BUFSIZ);
	pkg->arch = xstrdup(legacyarch);
	pkg_debug(4, "Emitting basic metadata");
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->name, 0,
	    UCL_STRING_TRIM), "name", 4, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->origin, 0,
	    UCL_STRING_TRIM), "origin", 6, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->version, 0,
	    UCL_STRING_TRIM), "version", 7, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->comment, 0,
	    UCL_STRING_TRIM), "comment", 7, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->maintainer, 0,
	    UCL_STRING_TRIM), "maintainer", 10, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->www, 0,
	    UCL_STRING_TRIM), "www", 3, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->abi, 0,
	    UCL_STRING_TRIM), "abi", 3, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->arch, 0,
	    UCL_STRING_TRIM), "arch", 4, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->prefix, 0,
	    UCL_STRING_TRIM), "prefix", 6, false);
	ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->sum, 0,
	    UCL_STRING_TRIM), "sum", 3, false);
	ucl_object_insert_key(top, ucl_object_fromint(pkg->flatsize), "flatsize", 8, false);
	if (pkg->dep_formula != NULL) {
		ucl_object_insert_key(top, ucl_object_fromstring_common(pkg->dep_formula, 0,
		    UCL_STRING_TRIM), "dep_formula", 11, false);
	}
	if (pkg->type == PKG_INSTALLED &&
	    (flags & PKG_MANIFEST_EMIT_LOCAL_METADATA) == PKG_MANIFEST_EMIT_LOCAL_METADATA) {
		ucl_object_insert_key(top, ucl_object_fromint(pkg->timestamp), "timestamp", 9, false);
	}

	/*
	 * XXX: dirty hack to be compatible with pkg 1.2
	 */
	if (pkg->repopath) {
		ucl_object_insert_key(top,
			ucl_object_fromstring(pkg->repopath), "path", 4, false);
		ucl_object_insert_key(top,
			ucl_object_fromstring(pkg->repopath), "repopath", 8, false);
	}

	switch (pkg->licenselogic) {
	case LICENSE_SINGLE:
		ucl_object_insert_key(top, ucl_object_fromlstring("single", 6), "licenselogic", 12, false);
		break;
	case LICENSE_AND:
		ucl_object_insert_key(top, ucl_object_fromlstring("and", 3), "licenselogic", 12, false);
		break;
	case LICENSE_OR:
		ucl_object_insert_key(top, ucl_object_fromlstring("or", 2), "licenselogic", 12, false);
		break;
	}

	pkg_debug(4, "Emitting licenses");
	seq = NULL;
	tll_foreach(pkg->licenses, l) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(l->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "licenses", 8, false);

	if (pkg->pkgsize > 0)
		ucl_object_insert_key(top, ucl_object_fromint(pkg->pkgsize), "pkgsize", 7, false);

	if (pkg->vital)
		ucl_object_insert_key(top, ucl_object_frombool(pkg->vital), "vital", 5, false);

	if (pkg->desc != NULL) {
		urlencode(pkg->desc, &tmpsbuf);
		ucl_object_insert_key(top,
			ucl_object_fromstring_common(tmpsbuf->buf, strlen(tmpsbuf->buf), UCL_STRING_TRIM),
			"desc", 4, false);
	}

	pkg_debug(4, "Emitting deps");
	map = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		submap = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(submap, ucl_object_fromstring(dep->origin), "origin", 6, false);
		ucl_object_insert_key(submap, ucl_object_fromstring(dep->version), "version", 7, false);
		if (map == NULL)
			map = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(map, submap, dep->name, 0, false);
	}
	if (map)
		ucl_object_insert_key(top, map, "deps", 4, false);

	pkg_debug(4, "Emitting categories");
	seq = NULL;
	tll_foreach(pkg->categories, c) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(c->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "categories", 10, false);

	pkg_debug(4, "Emitting users");
	seq = NULL;
	tll_foreach(pkg->users, u) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(u->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "users", 5, false);

	pkg_debug(4, "Emitting groups");
	seq = NULL;
	tll_foreach(pkg->groups, g) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(g->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "groups", 6, false);

	pkg_debug(4, "Emitting shibs_required");
	seq = NULL;
	tll_foreach(pkg->shlibs_required, s) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(s->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "shlibs_required", 15, false);

	pkg_debug(4, "Emitting shlibs_provided");
	seq = NULL;
	tll_foreach(pkg->shlibs_provided, s) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(s->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "shlibs_provided", 15, false);

	pkg_debug(4, "Emitting conflicts");
	seq = NULL;
	while (pkg_conflicts(pkg, &conflict) == EPKG_OK) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(conflict->uid));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "conflicts", 9, false);

	pkg_debug(4, "Emitting provides");
	seq = NULL;
	tll_foreach(pkg->provides, p) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(p->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "provides", 8, false);

	pkg_debug(4, "Emitting requires");
	seq = NULL;
	tll_foreach(pkg->requires, r) {
		if (seq == NULL)
			seq = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(seq, ucl_object_fromstring(r->item));
	}
	if (seq)
		ucl_object_insert_key(top, seq, "requires", 8, false);

	pkg_debug(4, "Emitting options");
	map = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		pkg_debug(2, "Emiting option: %s", option->value);
		if (map == NULL)
			map = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(map,
		    ucl_object_fromstring(option->value),
		    option->key, 0, false);
	}
	if (map)
		ucl_object_insert_key(top, map, "options", 7, false);

	map = NULL;
	tll_foreach(pkg->annotations, k) {
		kv = k->item;
		if (map == NULL)
			map = ucl_object_typed_new(UCL_OBJECT);
		/* Add annotations except for internal ones. */
		if ((strcmp(kv->key, "repository") == 0 ||
		    strcmp(kv->key, "relocated") == 0) &&
		    (flags & PKG_MANIFEST_EMIT_LOCAL_METADATA) == 0)
			continue;
		ucl_object_insert_key(map, ucl_object_fromstring(kv->value),
		    kv->key, strlen(kv->key), true);
	}
	if (map)
		ucl_object_insert_key(top, map, "annotations", 11, false);

	if ((flags & PKG_MANIFEST_EMIT_COMPACT) == 0) {
		if ((flags & PKG_MANIFEST_EMIT_NOFILES) == 0) {
			pkg_debug(4, "Emitting files");
			map = NULL;
			while (pkg_files(pkg, &file) == EPKG_OK) {
				char dpath[MAXPATHLEN];
				const char *dp = file->path;

				if (pkg->oprefix != NULL) {
					size_t l = strlen(pkg->prefix);
					if (strncmp(file->path, pkg->prefix, l) == 0 &&
							(file->path[l] == '/' || l == 1)) {
						snprintf(dpath, sizeof(dpath), "%s%s%s",
								pkg->oprefix, l == 1 ? "/" : "", file->path + l);
						dp = dpath;
					}
				}
				if (file->sum == NULL)
					file->sum = xstrdup("-");

				urlencode(dp, &tmpsbuf);
				if (map == NULL)
					map = ucl_object_typed_new(UCL_OBJECT);
				ucl_object_insert_key(map,
				    ucl_object_fromstring(file->sum),
				    tmpsbuf->buf, strlen(tmpsbuf->buf), true);
			}
			if (map)
				ucl_object_insert_key(top, map, "files", 5, false);

			pkg_debug(3, "Emitting config files");
			seq = NULL;
			while (pkg_config_files(pkg, &cf) == EPKG_OK) {
				urlencode(cf->path, &tmpsbuf);
				if (seq == NULL)
					seq = ucl_object_typed_new(UCL_ARRAY);
				ucl_array_append(seq, ucl_object_fromstring(tmpsbuf->buf));
			}
			if (seq)
				ucl_object_insert_key(top, seq, "config", 6, false);

			pkg_debug(4, "Emitting directories");
			map = NULL;
			while (pkg_dirs(pkg, &dir) == EPKG_OK) {
				urlencode(dir->path, &tmpsbuf);
				if (map == NULL)
					map = ucl_object_typed_new(UCL_OBJECT);
				ucl_object_insert_key(map,
				    ucl_object_fromstring("y"),
				    tmpsbuf->buf, strlen(tmpsbuf->buf), true);
			}
			if (map)
				ucl_object_insert_key(top, map, "directories", 11, false);
		}

		pkg_debug(4, "Emitting scripts");
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
			if (map == NULL)
				map = ucl_object_typed_new(UCL_OBJECT);
			ucl_object_insert_key(map,
			    ucl_object_fromstring_common(tmpsbuf->buf,
			        strlen(tmpsbuf->buf), UCL_STRING_TRIM),
			    script_types, 0, true);
		}
		if (map)
			ucl_object_insert_key(top, map, "scripts", 7, false);

		pkg_debug(4, "Emitting lua scripts");
		map = NULL;
		for (i = 0; i < PKG_NUM_LUA_SCRIPTS; i++) {
			if (tll_length(pkg->lua_scripts[i]) == 0)
				continue;
			switch(i) {
			case PKG_LUA_PRE_INSTALL:
				script_types = "pre-install";
				break;
			case PKG_LUA_POST_INSTALL:
				script_types = "post-install";
				break;
			case PKG_LUA_PRE_DEINSTALL:
				script_types = "pre-deinstall";
				break;
			case PKG_LUA_POST_DEINSTALL:
				script_types = "post-deinstall";
				break;
			}
			if (map == NULL)
				map = ucl_object_typed_new(UCL_OBJECT);
			ucl_object_insert_key(map,
			    pkg_lua_script_to_ucl(&pkg->lua_scripts[i]),
				    script_types, 0, true);
		}
		if (map)
			ucl_object_insert_key(top, map, "lua_scripts", 11, false);
	}

	pkg_debug(4, "Emitting message");
	if (pkg_has_message(pkg))  {
		ucl_object_insert_key(top,
			pkg_message_to_ucl(pkg),
			"messages", sizeof("messages") - 1, false);
	}

	xstring_free(tmpsbuf);

	return (top);
}


static int
emit_manifest(struct pkg *pkg, xstring **out, short flags)
{
	ucl_object_t *top;

	top = pkg_emit_object(pkg, flags);

	if ((flags & PKG_MANIFEST_EMIT_PRETTY) == PKG_MANIFEST_EMIT_PRETTY)
		ucl_object_emit_buf(top, UCL_EMIT_YAML, out);
	else if ((flags & PKG_MANIFEST_EMIT_UCL) == PKG_MANIFEST_EMIT_UCL)
		ucl_object_emit_buf(top, UCL_EMIT_CONFIG, out);
	else if ((flags & PKG_MANIFEST_EMIT_JSON) == PKG_MANIFEST_EMIT_JSON)
		ucl_object_emit_buf(top, UCL_EMIT_JSON, out);
	else
		ucl_object_emit_buf(top, UCL_EMIT_JSON_COMPACT, out);

	ucl_object_unref(top);

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
 * pkg_emit_manifest_file() and pkg_emit_manifest_buf().
 */
static int
pkg_emit_manifest_generic(struct pkg *pkg, void *out, short flags,
	    char **pdigest, bool out_is_a_buf)
{
	xstring *output = NULL;
	unsigned char digest[SHA256_BLOCK_SIZE];
	SHA256_CTX *sign_ctx = NULL;
	int rc;

	if (pdigest != NULL) {
		*pdigest = xmalloc(sizeof(digest) * 2 + 1);
		sign_ctx = xmalloc(sizeof(SHA256_CTX));
		sha256_init(sign_ctx);
	}

	if (out_is_a_buf)
		output = out;

	rc = emit_manifest(pkg, &output, flags);

	fflush(output->fp);
	if (sign_ctx != NULL)
		sha256_update(sign_ctx, output->buf, strlen(output->buf));

	if (!out_is_a_buf)
		fprintf(out, "%s\n", output->buf);

	if (pdigest != NULL) {
		sha256_final(sign_ctx, digest);
		pkg_emit_manifest_digest(digest, sizeof(digest), *pdigest);
		free(sign_ctx);
	}

	if (!out_is_a_buf)
		xstring_free(output);

	return (rc);
}

int
pkg_emit_manifest_file(struct pkg *pkg, FILE *f, short flags, char **pdigest)
{

	return (pkg_emit_manifest_generic(pkg, f, flags, pdigest, false));
}

int
pkg_emit_manifest_buf(struct pkg *pkg, xstring *b, short flags, char **pdigest)
{

	return (pkg_emit_manifest_generic(pkg, b, flags, pdigest, true));
}

int
pkg_emit_manifest(struct pkg *pkg, char **dest, short flags, char **pdigest)
{
	xstring *b;
	int rc;

	b = xstring_new();
	rc = pkg_emit_manifest_buf(pkg, b, flags, pdigest);

	if (rc != EPKG_OK) {
		xstring_free(b);
		return (rc);
	}

	*dest = xstring_get(b);

	return (rc);
}

