/*-
 * Copyright (c) 2011-2013 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#ifdef BUNDLED_YAML
#include <yaml.h>
#else
#include <bsdyml.h>
#endif

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
#define PKG_SHLIBS_REQUIRED -12
#define PKG_SHLIBS_PROVIDED -13
#define PKG_ANNOTATIONS -14
#define PKG_INFOS -15		/* Deprecated field: treat as an annotation for backwards compatibility */

static int pkg_set_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int pkg_set_size_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int pkg_infos_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int pkg_set_licenselogic_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int pkg_set_deps_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, const char *);
static int pkg_set_files_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, const char *);
static int pkg_set_dirs_from_node(struct pkg *, yaml_node_t *, yaml_document_t *, const char *);
static int parse_sequence(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int parse_mapping(struct pkg *, yaml_node_t *, yaml_document_t *, int);
static int parse_seq(struct pkg *, struct pkg_manifest_parser *, int);
static int parse_map(struct pkg *, struct pkg_manifest_parser *, int);
static int pkg_set_licenselogic_from_scalar(struct pkg *, struct pkg_manifest_parser *, int);
static int pkg_set_size_from_scalar(struct pkg *, struct pkg_manifest_parser *, int);
static int pkg_set_from_scalar(struct pkg *, struct pkg_manifest_parser *, int);

#define EV_SCALAR  YAML_SCALAR_EVENT
#define EV_MAP     YAML_MAPPING_START_EVENT
#define EV_SEQ     YAML_SEQUENCE_START_EVENT
#define EV_NONE    YAML_NO_EVENT

static struct mkey {
	const char *key;
	int type;
	yaml_event_type_t valid_type;
	int (*parse_data)(struct pkg *, struct pkg_manifest_parser *, int type);
} mkeys[] = {
	{ "annotations",     PKG_ANNOTATIONS,     EV_MAP,    parse_map},
	{ "arch",            PKG_ARCH,            EV_SCALAR, pkg_set_from_scalar},
	{ "categories",      PKG_CATEGORIES,      EV_SEQ,    parse_seq},
	{ "comment",         PKG_COMMENT,         EV_SCALAR, pkg_set_from_scalar},
	{ "deps",            PKG_DEPS,            EV_MAP,    parse_map},
	{ "desc",            PKG_DESC,            EV_SCALAR, pkg_set_from_scalar},
	{ "directories",     PKG_DIRECTORIES,     EV_MAP,    parse_map},
	{ "dirs",            PKG_DIRS,            EV_SEQ,    parse_seq},
	{ "files",           PKG_FILES,           EV_MAP,    parse_map},
	{ "flatsize",        PKG_FLATSIZE,        EV_SCALAR, pkg_set_size_from_scalar},
	{ "groups",          PKG_GROUPS,          EV_SEQ,    parse_seq},
	{ "groups",          PKG_GROUPS,          EV_MAP,    parse_map},
	{ "infos",           PKG_INFOS,           EV_SCALAR, pkg_set_from_scalar},
	{ "licenselogic",    -1,                  EV_SCALAR, pkg_set_licenselogic_from_scalar},
	{ "licenses",        PKG_LICENSES,        EV_SEQ,    parse_seq},
	{ "maintainer",      PKG_MAINTAINER,      EV_SCALAR, pkg_set_from_scalar},
	{ "message",         PKG_MESSAGE,         EV_SCALAR, pkg_set_from_scalar},
	{ "name",            PKG_NAME,            EV_SCALAR, pkg_set_from_scalar},
	{ "options",         PKG_OPTIONS,         EV_MAP,    parse_map},
	{ "origin",          PKG_ORIGIN,          EV_SCALAR, pkg_set_from_scalar},
	{ "path",            PKG_REPOPATH,        EV_SCALAR, pkg_set_from_scalar},
	{ "pkgsize",         PKG_PKGSIZE,         EV_SCALAR, pkg_set_size_from_scalar},
	{ "prefix",          PKG_PREFIX,          EV_SCALAR, pkg_set_from_scalar},
	{ "scripts",         PKG_SCRIPTS,         EV_MAP,    parse_map},
	{ "shlibs_provided", PKG_SHLIBS_PROVIDED, EV_SEQ,    parse_seq},
	{ "shlibs_required", PKG_SHLIBS_REQUIRED, EV_SEQ,    parse_seq},
	{ "sum",             PKG_CKSUM,           EV_SCALAR, pkg_set_from_scalar},
	{ "users",           PKG_USERS,           EV_SEQ,    parse_seq},
	{ "users",           PKG_USERS,           EV_MAP,    parse_map},
	{ "version",         PKG_VERSION,         EV_SCALAR, pkg_set_from_scalar},
	{ "www",             PKG_WWW,             EV_SCALAR, pkg_set_from_scalar},
	{ NULL,              -99,                 EV_NONE,   NULL}
};

/*
 * Keep sorted
 */
static struct manifest_key {
	const char *key;
	int type;
	yaml_node_type_t valid_type;
	int (*parse_data)(struct pkg *, yaml_node_t *, yaml_document_t *, int);
} manifest_keys[] = {
	{ "annotations", PKG_ANNOTATIONS, YAML_MAPPING_NODE, parse_mapping},
	{ "arch", PKG_ARCH, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "categories", PKG_CATEGORIES, YAML_SEQUENCE_NODE, parse_sequence},
	{ "comment", PKG_COMMENT, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "deps", PKG_DEPS, YAML_MAPPING_NODE, parse_mapping},
	{ "desc", PKG_DESC, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "directories", PKG_DIRECTORIES, YAML_MAPPING_NODE, parse_mapping},
	{ "dirs", PKG_DIRS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "files", PKG_FILES, YAML_MAPPING_NODE, parse_mapping},
	{ "flatsize", PKG_FLATSIZE, YAML_SCALAR_NODE, pkg_set_size_from_node},
	{ "groups", PKG_GROUPS, YAML_MAPPING_NODE, parse_mapping},
	{ "groups", PKG_GROUPS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "infos", PKG_INFOS, YAML_SCALAR_NODE, pkg_infos_from_node}, /* Deprecated: treat as an annotation */
	{ "licenselogic", -1, YAML_SCALAR_NODE, pkg_set_licenselogic_from_node},
	{ "licenses", PKG_LICENSES, YAML_SEQUENCE_NODE, parse_sequence},
	{ "maintainer", PKG_MAINTAINER, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "message", PKG_MESSAGE, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "name", PKG_NAME, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "options", PKG_OPTIONS, YAML_MAPPING_NODE, parse_mapping},
	{ "origin", PKG_ORIGIN, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "path", PKG_REPOPATH, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "pkgsize", PKG_PKGSIZE, YAML_SCALAR_NODE, pkg_set_size_from_node},
	{ "prefix", PKG_PREFIX, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "scripts", PKG_SCRIPTS, YAML_MAPPING_NODE, parse_mapping},
	{ "shlibs", PKG_SHLIBS_REQUIRED, YAML_SEQUENCE_NODE, parse_sequence}, /* Backwards compat with 1.0.x packages */
	{ "shlibs_provided", PKG_SHLIBS_PROVIDED, YAML_SEQUENCE_NODE, parse_sequence},
	{ "shlibs_required", PKG_SHLIBS_REQUIRED, YAML_SEQUENCE_NODE, parse_sequence},
	{ "sum", PKG_CKSUM, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "users", PKG_USERS, YAML_MAPPING_NODE, parse_mapping},
	{ "users", PKG_USERS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "version", PKG_VERSION, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "www", PKG_WWW, YAML_SCALAR_NODE, pkg_set_from_node},
	{ NULL, -99, -99, NULL}
};

struct dataparser {
	yaml_node_type_t type;
	int (*parse_data)(struct pkg *, yaml_node_t *, yaml_document_t *, int);
	UT_hash_handle hh;
};

struct pkg_manifest_key {
	const char *key;
	int type;
	struct dataparser *parser;
	UT_hash_handle hh;
};

struct dparser {
	yaml_event_type_t type;
	int (*parse_data)(struct pkg *, struct pkg_manifest_parser *, int);
	UT_hash_handle hh;
};

struct pmk {
	const char *key;
	int type;
	struct dparser *parser;
	UT_hash_handle hh;
};

struct pkg_manifest_parser {
	yaml_parser_t parser;
	yaml_event_t event;
	struct sbuf *buf;
	struct pmk *keys;
};

int
pkg_manifest_parser_new(struct pkg_manifest_parser **p)
{
	struct pmk *k;
	struct dparser *dp;
	int i;

	if (*p != NULL) {
		yaml_parser_delete(&(*p)->parser);
		yaml_parser_initialize(&(*p)->parser);
		return (EPKG_OK);
	}
	*p = calloc(1, sizeof(struct pkg_manifest_parser));
	if (*p == NULL)
		return (EPKG_FATAL);

	yaml_parser_initialize(&(*p)->parser);
	(*p)->buf = sbuf_new_auto();

	for (i = 0; mkeys[i].key != NULL; i++) {
		HASH_FIND_STR((*p)->keys, __DECONST(char *, mkeys[i].key), k);
		if (k == NULL) {
			k = calloc(1, sizeof(struct pmk));
			k->key = mkeys[i].key;
			k->type = mkeys[i].type;
			HASH_ADD_KEYPTR(hh, (*p)->keys, __DECONST(char *, k->key), strlen(k->key), k);
		}
		HASH_FIND_YAMLEVT(k->parser, &mkeys[i].valid_type, dp);
		if (dp != NULL)
			continue;
		dp = calloc(1, sizeof(struct dparser));
		dp->type = mkeys[i].valid_type;
		dp->parse_data = mkeys[i].parse_data;
		HASH_ADD_YAMLEVT(k->parser, type, dp);
	}

	return (EPKG_OK);
}

static void
mk_free(struct pmk *key)
{
	HASH_FREE(key->parser, dparser, free);
	free(key);
}

void
pkg_manifest_parser_free(struct pkg_manifest_parser *p)
{
	if (p == NULL)
		return;

	yaml_parser_delete(&p->parser);
	HASH_FREE(p->keys, pmk, mk_free);
}

int
pkg_manifest_keys_new(struct pkg_manifest_key **key)
{
	int i;
	struct pkg_manifest_key *k;
	struct dataparser *dp;

	if (*key != NULL)
		return (EPKG_OK);

	for (i = 0; manifest_keys[i].key != NULL; i++) {
		HASH_FIND_STR(*key, __DECONST(char *, manifest_keys[i].key), k);
		if (k == NULL) {
			k = calloc(1, sizeof(struct pkg_manifest_key));
			k->key = manifest_keys[i].key;
			k->type = manifest_keys[i].type;
			HASH_ADD_KEYPTR(hh, *key, __DECONST(char *, k->key), strlen(k->key), k);
		}
		HASH_FIND_YAMLT(k->parser, &manifest_keys[i].valid_type, dp);
		if (dp != NULL)
			continue;
		dp = calloc(1, sizeof(struct dataparser));
		dp->type = manifest_keys[i].valid_type;
		dp->parse_data = manifest_keys[i].parse_data;
		HASH_ADD_YAMLT(k->parser, type, dp);
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
is_valid_yaml_scalar(yaml_node_t *val)
{
	return (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0);
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
pkg_set_from_scalar(struct pkg *pkg, struct pkg_manifest_parser *p, int attr)
{
	/* strip the scalar */
	while (p->event.data.scalar.length > 0 &&
	    p->event.data.scalar.value[p->event.data.scalar.length - 1] == '\n') {
	    p->event.data.scalar.value[p->event.data.scalar.length - 1] = '\0';
	    p->event.data.scalar.length--;
	}

	return (urldecode(p->event.data.scalar.value, &pkg->fields[attr]));
}

static int
pkg_set_size_from_scalar(struct pkg *pkg, struct pkg_manifest_parser *p, int attr)
{
	int64_t size;
	const char *errstr = NULL;

	size = strtonum(p->event.data.scalar.value, 0, INT64_MAX, &errstr);
	if (errstr) {
		pkg_emit_error("Unable to convert %s to int64: %s",
		    p->event.data.scalar.value, errstr);
		return (EPKG_FATAL);
	}

	return (pkg_set(pkg, attr, size));
}

static int
pkg_set_licenselogic_from_scalar(struct pkg *pkg, struct pkg_manifest_parser *p, __unused int attr)
{
	if (!strcmp(p->event.data.scalar.value, "single"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t) LICENSE_SINGLE);
	else if (!strcmp(p->event.data.scalar.value, "or") ||
	    !strcmp(p->event.data.scalar.value, "dual"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_OR);
	else if (!strcmp(p->event.data.scalar.value, "and") ||
	    !strcmp(p->event.data.scalar.value, "multi"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_AND);
	else {
		pkg_emit_error("Unknown license logic: %s",
		    p->event.data.scalar.value);
		return (EPKG_FATAL);
	}

	return (EPKG_OK);
}

static int
parse_seq(struct pkg * pkg, struct pkg_manifest_parser * p, int attr)
{
	for (;;) {
		if (!yaml_parser_parse(&p->parser, &p->event)) {
			pkg_emit_error("Invalid manifest format");
			return (EPKG_FATAL);
		}

		if (p->event.type == YAML_SEQUENCE_END_EVENT)
			break;

		switch (attr) {
		case PKG_CATEGORIES:
			if (p->event.type != YAML_SCALAR_EVENT)
				pkg_emit_error("Skipping malformed category");
			else
				pkg_addcategory(pkg, p->event.data.scalar.value);
			break;
		case PKG_LICENSES:
			if (p->event.type != YAML_SCALAR_EVENT)
				pkg_emit_error("Skipping malformed license");
			else
				pkg_addlicense(pkg, p->event.data.scalar.value);
			break;
		case PKG_USERS:
			if (p->event.type == YAML_SCALAR_EVENT)
				pkg_adduser(pkg, p->event.data.scalar.value);
			else if (p->event.type == YAML_MAPPING_START_EVENT)
				parse_map(pkg, p, attr);
			else
				pkg_emit_error("Skipping malformed users");
			break;
		case PKG_GROUPS:
			if (p->event.type == YAML_SCALAR_EVENT)
				pkg_addgroup(pkg, p->event.data.scalar.value);
			else if (p->event.type == YAML_MAPPING_START_EVENT)
				parse_map(pkg, p, attr);
			else
				pkg_emit_error("Skipping malformed groups");
			break;
		case PKG_DIRS:
			if (p->event.type == YAML_SCALAR_EVENT)
				pkg_adddir(pkg, p->event.data.scalar.value, 1, false);
			else if (p->event.type == YAML_MAPPING_START_EVENT)
				parse_map(pkg, p, attr);
			else
				pkg_emit_error("Skipping malformed dirs");
			break;
		case PKG_SHLIBS_REQUIRED:
			if (p->event.type != YAML_SCALAR_EVENT)
				pkg_emit_error("Skipping malformed required shared library");
			else
				pkg_addshlib_required(pkg, p->event.data.scalar.value);
			break;
		case PKG_SHLIBS_PROVIDED:
			if (p->event.type != YAML_SCALAR_EVENT)
				pkg_emit_error("Skipping malformed provided shared library");
			else
				pkg_addshlib_provided(pkg, p->event.data.scalar.value);
			break;
		}
	}
	return (EPKG_OK);
}

static void
parse_dep(struct pkg *pkg, struct pkg_manifest_parser *p)
{
	int state = 0;
	char *orig = NULL, *vers = NULL;

	sbuf_clear(p->buf);
	sbuf_cat(p->buf, p->event.data.scalar.value);
	sbuf_finish(p->buf);

	for (;;) {
		if (!yaml_parser_parse(&p->parser, &p->event)) {
			pkg_emit_error("Invalid manifest format");
			break;
		}

		if (p->event.type == YAML_MAPPING_START_EVENT)
			continue;

		if (p->event.type == YAML_MAPPING_END_EVENT)
			break;

		if (p->event.type != YAML_SCALAR_EVENT) {
			pkg_emit_error("Invalid manifest format");
			break;
		}

		if (state == 0) {
			if (strcmp(p->event.data.scalar.value, "origin") == 0)
				state = 1;
			else if (strcmp(p->event.data.scalar.value, "version") == 0)
				state = 2;
			continue;
		}

		if (state == 1) {
			orig = strdup(p->event.data.scalar.value);
			state = 0;
			continue;
		}

		if (state == 2) {
			vers = strdup(p->event.data.scalar.value);
			state = 0;
			continue;
		}
	}

	if (orig != NULL && vers != NULL)
		pkg_adddep(pkg, sbuf_data(p->buf), orig, vers, false);
	else
		pkg_emit_error("Skipping malformed dependency %s", sbuf_data(p->buf));

	free(vers);
	free(orig);

	return;
}

static void
parse_script(struct pkg *pkg, struct pkg_manifest_parser *p)
{
	pkg_script script_type;

	script_type = script_type_str(p->event.data.scalar.value);

	if (!yaml_parser_parse(&p->parser, &p->event)) {
		pkg_emit_error("Invalid manifest format");
		return;
	}

	if (p->event.type != YAML_SCALAR_EVENT) {
		pkg_emit_error("Invalid manifest format");
		return;
	}

	urldecode(p->event.data.scalar.value, &p->buf);;
	pkg_addscript(pkg, sbuf_data(p->buf), script_type);
}

static void
parse_file(struct pkg *pkg, struct pkg_manifest_parser *p)
{
	urldecode(p->event.data.scalar.value, &p->buf);

	if (!yaml_parser_parse(&p->parser, &p->event)) {
		pkg_emit_error("Invalid manifest format");
		return;
	}

	if (p->event.type == YAML_SCALAR_EVENT) {
		pkg_addfile(pkg, sbuf_data(p->buf), p->event.data.scalar.value, false);
		return;
	}

	if (p->event.type == YAML_MAPPING_START_EVENT) {
		/* TODO */
	}
}

static void
parse_option(struct pkg *pkg, struct pkg_manifest_parser *p)
{
	sbuf_clear(p->buf);
	sbuf_cat(p->buf, p->event.data.scalar.value);
	sbuf_finish(p->buf);

	if (!yaml_parser_parse(&p->parser, &p->event)) {
		pkg_emit_error("Invalid manifest format");
		return;
	}

	if (p->event.type != YAML_SCALAR_EVENT) {
		pkg_emit_error("Invalid manifest format");
		return;
	}

	pkg_addoption(pkg, sbuf_data(p->buf), p->event.data.scalar.value);
}

static void
parse_directory(struct pkg *pkg, struct pkg_manifest_parser *p)
{
	urldecode(p->event.data.scalar.value, &p->buf);

	if (!yaml_parser_parse(&p->parser, &p->event)) {
		pkg_emit_error("Invalid manifest format");
		return;
	}

	if (p->event.type == YAML_SCALAR_EVENT) {
		pkg_adddir(pkg, sbuf_data(p->buf), p->event.data.scalar.value[0] == 'y', false);
		return;
	}

	if (p->event.type == YAML_MAPPING_START_EVENT) {
		/* TODO */
	}
}

static int
parse_map(struct pkg *pkg, struct pkg_manifest_parser *p, int attr)
{
	for (;;) {
		if (!yaml_parser_parse(&p->parser, &p->event)) {
			pkg_emit_error("Invalid manifest format");
			break;
		}

		if (p->event.type == YAML_MAPPING_END_EVENT)
			break;

		if (p->event.type != YAML_SCALAR_EVENT)
			continue;

		switch (attr) {
		case PKG_DEPS:
			parse_dep(pkg, p);
			break;
		case PKG_SCRIPTS:
			parse_script(pkg, p);
			break;
		case PKG_FILES:
			parse_file(pkg, p);
			break;
		case PKG_OPTIONS:
			parse_option(pkg, p);
			break;
		case PKG_DIRECTORIES:
			parse_directory(pkg, p);
			break;
		default:
			break;
		}
	}

	return (EPKG_OK);
}

int
pkg_load_manifest_file(struct pkg *pkg, const char *fpath, struct pkg_manifest_key *keys)
{
	FILE *f;
	int ret;

	pkg_debug(1, "Loading manifest from '%s'", fpath);
	f = fopen(fpath, "r");
	if (f == NULL) {
		pkg_emit_errno("open", fpath);
		return (EPKG_FATAL);
	}

	ret = pkg_parse_manifest_file(pkg, f, keys);
	fclose(f);

	return (ret);
}

static int
pkg_set_from_node(struct pkg *pkg, yaml_node_t *val,
    __unused yaml_document_t *doc, int attr)
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
pkg_infos_from_node(struct pkg *pkg, yaml_node_t *val,
    __unused yaml_document_t *doc, __unused int attr)
{
	while (val->data.scalar.length > 0 &&
	    val->data.scalar.value[val->data.scalar.length - 1] == '\n') {
		val->data.scalar.value[val->data.scalar.length - 1] = '\0';
		val->data.scalar.length--;
	}

	pkg_addannotation(pkg, "_INFOS_", val->data.scalar.value);

	return (EPKG_OK);
}

static int
pkg_set_size_from_node(struct pkg *pkg, yaml_node_t *val,
    __unused yaml_document_t *doc, int attr)
{
	int64_t size;
	const char *errstr = NULL;
	size = strtonum(val->data.scalar.value, 0, INT64_MAX, &errstr);
	if (errstr) {
		pkg_emit_error("Unable to convert %s to int64: %s",
					   val->data.scalar.value, errstr);
		return (EPKG_FATAL);
	}

	return (pkg_set(pkg, attr, size));
}
static int
pkg_set_licenselogic_from_node(struct pkg *pkg, yaml_node_t *val,
    __unused yaml_document_t *doc, __unused int attr)
{
	if (!strcmp(val->data.scalar.value, "single"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t) LICENSE_SINGLE);
	else if (!strcmp(val->data.scalar.value, "or") ||
	    !strcmp(val->data.scalar.value, "dual"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_OR);
	else if (!strcmp(val->data.scalar.value, "and") ||
	    !strcmp(val->data.scalar.value, "multi"))
		pkg_set(pkg, PKG_LICENSE_LOGIC, (int64_t)LICENSE_AND);
	else {
		pkg_emit_error("Unknown license logic: %s",
		    val->data.scalar.value);
		return (EPKG_FATAL);
	}
	return (EPKG_OK);
}

static int
parse_sequence(struct pkg * pkg, yaml_node_t *node, yaml_document_t *doc,
    int attr)
{
	yaml_node_item_t *item;
	yaml_node_t *val;

	pkg_debug(3, "%s", "Manifest: parsing sequence");
	item = node->data.sequence.items.start;
	while (item < node->data.sequence.items.top) {
		val = yaml_document_get_node(doc, *item);
		switch (attr) {
		case PKG_CATEGORIES:
			if (!is_valid_yaml_scalar(val))
				pkg_emit_error("Skipping malformed category");
			else
				pkg_addcategory(pkg, val->data.scalar.value);
			break;
		case PKG_LICENSES:
			if (!is_valid_yaml_scalar(val))
				pkg_emit_error("Skipping malformed license");
			else
				pkg_addlicense(pkg, val->data.scalar.value);
			break;
		case PKG_USERS:
			if (is_valid_yaml_scalar(val))
				pkg_adduser(pkg, val->data.scalar.value);
			else if (val->type == YAML_MAPPING_NODE)
				parse_mapping(pkg, val, doc, attr);
			else
				pkg_emit_error("Skipping malformed license");
			break;
		case PKG_GROUPS:
			if (is_valid_yaml_scalar(val))
				pkg_addgroup(pkg, val->data.scalar.value);
			else if (val->type == YAML_MAPPING_NODE)
				parse_mapping(pkg, val, doc, attr);
			else
				pkg_emit_error("Skipping malformed license");
			break;
		case PKG_DIRS:
			if (is_valid_yaml_scalar(val))
				pkg_adddir(pkg, val->data.scalar.value, 1, false);
			else if (val->type == YAML_MAPPING_NODE)
				parse_mapping(pkg, val, doc, attr);
			else
				pkg_emit_error("Skipping malformed dirs");
			break;
		case PKG_SHLIBS_REQUIRED:
			if (!is_valid_yaml_scalar(val))
				pkg_emit_error("Skipping malformed required shared library");
			else
				pkg_addshlib_required(pkg, val->data.scalar.value);
			break;
		case PKG_SHLIBS_PROVIDED:
			if (!is_valid_yaml_scalar(val))
				pkg_emit_error("Skipping malformed provided shared library");
			else
				pkg_addshlib_provided(pkg, val->data.scalar.value);
			break;
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
	pkg_script script_type;

	pair = item->data.mapping.pairs.start;

	pkg_debug(3, "%s", "Manifest: parsing mapping");
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
				pkg_emit_error("Skipping malformed dependency %s",
				    key->data.scalar.value);
			else
				pkg_set_deps_from_node(pkg, val, doc,
				    key->data.scalar.value);
			break;
		case PKG_DIRS:
			if (val->type != YAML_MAPPING_NODE)
				pkg_emit_error("Skipping malformed dirs %s",
				    key->data.scalar.value);
			else
				pkg_set_dirs_from_node(pkg, val, doc,
				    key->data.scalar.value);
			break;
		case PKG_USERS:
			if (is_valid_yaml_scalar(val))
				pkg_adduid(pkg, key->data.scalar.value,
				    val->data.scalar.value);
			else
				pkg_emit_error("Skipping malformed users %s",
						key->data.scalar.value);
			break;
		case PKG_GROUPS:
			if (is_valid_yaml_scalar(val))
				pkg_addgid(pkg, key->data.scalar.value,
				    val->data.scalar.value);
			else
				pkg_emit_error("Skipping malformed groups %s",
						key->data.scalar.value);
			break;
		case PKG_DIRECTORIES:
			if (is_valid_yaml_scalar(val)) {
				urldecode(key->data.scalar.value, &tmp);
				if (val->data.scalar.value[0] == 'y')
					pkg_adddir(pkg, sbuf_get(tmp), 1, false);
				else
					pkg_adddir(pkg, sbuf_get(tmp), 0, false);
			} else if (val->type == YAML_MAPPING_NODE) {
				pkg_set_dirs_from_node(pkg, val, doc,
				    key->data.scalar.value);
			} else {
				pkg_emit_error("Skipping malformed directories %s",
				    key->data.scalar.value);
			}
			break;
		case PKG_FILES:
			if (is_valid_yaml_scalar(val)) {
				const char *pkg_sum = NULL;
				if (val->data.scalar.length == 64)
					pkg_sum = val->data.scalar.value;
				urldecode(key->data.scalar.value, &tmp);
				pkg_addfile(pkg, sbuf_get(tmp), pkg_sum, false);
			} else if (val->type == YAML_MAPPING_NODE)
				pkg_set_files_from_node(pkg, val, doc,
				    key->data.scalar.value);
			else
				pkg_emit_error("Skipping malformed files %s",
				    key->data.scalar.value);
			break;
		case PKG_OPTIONS:
			if (val->type != YAML_SCALAR_NODE)
				pkg_emit_error("Skipping malformed option %s",
				    key->data.scalar.value);
			else
				pkg_addoption(pkg, key->data.scalar.value,
				    val->data.scalar.value);
			break;
		case PKG_SCRIPTS:
			if (val->type != YAML_SCALAR_NODE)
				pkg_emit_error("Skipping malformed scripts %s",
				    key->data.scalar.value);
			script_type = script_type_str(key->data.scalar.value);
			if (script_type == PKG_SCRIPT_UNKNOWN) {
				pkg_emit_error("Skipping unknown script "
				    "type: %s", key->data.scalar.value);
				break;
			}

			urldecode(val->data.scalar.value, &tmp);
			pkg_addscript(pkg, sbuf_get(tmp), script_type);
			break;
		case PKG_ANNOTATIONS:
			if (val->type != YAML_SCALAR_NODE)
				pkg_emit_error("Skipping malformed annotation %s",
				    key->data.scalar.value);
			else
				pkg_addannotation(pkg, key->data.scalar.value,
				    val->data.scalar.value);
			break;
		}

		++pair;
	}

	sbuf_free(tmp);
	return (EPKG_OK);
}

static int
pkg_set_files_from_node(struct pkg *pkg, yaml_node_t *item,
    yaml_document_t *doc, const char *filename)
{
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
			pkg_emit_error("Skipping malformed file entry for %s",
			    filename);
			++pair;
			continue;
		}

		if (val->type != YAML_SCALAR_NODE ||
		    val->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed file entry for %s",
			    filename);
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "uname"))
			uname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "gname"))
			gname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "sum") &&
		    val->data.scalar.length == 64)
			sum = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "perm")) {
			if ((set = setmode(val->data.scalar.value)) == NULL)
				pkg_emit_error("Not a valid mode: %s",
				    val->data.scalar.value);
			else
				perm = getmode(set, 0);
		} else {
			pkg_emit_error("Skipping unknown key for file(%s): %s",
			    filename, key->data.scalar.value);
		}

		++pair;
	}

	if (key != NULL)
		pkg_addfile_attr(pkg, filename, sum, uname, gname,
		    perm, false);

	return (EPKG_OK);
}

