#include <sys/types.h>
#include <sys/sbuf.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_util.h"
#include "pkg_private.h"

#define PKG_UNKNOWN -1
#define PKG_DEPS -2
#define PKG_CONFLICTS -3
#define PKG_FILES -4
#define PKG_DIRS -5
#define PKG_FLATSIZE -6

static void parse_mapping(struct pkg *, yaml_node_pair_t *, yaml_document_t *, int);
static void parse_node(struct pkg *, yaml_node_t *, yaml_document_t *, int);

static struct manifest_key {
	const char *key;
	int type;
} manifest_key[] = {
	{ "name", PKG_NAME },
	{ "origin", PKG_ORIGIN },
	{ "version", PKG_VERSION },
	{ "arch", PKG_ARCH },
	{ "osversion", PKG_OSVERSION },
	{ "www", PKG_WWW },
	{ "comment", PKG_COMMENT},
	{ "maintainer", PKG_MAINTAINER},
	{ "prefix", PKG_PREFIX},
	{ "deps", PKG_DEPS},
	{ "conflicts", PKG_CONFLICTS},
	{ "files", PKG_FILES},
	{ "dirs", PKG_DIRS},
	{ "flatsize", PKG_FLATSIZE},
	{ "desc", PKG_FLATSIZE },
};

#define manifest_key_len (int)(sizeof(manifest_key)/sizeof(manifest_key[0]))

static int manifest_type(const char *key) {
	int i = 0;
	
	for (i = 0; i < manifest_key_len; i++) {
		if (!strcasecmp(key, manifest_key[i].key))
			return (manifest_key[i].type);
	}

	return (PKG_UNKNOWN);
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
	if (ret != EPKG_OK && manifest != NULL)
			free(manifest);

	return (ret);
}

static void
parse_mapping(struct pkg *pkg, yaml_node_pair_t *pair, yaml_document_t *document, int pkgtype)
{
	int type;
	yaml_node_t *key, *subkey;
	yaml_node_t *val, *subval;
	yaml_node_pair_t *subpair;
	char origin[BUFSIZ];
	char version[BUFSIZ];

	key = yaml_document_get_node(document, pair->key);
	val = yaml_document_get_node(document, pair->value);

	switch (pkgtype) {
		case PKG_FILES:
			pkg_addfile(pkg, key->data.scalar.value, val->data.scalar.length == 65 ? val->data.scalar.value : NULL);
			break;
		case PKG_DEPS:
			subpair = val->data.mapping.pairs.start;
			while (subpair < val->data.mapping.pairs.top) {
				subkey = yaml_document_get_node(document, subpair->key);
				subval = yaml_document_get_node(document, subpair->value);
				if (!strcasecmp(subkey->data.scalar.value, "origin"))
					strlcpy(origin, subval->data.scalar.value, BUFSIZ);
				else if (!strcasecmp(subkey->data.scalar.value, "version"))
					strlcpy(version, subval->data.scalar.value, BUFSIZ);
				else
					warnx("Ignoring key: (%s: %s) for dependency %s",subkey->data.scalar.value,subval->data.scalar.value, key->data.scalar.value);
				++subpair;
			}
			pkg_adddep(pkg, key->data.scalar.value, origin, version);
			break;
		default:
			type = manifest_type(key->data.scalar.value);
			if (type == -1) {
				if (val->type == YAML_SCALAR_NODE)
					warnx("Unknown line: (%s: %s)\n", key->data.scalar.value, val->data.scalar.value);
				else
					warnx("Unknown key: (%s)\n", key->data.scalar.value);
				++pair;
				break;
			}
			if (val->type == YAML_SCALAR_NODE) {
				type = manifest_type(key->data.scalar.value);
				if (type == -1) {
					warnx("Unknown line: (%s: %s)\n", key->data.scalar.value, val->data.scalar.value);
				} else if (type == PKG_FLATSIZE)
					pkg_setflatsize(pkg, strtoimax(val->data.scalar.value, NULL, 10));
				else
					pkg_set(pkg, type, val->data.scalar.value);
			} else {
				parse_node(pkg, val, document, type);
			}
			break;
	}
}

static void
parse_node(struct pkg *pkg, yaml_node_t *node, yaml_document_t *document, int pkgtype)
{
	yaml_node_pair_t *pair;
	yaml_node_item_t *item;
	yaml_node_t *nd;

	switch (node->type) {
		case YAML_SCALAR_NODE:
			/* NOT REACHED THERE SHOULD NOT BE ALONE SCALARS */
			printf("%s\n", (char *)node->data.scalar.value);
			break;
		case YAML_SEQUENCE_NODE:
			switch (pkgtype) {
				case PKG_DIRS:
					item = node->data.sequence.items.start;
					while (item < node->data.sequence.items.top) {
						nd = yaml_document_get_node(document, *item);
						pkg_adddir(pkg, nd->data.scalar.value);
						++item;
					}

					break;
				case PKG_CONFLICTS:
					item = node->data.sequence.items.start;
					while (item < node->data.sequence.items.top) {
						nd = yaml_document_get_node(document, *item);
						pkg_addconflict(pkg, nd->data.scalar.value);
						++item;
					}
					break;
			}
			break;
		case YAML_MAPPING_NODE:
			pair = node->data.mapping.pairs.start;
			while (pair < node->data.mapping.pairs.top) {
				parse_mapping(pkg, pair, document, pkgtype);
				++pair;
			}
			break;
		case YAML_NO_NODE:
			break;
	}
}

