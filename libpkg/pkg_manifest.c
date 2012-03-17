/*
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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
#include <wctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include <wchar.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/utils.h"

#define PKG_UNKNOWN -1
#define PKG_DEPS -2
#define PKG_FILES -3
#define PKG_DIRS -4
#define PKG_SCRIPTS -5
#define PKG_CATEGORIES -6
#define PKG_LICENSES -7
#define PKG_OPTIONS -8
#define PKG_USERS -9
#define PKG_GROUPS -10
#define PKG_DIRECTORIES -11

static int pkg_set_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int pkg_set_flatsize_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int pkg_set_licenselogic_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int pkg_set_deps_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, const char *);
static int pkg_set_files_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, const char *);
static int pkg_set_dirs_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, const char *);
static int parse_sequence(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int parse_mapping(struct pkg *, yaml_node_t *, yaml_document_t *, int);

static struct manifest_key {
	const char *key;
	int type;
	yaml_node_type_t valid_type;
	int (*parse_data)(struct pkg *, yaml_node_t *, yaml_document_t *, int);
} manifest_key[] = {
	{ "name", PKG_NAME, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "origin", PKG_ORIGIN, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "version", PKG_VERSION, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "arch", PKG_ARCH, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "osversion", PKG_OSVERSION, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "www", PKG_WWW, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "comment", PKG_COMMENT, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "maintainer", PKG_MAINTAINER, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "prefix", PKG_PREFIX, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "deps", PKG_DEPS, YAML_MAPPING_NODE, parse_mapping},
	{ "files", PKG_FILES, YAML_MAPPING_NODE, parse_mapping},
	{ "dirs", PKG_DIRS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "directories", PKG_DIRECTORIES, YAML_MAPPING_NODE, parse_mapping},
	{ "flatsize", -1, YAML_SCALAR_NODE, pkg_set_flatsize_from_node},
	{ "licenselogic", -1, YAML_SCALAR_NODE, pkg_set_licenselogic_from_node},
	{ "licenses", PKG_LICENSES, YAML_SEQUENCE_NODE, parse_sequence},
	{ "desc", PKG_DESC, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "scripts", PKG_SCRIPTS, YAML_MAPPING_NODE, parse_mapping},
	{ "message", PKG_MESSAGE, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "categories", PKG_CATEGORIES, YAML_SEQUENCE_NODE, parse_sequence},
	{ "options", PKG_OPTIONS, YAML_MAPPING_NODE, parse_mapping},
	{ "users", PKG_USERS, YAML_SEQUENCE_NODE, parse_sequence}, /* compatibility with old format */
	{ "users", PKG_USERS, YAML_MAPPING_NODE, parse_mapping},
	{ "groups", PKG_GROUPS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "groups", PKG_GROUPS, YAML_MAPPING_NODE, parse_mapping}, /* compatibility with old format */
	{ NULL, -99, -99, NULL}
};