static int
pkg_set_dirs_from_node(struct pkg *pkg, yaml_node_t *item, yaml_document_t *doc,
    const char *dirname)
{
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
			pkg_emit_error("Skipping malformed file entry for %s",
			    dirname);
			++pair;
			continue;
		}

		if (val->type != YAML_SCALAR_NODE ||
		    val->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed file entry for %s",
			    dirname);
			++pair;
			continue;
		}

		if (!strcasecmp(key->data.scalar.value, "uname"))
			uname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "gname"))
			gname = val->data.scalar.value;
		else if (!strcasecmp(key->data.scalar.value, "perm")) {
			if ((set = setmode(val->data.scalar.value)) == NULL)
				pkg_emit_error("Not a valid mode: %s",
				    val->data.scalar.value);
			else
				perm = getmode(set, 0);
		} else if (!strcasecmp(key->data.scalar.value, "try")) {
			if (val->data.scalar.value[0] == 'n')
				try = false;
			else if (val->data.scalar.value[0] == 'y')
				try = true;
			else
				pkg_emit_error("Wrong value for try: %s, "
				    "expected 'y' or 'n'",
				    val->data.scalar.value);
		} else {
			pkg_emit_error("Skipping unknown key for dir(%s): %s",
			    dirname, key->data.scalar.value);
		}

		++pair;
	}

	pkg_adddir_attr(pkg, dirname, uname, gname, perm, try, false);

	return (EPKG_OK);
}