int
pkg_parse_manifest(struct pkg *pkg, char *buf)
{
	yaml_parser_t parser;
	yaml_document_t doc;

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_string(&parser, buf, strlen(buf));
	yaml_parser_load(&parser, &doc);

	parse_node(pkg, yaml_document_get_root_node(&doc), &doc, -1);

	yaml_parser_delete(&parser);

	return EPKG_OK;
}

static int
yaml_write_buf(void *data, unsigned char *buffer, size_t size)
{
	char **dest = (char **)data;
	*dest = malloc(size);
	strlcpy(*dest, buffer, size);
	return (1);
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
	int rc = EPKG_OK;
	int mapping;
	int depsmap = -1;
	int depkv;
	int conflicts = -1;
	int files = -1;
	int dirs = -1;
	int options = -1;

	yaml_emitter_initialize(&emitter);
	yaml_emitter_set_output(&emitter, yaml_write_buf, dest);

#define manifest_append_kv(map, key, val) yaml_document_append_mapping_pair(&doc, map, \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, key), strlen(key), YAML_ANY_SCALAR_STYLE), \
		yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, val), strlen(val), YAML_ANY_SCALAR_STYLE));

	yaml_document_initialize(&doc, NULL, NULL, NULL, 1, 1);
	mapping = yaml_document_add_mapping(&doc, NULL, YAML_ANY_MAPPING_STYLE);

	manifest_append_kv(mapping, "name", pkg_get(pkg, PKG_NAME));
	manifest_append_kv(mapping, "version", pkg_get(pkg, PKG_VERSION));
	manifest_append_kv(mapping, "origin", pkg_get(pkg, PKG_ORIGIN));
	manifest_append_kv(mapping, "comment", pkg_get(pkg, PKG_COMMENT));
	manifest_append_kv(mapping, "arch", pkg_get(pkg, PKG_ARCH));
	manifest_append_kv(mapping, "osversion", pkg_get(pkg, PKG_OSVERSION));
	manifest_append_kv(mapping, "www", pkg_get(pkg, PKG_WWW));
	manifest_append_kv(mapping, "maintainer", pkg_get(pkg, PKG_MAINTAINER));
	manifest_append_kv(mapping, "prefix", pkg_get(pkg, PKG_PREFIX));
	snprintf(tmpbuf, BUFSIZ, "%ld", pkg_flatsize(pkg));
	manifest_append_kv(mapping, "flatsize", tmpbuf);

	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (depsmap == -1) {
			depsmap = yaml_document_add_mapping(&doc, NULL, YAML_ANY_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "deps"), 4, YAML_ANY_SCALAR_STYLE),
					depsmap);
		}

		depkv = yaml_document_add_mapping(&doc, NULL, YAML_ANY_MAPPING_STYLE);
		yaml_document_append_mapping_pair(&doc, depsmap,
				yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, pkg_dep_name(dep)), strlen(pkg_dep_name(dep)), YAML_ANY_SCALAR_STYLE),
				depkv);

		manifest_append_kv(depkv, "origin", pkg_dep_origin(dep));
		manifest_append_kv(depkv, "version", pkg_dep_version(dep));
	}

	while (pkg_conflicts(pkg, &conflict) == EPKG_OK) {
		if (conflicts == -1) {
			conflicts = yaml_document_add_sequence(&doc, NULL, YAML_ANY_SEQUENCE_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "conflicts"), 9, YAML_ANY_SCALAR_STYLE),
					conflicts);
		}
		yaml_document_append_sequence_item(&doc, conflicts,
				yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, pkg_conflict_glob(conflict)), strlen(pkg_conflict_glob(conflict)), YAML_ANY_SCALAR_STYLE));
	}

	while (pkg_options(pkg, &option) == EPKG_OK) {
		if (options == -1) {
			options = yaml_document_add_mapping(&doc, NULL, YAML_ANY_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "options"), 7, YAML_ANY_SCALAR_STYLE),
					options);
		}
		manifest_append_kv(files, pkg_option_opt(option), pkg_option_value(option));
	}

	while (pkg_files(pkg, &file) == EPKG_OK) {
		if (files == -1) {
			files = yaml_document_add_mapping(&doc, NULL, YAML_ANY_MAPPING_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "files"), 5, YAML_ANY_SCALAR_STYLE),
					files);
		}
		manifest_append_kv(files, pkg_file_path(file), pkg_file_sha256(file) && strlen(pkg_file_sha256(file)) > 0 ? pkg_file_sha256(file) : "-");
	}

	while (pkg_dirs(pkg, &dir) == EPKG_OK) {
		if (dirs == -1) {
			dirs = yaml_document_add_sequence(&doc, NULL, YAML_ANY_SEQUENCE_STYLE);
			yaml_document_append_mapping_pair(&doc, mapping,
					yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, "dirs"), 4, YAML_ANY_SCALAR_STYLE),
					dirs);
		}
		yaml_document_append_sequence_item(&doc, dirs,
				yaml_document_add_scalar(&doc, NULL, __DECONST(yaml_char_t*, pkg_dir_path(dir)), strlen(pkg_dir_path(dir)), YAML_ANY_SCALAR_STYLE));
	}

	if (!yaml_emitter_dump(&emitter, &doc))
		rc = EPKG_FATAL;

	yaml_emitter_delete(&emitter);
	return (rc);

}