static int
urlencode(const char *src, struct sbuf **dest)
{
	size_t len;
	size_t i;

	if (*dest == NULL)
		*dest = sbuf_new_auto();
	else
		sbuf_clear(*dest);

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

	if (*dest == NULL)
		*dest = sbuf_new_auto();
	else
		sbuf_reset(*dest);

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

int
pkg_load_manifest_file(struct pkg *pkg, const char *fpath)
{
	char *manifest = NULL;
	off_t sz;
	int ret = EPKG_OK;

	if ((ret = file_to_buffer(fpath, &manifest, &sz)) != EPKG_OK)
		return (ret);

	ret = pkg_parse_manifest(pkg, manifest);
	free(manifest);

	return (ret);
}

static int
pkg_set_from_node(struct pkg *pkg, yaml_node_t *val, __unused yaml_document_t *doc, int attr)
{
	int ret = EPKG_OK;

	while (val->data.scalar.length > 0 &&
			val->data.scalar.value[val->data.scalar.length - 1] == '\n') {
		val->data.scalar.value[val->data.scalar.length - 1] = '\0';
		val->data.scalar.length--;
	}

	ret = urldecode(val->data.scalar.value, &pkg->fields[attr]);

	return (ret);
}

static int
pkg_set_flatsize_from_node(struct pkg *pkg, yaml_node_t *val, __unused yaml_document_t *doc, __unused int attr)
{
	int64_t flatsize;
	const char *errstr = NULL;
	flatsize = strtonum(val->data.scalar.value, 0, INT64_MAX, &errstr);
	if (errstr) {
		pkg_emit_error("Unable to convert %s to int64: %s",
					   val->data.scalar.value, errstr);
		return (EPKG_FATAL);
	}

	return (pkg_set(pkg, PKG_FLATSIZE, flatsize));
}
static int
pkg_set_licenselogic_from_node(struct pkg *pkg, yaml_node_t *val, __unused yaml_document_t *doc, __unused int attr)
{
	if (!strcmp(val->data.scalar.value, "single"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t) LICENSE_SINGLE);
	else if ( !strcmp(val->data.scalar.value, "and") || !strcmp(val->data.scalar.value, "dual"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_AND);
	else if ( !strcmp(val->data.scalar.value, "or") || !strcmp(val->data.scalar.value, "multi"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_OR);
	else {
		pkg_emit_error("Unknown license logic: %s", val->data.scalar.value);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

static int
parse_sequence(struct pkg * pkg, yaml_node_t *node, yaml_document_t *doc, int attr)
{
	yaml_node_item_t *item;
	yaml_node_t *val;

	item = node->data.sequence.items.start;
	while (item < node->data.sequence.items.top) {
		val = yaml_document_get_node(doc, *item);
		switch (attr) {
			case PKG_CATEGORIES:
				if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0)
					pkg_emit_error("Skipping malformed category");
				else
					pkg_addcategory(pkg, val->data.scalar.value);
				break;
			case PKG_LICENSES:
				if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0)
					pkg_emit_error("Skipping malformed license");
				else
					pkg_addlicense(pkg, val->data.scalar.value);
				break;
			case PKG_USERS:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0)
					pkg_adduser(pkg, val->data.scalar.value);
				else if (val->type == YAML_MAPPING_NODE)
					parse_mapping(pkg, val, doc, attr);
				else
					pkg_emit_error("Skipping malformed license");
				break;
			case PKG_GROUPS:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0)
					pkg_addgroup(pkg, val->data.scalar.value);
				else if (val->type == YAML_MAPPING_NODE)
					parse_mapping(pkg, val, doc, attr);
				else
					pkg_emit_error("Skipping malformed license");
				break;
			case PKG_DIRS:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0)
					pkg_adddir(pkg, val->data.scalar.value, 1);
				else if (val->type == YAML_MAPPING_NODE)
					parse_mapping(pkg, val, doc, attr);
				else
					pkg_emit_error("Skipping malformed dirs");
		}
		++item;
	}
	return (EPKG_OK);
}