static int
pkg_set_deps_from_node(struct pkg *pkg, yaml_node_t *item, yaml_document_t *doc,
    const char *depname)
{
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;
	const char *origin = NULL;
	const char *version = NULL;

	pair = item->data.mapping.pairs.start;
	while (pair < item->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);

		if (key->data.scalar.length <= 0 ||
		    val->type != YAML_SCALAR_NODE ||
		    val->data.scalar.length <= 0) {
			pkg_emit_error("Skipping malformed dependency entry "
			    "for %s", depname);
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
		pkg_adddep(pkg, depname, origin, version, false);
	else
		pkg_emit_error("Skipping malformed dependency %s", depname);

	return (EPKG_OK);
}

static int
parse_root_node(struct pkg *pkg, struct pkg_manifest_key *keys,  yaml_node_t *node, yaml_document_t *doc) {
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;
	int retcode = EPKG_OK;
	struct pkg_manifest_key *selected_key;
	struct dataparser *dp;

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

		if (val->type == YAML_NO_NODE ||
		    (val->type == YAML_SCALAR_NODE &&
		     val->data.scalar.length <= 0)) {
			/* silently skip on purpose */
			++pair;
			continue;
		}

		pkg_debug(2, "Manifest: found key: '%s'", key->data.scalar.value);

		HASH_FIND_STR(keys, key->data.scalar.value, selected_key);
		if (selected_key != NULL) {
			HASH_FIND_YAMLT(selected_key->parser, &val->type, dp);
			if (dp != NULL) {
				dp->parse_data(pkg, val, doc, selected_key->type);
			}
		}
		++pair;
	}

	return (retcode);
}

