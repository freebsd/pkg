#include <sys/types.h>
#include <sys/sbuf.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include <wchar.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_util.h"
#include "pkg_private.h"

#define PKG_UNKNOWN -1
#define PKG_DEPS -2
#define PKG_CONFLICTS -3
#define PKG_FILES -4
#define PKG_DIRS -5
#define PKG_FLATSIZE -6
#define PKG_SCRIPTS -7
#define PKG_CATEGORIES -8
#define PKG_LICENSELOGIC -9
#define PKG_LICENSES -10
#define PKG_OPTIONS -11
#define PKG_USERS -12
#define PKG_GROUPS -13

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
	{ "conflicts", PKG_CONFLICTS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "files", PKG_FILES, YAML_MAPPING_NODE, parse_mapping},
	{ "dirs", PKG_DIRS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "flatsize", -1, YAML_SCALAR_NODE, pkg_set_flatsize_from_node},
	{ "licenselogic", -1, YAML_SCALAR_NODE, pkg_set_licenselogic_from_node},
	{ "licenses", PKG_LICENSES, YAML_SEQUENCE_NODE, parse_sequence},
	{ "desc", PKG_DESC, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "scripts", PKG_SCRIPTS, YAML_MAPPING_NODE, parse_mapping},
	{ "message", PKG_MESSAGE, YAML_SCALAR_NODE, pkg_set_from_node},
	{ "categories", PKG_CATEGORIES, YAML_SEQUENCE_NODE, parse_sequence},
	{ "options", PKG_OPTIONS, YAML_MAPPING_NODE, parse_mapping},
	{ "users", PKG_USERS, YAML_SEQUENCE_NODE, parse_sequence},
	{ "groups", PKG_GROUPS, YAML_SEQUENCE_NODE, parse_sequence},
	{ NULL, -99, -99, NULL}
};

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
	while (val->data.scalar.length > 0 &&
			val->data.scalar.value[val->data.scalar.length - 1] == '\n') {
		val->data.scalar.value[val->data.scalar.length - 1] = '\0';
		val->data.scalar.length--;
	}

	return (pkg_set(pkg, attr, val->data.scalar.value));
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

	return (pkg_setflatsize(pkg, flatsize));
}
static int
pkg_set_licenselogic_from_node(struct pkg *pkg, yaml_node_t *val, __unused yaml_document_t *doc, __unused int attr)
{
	if (!strcmp(val->data.scalar.value, "single"))
		pkg_set_licenselogic(pkg, LICENSE_SINGLE);
	else if ( !strcmp(val->data.scalar.value, "and") || !strcmp(val->data.scalar.value, "dual"))
		pkg_set_licenselogic(pkg, LICENSE_AND);
	else if ( !strcmp(val->data.scalar.value, "or") || !strcmp(val->data.scalar.value, "multi"))
		pkg_set_licenselogic(pkg, LICENSE_OR);
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
			case PKG_CONFLICTS:
				if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0)
					pkg_emit_error("Skipping malformed conflict");
				else
					pkg_addconflict(pkg, val->data.scalar.value);
				break;
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
				if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0)
					pkg_emit_error("Skipping malformed license");
				else
					pkg_adduser(pkg, val->data.scalar.value);
				break;
			case PKG_GROUPS:
				if (val->type != YAML_SCALAR_NODE || val->data.scalar.length <= 0)
					pkg_emit_error("Skipping malformed license");
				else
					pkg_addgroup(pkg, val->data.scalar.value);
				break;
			case PKG_DIRS:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0)
					pkg_adddir(pkg, val->data.scalar.value);
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
			case PKG_FILES:
				if (val->type == YAML_SCALAR_NODE && val->data.scalar.length > 0)
					pkg_addfile(pkg, key->data.scalar.value, val->data.scalar.length == 64 ? val->data.scalar.value : NULL);
				else if (val->type == YAML_MAPPING_NODE)
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

				pkg_addscript(pkg, val->data.scalar.value, script_type);
				break;
		}

		++pair;
	}
	return (EPKG_OK);
}

static int
pkg_set_files_from_node(struct pkg *pkg, yaml_node_t *item, yaml_document_t *doc, const char *filename) {
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;
	const char *sum = NULL;
	const char *uname = NULL;
	const char *gname = NULL;
	void *set;
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
		} else {
			pkg_emit_error("Skipping unknown key for dir(%s): %s", dirname,
						   key->data.scalar.value);
		}

		++pair;
	}

	pkg_adddir_attr(pkg, dirname, uname, gname, perm);

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
				} else {
					pkg_emit_error("Unsupported format for key: %s",
								   key->data.scalar.value);
					retcode = EPKG_FATAL;
				}
				break;
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
	struct pkg_conflict *conflict = NULL;
	struct pkg_option *option = NULL;
	struct pkg_file *file = NULL;
	struct pkg_dir *dir = NULL;
	struct pkg_script *script = NULL;
	struct pkg_category *category = NULL;
	struct pkg_license *license = NULL;
	struct pkg_user *user = NULL;
	struct pkg_group *group = NULL;
	int rc = EPKG_OK;
	int mapping;
	int seq = -1;
	int depsmap = -1;
	int depkv;
	int files = -1;
	int options = -1;
	int scripts = -1;
	const char *script_types;
	struct sbuf *destbuf = sbuf_new_auto();

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_output(&emitter, yaml_write_buf, destbuf);