static int
parse_mapping(struct pkg *pkg, yaml_node_t *item, yaml_document_t *doc, int attr)
{
	struct sbuf *tmp = NULL;
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;
	pkg_script_t script_type;

	pair = item->data.mapping.pairs.start;

	while (pair < item->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);

		if (key->data.scalar.length <= 0) {
			pkg_emit_error("Skipping empty dependency name");
			++pair;
			continue;
		}

		switch (attr) {
			case PKG_DEPS:
				if (val->type != YAML_MAPPING_NODE)
					pkg_emit_error("Skipping malformed depencency %s",
								   key->data.scalar.value);
				else
					pkg_set_deps_from_node(pkg, val, doc, key->data.scalar.value);
				break;
			case PKG_DIRS:
				if (val->type != YAML_MAPPING_NODE)
					pkg_emit_error("Skipping malformed dirs %s",
								   key->data.scalar.value);
				else
					pkg_set_dirs_from_node(pkg, val, doc, key->data.scalar.value);
				break;
			case PKG_USERS:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0)
					pkg_adduid(pkg, key->data.scalar.value, val->data.scalar.value);
				else
					pkg_emit_error("Skipping malformed users %s",
							key->data.scalar.value);
				break;
			case PKG_GROUPS:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0)
					pkg_addgid(pkg, key->data.scalar.value, val->data.scalar.value);
				else
					pkg_emit_error("Skipping malformed groups %s",
							key->data.scalar.value);
				break;
			case PKG_DIRECTORIES:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0) {
					urldecode(key->data.scalar.value, &tmp);
					if (val->data.scalar.value[0] == 'y')
						pkg_adddir(pkg, sbuf_get(tmp), 1);
					else
						pkg_adddir(pkg, sbuf_get(tmp), 0);
				} else if (val->type == YAML_MAPPING_NODE) {
					pkg_set_dirs_from_node(pkg, val, doc, key->data.scalar.value);
				} else {
					pkg_emit_error("Skipping malformed directories %s",
								   key->data.scalar.value);
				}
				break;
			case PKG_FILES:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0) {
					urldecode(key->data.scalar.value, &tmp);
					pkg_addfile(pkg, sbuf_get(tmp), val->data.scalar.length == 64 ? val->data.scalar.value : NULL);
				} else if (val->type == YAML_MAPPING_NODE)
					pkg_set_files_from_node(pkg, val, doc, key->data.scalar.value);
				else
					pkg_emit_error("Skipping malformed files %s",
								   key->data.scalar.value);
				break;
			case PKG_OPTIONS:
				if (val->type != YAML_SCALAR_NODE)
					pkg_emit_error("Skipping malformed option %s",
								   key->data.scalar.value);
				else
					pkg_addoption(pkg, key->data.scalar.value, val->data.scalar.value);
				break;
			case PKG_SCRIPTS:
				if (val->type != YAML_SCALAR_NODE)
					pkg_emit_error("Skipping malformed scripts %s",
								   key->data.scalar.value);
				if (strcmp(key->data.scalar.value, "pre-install") == 0) {
					script_type = PKG_SCRIPT_PRE_INSTALL;
				} else if (strcmp(key->data.scalar.value, "install") == 0) {
					script_type = PKG_SCRIPT_INSTALL;
				} else if (strcmp(key->data.scalar.value, "post-install") == 0) {
					script_type = PKG_SCRIPT_POST_INSTALL;
				} else if (strcmp(key->data.scalar.value, "pre-upgrade") == 0) {
					script_type = PKG_SCRIPT_PRE_UPGRADE;
				} else if (strcmp(key->data.scalar.value, "upgrade") == 0) {
					script_type = PKG_SCRIPT_UPGRADE;
				} else if (strcmp(key->data.scalar.value, "post-upgrade") == 0) {
					script_type = PKG_SCRIPT_POST_UPGRADE;
				} else if (strcmp(key->data.scalar.value, "pre-deinstall") == 0) {
					script_type = PKG_SCRIPT_PRE_DEINSTALL;
				} else if (strcmp(key->data.scalar.value, "deinstall") == 0) {
					script_type = PKG_SCRIPT_DEINSTALL;
				} else if (strcmp(key->data.scalar.value, "post-deinstall") == 0) {
					script_type = PKG_SCRIPT_POST_DEINSTALL;
				} else {
					pkg_emit_error("Skipping unknown script type: %s",
								   key->data.scalar.value);
					break;
				}

				urldecode(val->data.scalar.value, &tmp);
				pkg_addscript(pkg, sbuf_get(tmp), script_type);
				break;
		}

		++pair;
	}

	sbuf_free(tmp);
	return (EPKG_OK);
}

static int
pkg_set_files_from_node(struct pkg *pkg, yaml_node_t *item, yaml_document_t *doc, const char *filename) {
	yaml_node_pair_t *pair = NULL;
	yaml_node_t *key = NULL;
	yaml_node_t *val = NULL;
	const char *sum = NULL;
	const char *uname = NULL;
	const char *gname = NULL;
	void *set = NULL;
	mode_t perm = 0;

	pair = item->data.mapping.pairs.start;
	while (pair < item->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);
		if (key->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed file entry for %s", filename);
			++pair;
			continue;
		}

		if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed file entry for %s", filename);
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "uname"))
			uname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "gname"))
			gname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "sum") && val->data.scalar.length == 64)
			sum = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "perm")) {
			if ((set = setmode(val->data.scalar.value)) == NULL)
				pkg_emit_error("Not a valide mode: %s", val->data.scalar.value);
			else
				perm = getmode(set, 0);
		} else {
			pkg_emit_error("Skipping unknown key for file(%s): %s", filename,
						   key->data.scalar.value);
		}

		++pair;
	}

	if (key != NULL)
	    pkg_addfile_attr(pkg, key->data.scalar.value, sum, uname, gname, perm);

	return (EPKG_OK);
}