static int
scan_manifest(struct pkg *pkg, struct pkg_manifest_parser *p)
{
	int level = 0;
	int rc = EPKG_OK;
	struct pmk *selected_key;
	struct dparser *dp;

	pkg_debug(2, "%s", "Start scanning the manifest");

	for (;;) {
		if (!yaml_parser_parse(&p->parser, &p->event)) {
			pkg_emit_error("Invalid manifest format");
			rc = EPKG_FATAL;
			break;
		}
		pkg_debug(3, "YAML event: %d\n", p->event.type);
		if (p->event.type == YAML_STREAM_END_EVENT ||
		    p->event.type == YAML_DOCUMENT_END_EVENT)
			break;

		if (level == 0 &&
		    (p->event.type == YAML_STREAM_START_EVENT ||
		     p->event.type == YAML_DOCUMENT_START_EVENT))
			continue;

		if (level == 0 && p->event.type == YAML_MAPPING_START_EVENT) {
			level++;
			continue;
		}

		if (level == 0 && p->event.type == YAML_MAPPING_START_EVENT) {
			pkg_emit_error("Invalid manifest format: la");
			rc = EPKG_FATAL;
			break;
		}

		if (level == 1 ) {
			if (p->event.type == YAML_MAPPING_END_EVENT) {
				level--;
				continue;
			}

			if (p->event.type != YAML_SCALAR_EVENT) {
				pkg_emit_error("Invalid manifest format");
				rc = EPKG_FATAL;
				break;
			}

			HASH_FIND_STR(p->keys, p->event.data.scalar.value, selected_key);
			if (selected_key != NULL) {
				pkg_debug(2, "Found keyword '%s'",  p->event.data.scalar.value);
				if (!yaml_parser_parse(&p->parser, &p->event)) {
					pkg_emit_error("Inavlid manifest format");
					rc = EPKG_FATAL;
					break;
				}

				HASH_FIND_YAMLEVT(selected_key->parser, &p->event.type, dp);
				if (dp != NULL) {
					dp->parse_data(pkg, p , selected_key->type);
				} else {
					pkg_emit_error("No parser associated with '%s'",  p->event.data.scalar.value);
					rc = EPKG_FATAL;
					break;
				}
			} else {
				pkg_emit_error("Unknown keyword: '%s'", p->event.data.scalar.value);
				rc = EPKG_FATAL;
				break;
			}
		}
	}

	return (rc);
}