#define manifest_append_kv(map, key, val) yaml_document_append_mapping_pair(&doc, map, \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, key), wcslen(__DECONST(wchar_t*,key)), YAML_PLAIN_SCALAR_STYLE), \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, val), wcslen(__DECONST(wchar_t*,val)), YAML_PLAIN_SCALAR_STYLE));

#define manifest_append_kv_literal(map, key, val) yaml_document_append_mapping_pair(&doc, map, \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, key), wcslen(__DECONST(wchar_t*,key)), YAML_PLAIN_SCALAR_STYLE), \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, val), wcslen(__DECONST(wchar_t*,val)), YAML_LITERAL_SCALAR_STYLE));

	yaml_document_initialize(&doc, NULL, NULL, NULL, 1, 1);
	mapping = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);

	manifest_append_kv(mapping, "name", pkg_get(pkg, PKG_NAME));
	manifest_append_kv(mapping, "version", pkg_get(pkg, PKG_VERSION));
	manifest_append_kv(mapping, "origin", pkg_get(pkg, PKG_ORIGIN));
	manifest_append_kv(mapping, "comment", pkg_get(pkg, PKG_COMMENT));
	manifest_append_kv(mapping, "arch", pkg_get(pkg, PKG_ARCH));
	manifest_append_kv(mapping, "osversion", pkg_get(pkg, PKG_OSVERSION));
	manifest_append_kv(mapping, "www", pkg_get(pkg, PKG_WWW));
	manifest_append_kv(mapping, "maintainer", pkg_get(pkg, PKG_MAINTAINER));
	manifest_append_kv(mapping, "prefix", pkg_get(pkg, PKG_PREFIX));
	switch (pkg_licenselogic(pkg)) {
		case LICENSE_SINGLE:
			manifest_append_kv(mapping, "licenselogic", "single");
			break;
		case LICENSE_AND:
			manifest_append_kv(mapping, "licenselogic", "and");
			break;
		case LICENSE_OR:
			manifest_append_kv(mapping, "licenselogic", "or");
			break;
	}

	seq = -1;
	while (pkg_licenses(pkg, &license) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "licenses", pkg_license_name(license));

	snprintf(tmpbuf, BUFSIZ, "%" PRId64, pkg_flatsize(pkg));
	manifest_append_kv(mapping, "flatsize", tmpbuf);
	manifest_append_kv_literal(mapping, "desc", pkg_get(pkg, PKG_DESC));

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (depsmap == -1) {
			depsmap = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "deps"), 4, YAML_PLAIN_SCALAR_STYLE),
					depsmap);
		}

		depkv = yaml_document_add_mapping(&doc, NULL, YAML_FLOW_MAPPING_STYLE);
		yaml_document_append_mapping_pair(&doc, depsmap,
				yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, pkg_dep_name(dep)), strlen(pkg_dep_name(dep)), YAML_PLAIN_SCALAR_STYLE),
				depkv);

		manifest_append_kv(depkv, "origin", pkg_dep_origin(dep));
		manifest_append_kv(depkv, "version", pkg_dep_version(dep));
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

	seq = -1;
	while (pkg_conflicts(pkg, &conflict) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "conflicts", pkg_conflict_glob(conflict));

	while (pkg_options(pkg, &option) == EPKG_OK) {
		if (options == -1) {
			options = yaml_document_add_mapping(&doc, NULL, YAML_FLOW_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "options"), 7, YAML_PLAIN_SCALAR_STYLE),
					options);
		}
		manifest_append_kv(options, pkg_option_opt(option), pkg_option_value(option));
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (files == -1) {
			files = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "files"), 5, YAML_PLAIN_SCALAR_STYLE),
					files);
		}
		manifest_append_kv(files, pkg_file_path(file), pkg_file_sha256(file) && strlen(pkg_file_sha256(file)) > 0 ? pkg_file_sha256(file) : "-");
	}

	seq = -1;
	while (pkg_dirs(pkg, &dir) == EPKG_OK)
		manifest_append_seqval(&doc, mapping, &seq, "dirs", pkg_dir_path(dir));

	while (pkg_scripts(pkg, &script) == EPKG_OK) {
		if (scripts == -1) {
			scripts = yaml_document_add_mapping(&doc, NULL, YAML_BLOCK_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping, 
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "scripts"), 7, YAML_PLAIN_SCALAR_STYLE),
					scripts);
		}
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
		manifest_append_kv_literal(scripts, script_types, pkg_script_data(script));
	}
	if (pkg_get(pkg, PKG_MESSAGE) != NULL && pkg_get(pkg, PKG_MESSAGE)[0] != '\0')
		manifest_append_kv_literal(mapping, "message", pkg_get(pkg, PKG_MESSAGE));

	if (!yaml_emitter_dump(&emitter, &doc))
		rc = EPKG_FATAL;

	sbuf_finish(destbuf);
	*dest = strdup(sbuf_data(destbuf));
	sbuf_delete(destbuf);

	yaml_emitter_delete(&emitter);
	return (rc);

}