static int
pkg_set_dirs_from_node(struct pkg *pkg, yaml_node_t *item, yaml_document_t *doc, const char *dirname) {
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;
	const char *uname = NULL;
	const char *gname = NULL;
	void *set;
	mode_t perm = 0;
	bool try = false;

	pair = item->data.mapping.pairs.start;
	while (pair < item->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);
		if (key->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed file entry for %s", dirname);
			++pair;
			continue;
		}

		if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed file entry for %s", dirname);
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "uname"))
			uname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "gname"))
			gname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "perm")) {
			if ((set = setmode(val->data.scalar.value)) == NULL)
				pkg_emit_error("Not a valide mode: %s", val->data.scalar.value);
			else
				perm = getmode(set, 0);
		} else if (!strcasecmp(key->data.scalar.value, "try")) {
			if (val->data.scalar.value[0] == 'n')
				try = false;
			else if (val->data.scalar.value[0] == 'y')
				try = true;
			else
				pkg_emit_error("Wrong value for try: %s, expected 'y' or 'n'", val->data.scalar.value);
		} else {
			pkg_emit_error("Skipping unknown key for dir(%s): %s", dirname,
						   key->data.scalar.value);
		}

		++pair;
	}

	pkg_adddir_attr(pkg, dirname, uname, gname, perm, try);

	return (EPKG_OK);
}

static int
pkg_set_deps_from_node(struct pkg *pkg, yaml_node_t *item, yaml_document_t *doc, const char *depname) {
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;
	const char *origin = NULL;
	const char *version = NULL;

	pair = item->data.mapping.pairs.start;
	while (pair < item->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);
		if (key->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed dependency entry for %s",
						   depname);
			++pair;
			continue;
		}

		if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed dependency entry for %s",
						   depname);
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "origin"))
			origin = val->data.scalar.value;

		if (!strcasecmp(key->data.scalar.value, "version"))
			version = val->data.scalar.value;

		++pair;
	}

	if (origin != NULL && version != NULL)
		pkg_adddep(pkg, depname, origin, version);
	else
		pkg_emit_error("Skipping malformed dependency %s", depname);

	return (EPKG_OK);
}

static int
parse_root_node(struct pkg *pkg, yaml_node_t *node, yaml_document_t *doc) {
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;
	int i = 0;
	int retcode = EPKG_OK;

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		if (retcode == EPKG_FATAL)
			break;

		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);
		if (key->data.scalar.length <= 0) {
			pkg_emit_error("Skipping empty key");
			++pair;
			continue;
		}

		if (val->type == YAML_NO_NODE || ( val->type == YAML_SCALAR_NODE && val->data.scalar.length <= 0)) {
			/* silently skip on purpose */
			++pair;
			continue;
		}

		for (i = 0; manifest_key[i].key != NULL; i++) {
			if (!strcasecmp(key->data.scalar.value, manifest_key[i].key)) {
				if (val->type == manifest_key[i].valid_type) {
					retcode = manifest_key[i].parse_data(pkg, val, doc, manifest_key[i].type);
					break;
				}
			}

			if (manifest_key[i].key == NULL)
				pkg_emit_error("Skipping unknown manifest key: %s",
							   key->data.scalar.value);
		}
		++pair;
	}

	return (retcode);
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf)
{
	yaml_parser_t parser;
	yaml_document_t doc;
	yaml_node_t *node;
	int retcode = EPKG_FATAL;

	assert(pkg != NULL);
	assert(buf != NULL);

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, buf, strlen(buf));
	yaml_parser_load(&parser, &doc);

	node = yaml_document_get_root_node(&doc);
	if (node != NULL) {
		if (node->type != YAML_MAPPING_NODE) {
			pkg_emit_error("Invalid manifest format");
		} else {
			parse_root_node(pkg, node, &doc);
			retcode = EPKG_OK;
		}
	} else {
		pkg_emit_error("Invalid manifest format");
	}

	yaml_document_delete(&doc);
	yaml_parser_delete(&parser);

	return retcode;
}