static int
parse_manifest(struct pkg *pkg, struct pkg_manifest_key *keys, yaml_parser_t *parser)
{
	yaml_document_t doc;
	yaml_node_t *node;
	int retcode = EPKG_FATAL;

	yaml_parser_load(parser, &doc);
	node = yaml_document_get_root_node(&doc);
	if (node != NULL) {
		if (node->type != YAML_MAPPING_NODE) {
			pkg_emit_error("Invalid manifest format in package");
		} else {
			retcode = parse_root_node(pkg, keys, node, &doc);
		}
	} else {
		pkg_emit_error("Invalid manifest format: %s", parser->problem);
	}

	yaml_document_delete(&doc);

	return (retcode);
}

static int
archive_reader(void * data, unsigned char *buf, size_t size, size_t *r)
{
	struct archive *a = (struct archive *)data;

	*r = archive_read_data(a, buf, size);

	return (1);
}

int
pkg_parse_manifest_archive(struct pkg *pkg, struct archive *a, struct pkg_manifest_key *keys)
{
	yaml_parser_t parser;
	int rc;

	assert(pkg != NULL);

	pkg_debug(2, "%s", "Parsing manifest from archive");
	yaml_parser_initialize(&parser);
	yaml_parser_set_input(&parser, archive_reader, a);

	rc = parse_manifest(pkg, keys, &parser);

	yaml_parser_delete(&parser);

	return (rc);
}

int
pkg_parse_manifest_ev(struct pkg *pkg, char *buf, size_t len, struct pkg_manifest_parser *p)
{
	int rc;

	assert(pkg != NULL);
	assert(buf != NULL);
	assert(p != NULL);

	yaml_parser_set_input_string(&p->parser, buf, len);

	rc = scan_manifest(pkg, p);

	return (rc);
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf, struct pkg_manifest_key *keys)
{
	yaml_parser_t parser;
	int rc;

	assert(pkg != NULL);
	assert(buf != NULL);

	pkg_debug(2, "%s", "Parsing manifest from buffer");
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, buf, strlen(buf));

	rc = parse_manifest(pkg, keys, &parser);

	yaml_parser_delete(&parser);

	return (rc);
}

int
pkg_parse_manifest_file_ev(struct pkg *pkg, FILE *f, struct pkg_manifest_parser *p)
{
	int rc;

	assert(pkg != NULL);
	assert(f != NULL);
	assert(p != NULL);

	pkg_debug(2, "%s", "Parsing manifest from file (event type)");
	yaml_parser_set_input_file(&p->parser, f);

	rc = scan_manifest(pkg, p);

	return (rc);
}

int
pkg_parse_manifest_file(struct pkg *pkg, FILE *f, struct pkg_manifest_key *keys)
{
	yaml_parser_t parser;
	int rc;

	assert(pkg != NULL);
	assert(f != NULL);

	pkg_debug(2, "%s", "Parsing manifest from file");
	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, f);

	rc = parse_manifest(pkg, keys, &parser);

	yaml_parser_delete(&parser);

	return (rc);
}