static int
yaml_write_buf(void *data, unsigned char *buffer, size_t size)
{
	struct sbuf *dest = (struct sbuf *)data;
	sbuf_bcat(dest, buffer, size);
	return (1);
}

static void
manifest_append_seqval(yaml_document_t *doc, int parent, int *seq, const char *title, const char *value)
{
	if (*seq == -1) {
		*seq = yaml_document_add_sequence(doc, NULL, YAML_FLOW_SEQUENCE_STYLE);
		yaml_document_append_mapping_pair(doc, parent,
				yaml_document_add_scalar(doc, NULL, __DECONST(yaml_char_t*, title), strlen(title), YAML_PLAIN_SCALAR_STYLE), *seq);
	}
	yaml_document_append_sequence_item(doc, *seq,
			yaml_document_add_scalar(doc, NULL, __DECONST(yaml_char_t*, value), strlen(value), YAML_PLAIN_SCALAR_STYLE));
}

int
pkg_emit_manifest(struct pkg *pkg, char **dest)
{
	yaml_emitter_t emitter;
	yaml_document_t doc;
	char tmpbuf[BUFSIZ];
	struct pkg_dep *dep = NULL;
	struct pkg_option *option = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	struct pkg_script *script = NULL;
	struct pkg_category *category = NULL;
	struct pkg_license *license = NULL;
	struct pkg_user *user = NULL;
	struct pkg_group *group = NULL;
	struct sbuf *tmpsbuf = NULL;
	int rc = EPKG_OK;
	int mapping;
	int seq = -1;
	int map = -1;
	int depkv;
/*	int users = -1;
	int groups = -1;*/
	const char *script_types = NULL;
	struct sbuf *destbuf = sbuf_new_auto();
	const char *name, *version, *pkgorigin, *comment, *pkgarch, *osversion, *www, *pkgmaintainer, *prefix;
	const char *desc, *message;
	lic_t licenselogic;
	int64_t flatsize;

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_unicode(&emitter, 1);
	yaml_emitter_set_output(&emitter, yaml_write_buf, destbuf);

#define manifest_append_kv(map, key, val, style) yaml_document_append_mapping_pair(&doc, map, \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, key), strlen(key), YAML_PLAIN_SCALAR_STYLE), \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, val), strlen(val), YAML_##style##_SCALAR_STYLE));

#define manifest_append_map(id, map, key, block) do { \
	id = yaml_document_add_mapping(&doc, NULL, YAML_##block##_MAPPING_STYLE); \
	yaml_document_append_mapping_pair(&doc, map, \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, key), strlen(key), YAML_PLAIN_SCALAR_STYLE), id); \
	} while (0)

	yaml_document_initialize(&doc, NULL, NULL, NULL, 1, 1);
	mapping = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);

	pkg_get(pkg, PKG_NAME, &name, PKG_ORIGIN, &pkgorigin, PKG_COMMENT, &comment,
	    PKG_ARCH, &pkgarch, PKG_OSVERSION, &osversion, PKG_WWW, &www,
	    PKG_MAINTAINER, &pkgmaintainer, PKG_PREFIX, &prefix,
	    PKG_LICENSE_LOGIC, &licenselogic, PKG_DESC, &desc,
	    PKG_FLATSIZE, &flatsize, PKG_MESSAGE, &message, PKG_VERSION, &version);
	manifest_append_kv(mapping, "name", name, PLAIN);
	manifest_append_kv(mapping, "version", version, PLAIN);
	manifest_append_kv(mapping, "origin", pkgorigin, PLAIN);
	manifest_append_kv(mapping, "comment", comment, PLAIN);
	manifest_append_kv(mapping, "arch", pkgarch, PLAIN);
	manifest_append_kv(mapping, "osversion", osversion, PLAIN);
	manifest_append_kv(mapping, "www", www, PLAIN);
	manifest_append_kv(mapping, "maintainer", pkgmaintainer, PLAIN);
	manifest_append_kv(mapping, "prefix", prefix, PLAIN);
	switch (licenselogic) {
		case LICENSE_SINGLE:
			manifest_append_kv(mapping, "licenselogic", "single", PLAIN);
			break;
		case LICENSE_AND:
			manifest_append_kv(mapping, "licenselogic", "and", PLAIN);
			break;
		case LICENSE_OR:
			manifest_append_kv(mapping, "licenselogic", "or", PLAIN);
			break;
	}

	seq = -1;
	while (pkg_licenses(pkg, &license) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "licenses", pkg_license_name(license));

	snprintf(tmpbuf, BUFSIZ, "%" PRId64, flatsize);
	manifest_append_kv(mapping, "flatsize", tmpbuf, PLAIN);
	urlencode(desc, &tmpsbuf);
	manifest_append_kv(mapping, "desc", sbuf_get(tmpsbuf), LITERAL);

	map = -1;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "deps", BLOCK);

		depkv = yaml_document_add_mapping(&doc, NULL, YAML_FLOW_MAPPING_STYLE);
		yaml_document_append_mapping_pair(&doc, map,
				yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, pkg_dep_get(dep, PKG_DEP_NAME)), strlen(pkg_dep_get(dep, PKG_DEP_NAME)), YAML_PLAIN_SCALAR_STYLE),
				depkv);

		manifest_append_kv(depkv, "origin", pkg_dep_get(dep, PKG_DEP_ORIGIN), PLAIN);
		manifest_append_kv(depkv, "version", pkg_dep_get(dep, PKG_DEP_VERSION), PLAIN);
	}

	seq = -1;
	while (pkg_categories(pkg, &category) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "categories", pkg_category_name(category));

	seq = -1;
	while (pkg_users(pkg, &user) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "users", pkg_user_name(user));

	seq = -1;
	while (pkg_groups(pkg, &group) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "groups", pkg_group_name(group));