struct pkg_yaml_emitter_data {
	SHA256_CTX *sign_ctx;
	union {
		struct sbuf *sbuf;
		FILE *file;
		char *dest;
	} data;
};

static int
yaml_write_buf(void *data, unsigned char *buffer, size_t size)
{
	struct pkg_yaml_emitter_data *dest = (struct pkg_yaml_emitter_data *)data;

	sbuf_bcat(dest->data.sbuf, buffer, size);
	if (dest->sign_ctx != NULL)
		SHA256_Update(dest->sign_ctx, buffer, size);

	return (1);
}

static int
yaml_write_file(void *data, unsigned char *buffer, size_t size)
{
	struct pkg_yaml_emitter_data *dest = (struct pkg_yaml_emitter_data *)data;

	if (fwrite(buffer, size, 1, dest->data.file) != 1)
		return -1;

	if (dest->sign_ctx != NULL)
		SHA256_Update(dest->sign_ctx, buffer, size);

	return (1);
}

static void
manifest_append_seqval(yaml_document_t *doc, int parent, int *seq,
    const char *title, const char *value)
{
	int scalar;

	if (*seq == -1) {
		*seq = yaml_document_add_sequence(doc, NULL,
		    YAML_BLOCK_SEQUENCE_STYLE);
		scalar = yaml_document_add_scalar(doc, NULL,
		    __DECONST(yaml_char_t *, title), strlen(title),
		    YAML_PLAIN_SCALAR_STYLE);
		yaml_document_append_mapping_pair(doc, parent, scalar, *seq);
	}
	scalar = yaml_document_add_scalar(doc, NULL,
	    __DECONST(yaml_char_t *, value), strlen(value),
	    YAML_PLAIN_SCALAR_STYLE);
	yaml_document_append_sequence_item(doc, *seq, scalar);
}

#define	YAML_ADD_SCALAR(doc, name, style)				       \
	yaml_document_add_scalar(doc, NULL, __DECONST(yaml_char_t *, name),    \
	    strlen(name), YAML_##style##_SCALAR_STYLE)

#define manifest_append_kv(map, key, val, style) do {			\
	int key_obj = YAML_ADD_SCALAR(&doc, key, PLAIN);		\
	int val_obj = YAML_ADD_SCALAR(&doc, val, style);		\
	yaml_document_append_mapping_pair(&doc, map, key_obj, val_obj);	\
} while (0)


int
pkg_emit_filelist(struct pkg *pkg, FILE *f)
{
	yaml_emitter_t emitter;
	yaml_document_t doc;

	struct pkg_file *file = NULL;

	const char *name, *origin, *version;

	int mapping;
	int seq;
	struct sbuf *b = NULL;
	int rc = EPKG_OK;

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_unicode(&emitter, 1);
	yaml_emitter_set_output_file(&emitter, f);
	yaml_document_initialize(&doc, NULL, NULL, NULL, 0, 1);
	mapping = yaml_document_add_mapping(&doc, NULL,
	    YAML_BLOCK_MAPPING_STYLE);

	pkg_get(pkg, PKG_NAME, &name, PKG_ORIGIN, &origin, PKG_VERSION, &version);
	manifest_append_kv(mapping, "origin", origin, PLAIN);
	manifest_append_kv(mapping, "name", name, PLAIN);
	manifest_append_kv(mapping, "version", version, PLAIN);

	seq = -1;
	while (pkg_files(pkg, &file) == EPKG_OK) {
		urlencode(pkg_file_path(file), &b);
		manifest_append_seqval(&doc, mapping, &seq, "files", sbuf_data(b));
	}

	if (!yaml_emitter_dump(&emitter, &doc))
		rc = EPKG_FATAL;

	if (b != NULL)
		sbuf_delete(b);

	yaml_emitter_delete(&emitter);

	return (rc);
}

static int
emit_manifest(struct pkg *pkg, yaml_emitter_t *emitter, short flags)
{
	yaml_document_t doc;
	char tmpbuf[BUFSIZ];
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
	int rc = EPKG_OK;
	int mapping;
	int seq = -1;
	int map = -1;
	int depkv;
	int i;
/*	int users = -1;
	int groups = -1;*/
	const char *comment, *desc, *message, *name, *pkgarch;
	const char *pkgmaintainer, *pkgorigin, *prefix, *version, *www;
	const char *repopath, *pkgsum;
	const char *script_types = NULL;
	lic_t licenselogic;
	int64_t flatsize, pkgsize;
	yaml_mapping_style_t manifest_mapping = YAML_FLOW_MAPPING_STYLE;

#define manifest_append_map(id, map, key, block) do {			\
	int scalar_obj = YAML_ADD_SCALAR(&doc, key, PLAIN);		\
	id = yaml_document_add_mapping(&doc, NULL, YAML_##block##_MAPPING_STYLE); \
	yaml_document_append_mapping_pair(&doc, map, scalar_obj, id);	\
} while (0)

	if ((flags & PKG_MANIFEST_EMIT_PRETTY) == PKG_MANIFEST_EMIT_PRETTY)
		manifest_mapping = YAML_BLOCK_MAPPING_STYLE;

	yaml_document_initialize(&doc, NULL, NULL, NULL, 0, 1);
	mapping = yaml_document_add_mapping(&doc, NULL,
	    manifest_mapping);

	pkg_get(pkg, PKG_NAME, &name, PKG_ORIGIN, &pkgorigin,
	    PKG_COMMENT, &comment, PKG_ARCH, &pkgarch, PKG_WWW, &www,
	    PKG_MAINTAINER, &pkgmaintainer, PKG_PREFIX, &prefix,
	    PKG_LICENSE_LOGIC, &licenselogic, PKG_DESC, &desc,
	    PKG_FLATSIZE, &flatsize, PKG_MESSAGE, &message,
	    PKG_VERSION, &version, PKG_REPOPATH, &repopath,
	    PKG_CKSUM, &pkgsum, PKG_PKGSIZE, &pkgsize);
	manifest_append_kv(mapping, "name", name, PLAIN);
	manifest_append_kv(mapping, "version", version, PLAIN);
	manifest_append_kv(mapping, "origin", pkgorigin, PLAIN);
	manifest_append_kv(mapping, "comment", comment, PLAIN);
	manifest_append_kv(mapping, "arch", pkgarch, PLAIN);
	manifest_append_kv(mapping, "www", www, PLAIN);
	manifest_append_kv(mapping, "maintainer", pkgmaintainer, PLAIN);
	manifest_append_kv(mapping, "prefix", prefix, PLAIN);
	if (repopath != NULL)
		manifest_append_kv(mapping, "path", repopath, PLAIN);
	if (pkgsum != NULL)
		manifest_append_kv(mapping, "sum", pkgsum, PLAIN);

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
		manifest_append_seqval(&doc, mapping, &seq, "licenses",
		    pkg_license_name(license));

	snprintf(tmpbuf, BUFSIZ, "%" PRId64, flatsize);
	manifest_append_kv(mapping, "flatsize", tmpbuf, PLAIN);
	if (pkgsize > 0) {
		snprintf(tmpbuf, BUFSIZ, "%" PRId64, pkgsize);
	manifest_append_kv(mapping, "pkgsize", tmpbuf, PLAIN);
	}
	urlencode(desc, &tmpsbuf);
	manifest_append_kv(mapping, "desc", sbuf_get(tmpsbuf), LITERAL);

	map = -1;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "deps", BLOCK);

		manifest_append_map(depkv, map, pkg_dep_name(dep), FLOW);
		manifest_append_kv(depkv, "origin", pkg_dep_origin(dep), PLAIN);
		manifest_append_kv(depkv, "version", pkg_dep_version(dep),
		    PLAIN);
	}

	seq = -1;
	while (pkg_categories(pkg, &category) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "categories",
		    pkg_category_name(category));

	seq = -1;
	while (pkg_users(pkg, &user) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "users",
		    pkg_user_name(user));

	seq = -1;
	while (pkg_groups(pkg, &group) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "groups",
		    pkg_group_name(group));

	seq = -1;
	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "shlibs_required",
		    pkg_shlib_name(shlib));

	seq = -1;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "shlibs_provided",
		    pkg_shlib_name(shlib));

	map = -1;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "options", FLOW);
		manifest_append_kv(map, pkg_option_opt(option),
		    pkg_option_value(option), PLAIN);
	}
	map = -1;
	while (pkg_annotations(pkg, &note) == EPKG_OK) {
		if (map == -1)
			manifest_append_map(map, mapping, "annotations", FLOW);
		manifest_append_kv(map, pkg_annotation_tag(note),
		    pkg_annotation_value(note), PLAIN);
	}

	if ((flags & PKG_MANIFEST_EMIT_COMPACT) == 0) {
		map = -1;
		if ((flags & PKG_MANIFEST_EMIT_NOFILES) == 0) {
			while (pkg_files(pkg, &file) == EPKG_OK) {
				const char *pkg_sum = pkg_file_cksum(file);

				if (pkg_sum == NULL || pkg_sum[0] == '\0')
					pkg_sum = "-";

				if (map == -1)
					manifest_append_map(map, mapping, "files", BLOCK);
				urlencode(pkg_file_path(file), &tmpsbuf);
				manifest_append_kv(map, sbuf_get(tmpsbuf), pkg_sum, PLAIN);
			}

			seq = -1;
			map = -1;
			while (pkg_dirs(pkg, &dir) == EPKG_OK) {
				const char *try_str;
				if (map == -1)
					manifest_append_map(map, mapping, "directories", BLOCK);
				urlencode(pkg_dir_path(dir), &tmpsbuf);
				try_str = pkg_dir_try(dir) ? "y" : "n";
				manifest_append_kv(map, sbuf_get(tmpsbuf), try_str, PLAIN);
			}
		}

		map = -1;
		for (i = 0; i < PKG_NUM_SCRIPTS; i++) {
			if (map == -1)
				manifest_append_map(map, mapping, "scripts", BLOCK);

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
			manifest_append_kv(map, script_types, sbuf_get(tmpsbuf),
			    LITERAL);
		}
	}

	if (message != NULL && *message != '\0') {
		urlencode(message, &tmpsbuf);
		manifest_append_kv(mapping, "message", sbuf_get(tmpsbuf),
		    LITERAL);
	}

	if (!yaml_emitter_dump(emitter, &doc))
		rc = EPKG_FATAL;

	sbuf_free(tmpsbuf);

	return (rc);
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
	yaml_emitter_t emitter;
	struct pkg_yaml_emitter_data emitter_data;
	unsigned char digest[SHA256_DIGEST_LENGTH];
	int rc;

	if (pdigest != NULL) {
		*pdigest = malloc(sizeof(digest) * 2 + 1);
		emitter_data.sign_ctx = malloc(sizeof(SHA256_CTX));
		SHA256_Init(emitter_data.sign_ctx);
	}
	else {
		emitter_data.sign_ctx = NULL;
	}

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_unicode(&emitter, 1);
	yaml_emitter_set_width(&emitter, -1);

	if (out_is_a_sbuf) { /* out is (struct sbuf *) */
		emitter_data.data.sbuf = out;
		yaml_emitter_set_output(&emitter, yaml_write_buf, &emitter_data);
	} else { /* out is (FILE *) */
		emitter_data.data.file = out;
		yaml_emitter_set_output(&emitter, yaml_write_file, &emitter_data);
	}

	rc = emit_manifest(pkg, &emitter, flags);

	if (pdigest != NULL) {
		SHA256_Final(digest, emitter_data.sign_ctx);
		pkg_emit_manifest_digest(digest, sizeof(digest), *pdigest);
		free(emitter_data.sign_ctx);
	}

	yaml_emitter_delete(&emitter);

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