/*	while (pkg_users(pkg, &user) == EPKG_OK) {
		if (users == -1) {
			users = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "users"), 5, YAML_PLAIN_SCALAR_STYLE),
					users);
		}
		manifest_append_kv(users, pkg_user_name(user), user->uidstr);
	}

	while (pkg_groups(pkg, &group) == EPKG_OK) {
		if (groups == -1) {
			groups = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "groups"), 6, YAML_PLAIN_SCALAR_STYLE),
					groups);
		}
		manifest_append_kv(groups, pkg_group_name(group), group->gidstr);
	}*/

	map = -1;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "options", FLOW);
		manifest_append_kv(map, pkg_option_opt(option), pkg_option_value(option), PLAIN);
	}

	map = -1;
	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "files", BLOCK);
		urlencode(pkg_file_get(file, PKG_FILE_PATH), &tmpsbuf);
		manifest_append_kv(map, sbuf_get(tmpsbuf), pkg_file_get(file, PKG_FILE_SUM) && strlen(pkg_file_get(file, PKG_FILE_SUM)) > 0 ? pkg_file_get(file, PKG_FILE_SUM) : "-", PLAIN);
	}

	seq = -1;
	map = -1;
	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "directories", BLOCK);
		urlencode(pkg_dir_path(dir), &tmpsbuf);
		manifest_append_kv(map, sbuf_get(tmpsbuf), pkg_dir_try(dir) ? "y" : "n", PLAIN);
	}

	map = -1;
	while (pkg_scripts(pkg, &script) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "scripts", BLOCK);

		switch (pkg_script_type(script)) {
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
		urlencode(pkg_script_data(script), &tmpsbuf);
		manifest_append_kv(map, script_types, sbuf_get(tmpsbuf), LITERAL);
	}
	if (message != NULL && *message != '\0') {
		urlencode(message, &tmpsbuf);
		manifest_append_kv(mapping, "message", sbuf_get(tmpsbuf), LITERAL);
	}

	if (!yaml_emitter_dump(&emitter, &doc))
		rc = EPKG_FATAL;

	sbuf_free(tmpsbuf);
	sbuf_finish(destbuf);
	*dest = strdup(sbuf_get(destbuf));
	sbuf_delete(destbuf);

	yaml_emitter_delete(&emitter);
	return (rc);
}
