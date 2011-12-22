#include <sys/types.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <yaml.h>

#include "pkg.h"
#include "pkg_event.h"

#define STRING 0
#define BOOL 1
#define LIST 2

struct pkg_config_kv {
	char *key;
	char *value;
	STAILQ_ENTRY(pkg_config_kv) next;
};

struct config_entry {
	uint8_t type;
	const char *key;
	const char *def;
	union {
		char *val;
		STAILQ_HEAD(, pkg_config_kv) list;
	};
};

static struct config_entry c[] = {
	[PKG_CONFIG_REPO] = {
		STRING,
		"PACKAGESITE",
		NULL,
		{ NULL }
	},
	[PKG_CONFIG_DBDIR] = {
		STRING,
		"PKG_DBDIR",
		"/var/db/pkg",
		{ NULL }
	},
	[PKG_CONFIG_CACHEDIR] = {
		STRING,
		"PKG_CACHEDIR",
		"/var/cache/pkg",
		{ NULL }
	},
	[PKG_CONFIG_PORTSDIR] = {
		STRING,
		"PORTSDIR",
		"/usr/ports",
		{ NULL }
	},
	[PKG_CONFIG_REPOKEY] = {
		STRING,
		"PUBKEY",
		"/etc/ssl/pkg.pub",
		{ NULL }
	},
	[PKG_CONFIG_MULTIREPOS] = {
		BOOL,
		"PKG_MULTIREPOS",
		NULL,
		{ NULL }
	},
	[PKG_CONFIG_HANDLE_RC_SCRIPTS] = {
		BOOL,
		"HANDLE_RC_SCRIPTS",
		NULL,
		{ NULL }
	},
	[PKG_CONFIG_ASSUME_ALWAYS_YES] = {
		BOOL,
		"ASSUME_ALWAYS_YES",
		NULL,
		{ NULL }
	},
	[PKG_CONFIG_REPOS] = {
		LIST,
		"REPOS",
		"NULL",
		{ NULL }
	}
};

static bool parsed = false;
static size_t c_size = sizeof(c) / sizeof(struct config_entry);

static void
parse_configuration(yaml_document_t *doc, yaml_node_t *node)
{
	size_t i;
	struct pkg_config_kv *kv;
	yaml_node_pair_t *pair, *subpair;
	yaml_node_t *key, *subkey;
	yaml_node_t *val, *subval;

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		key = yaml_document_get_node(doc, pair->key);
		val = yaml_document_get_node(doc, pair->value);

		if (key->data.scalar.length <= 0) {
			/* ignoring silently */
			++pair;
			continue;
		}

		if (val->type == YAML_NO_NODE || ( val->type == YAML_SCALAR_NODE && val->data.scalar.length <= 0)) {
			/* silently skip on purpose */
			++pair;
			continue;
		}
		for (i = 0; i < c_size; i++) {
			if (!strcasecmp(key->data.scalar.value, c[i].key)) {
				if (c[i].val != NULL) {
					/* skip env var already set */
					++pair;
					continue;
				}
				if (val->type == YAML_SCALAR_NODE) {
					c[i].val = strdup(val->data.scalar.value);
				} else if (val->type == YAML_MAPPING_NODE) {
					subpair = val->data.mapping.pairs.start;
					STAILQ_INIT(&c[i].list);
					while (subpair < val->data.mapping.pairs.top) {
						subkey = yaml_document_get_node(doc, subpair->key);
						subval = yaml_document_get_node(doc, subpair->value);
						if (subkey->type != YAML_SCALAR_NODE || subval->type != YAML_SCALAR_NODE) {
							++subpair;
							continue;
						}
						kv = malloc(sizeof(struct pkg_config_kv));
						kv->key = strdup(subkey->data.scalar.value);
						kv->value = strdup(subval->data.scalar.value);
						STAILQ_INSERT_TAIL(&(c[i].list), kv, next);
						++subpair;
					}
				}
				break;
			}
		}
		/* unknown values are just silently skipped */
		++pair;
	}
}

int
pkg_config_string(pkg_config_key key, const char **val)
{
	*val = NULL;

	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_string()");
		return (EPKG_FATAL);
	}

	if (c[key].type != STRING) {
		pkg_emit_error("this config entry is not a string");
		return (EPKG_FATAL);
	}

	*val = c[key].val;

	if (*val == NULL)
		*val = c[key].def;

	if (*val == NULL)
		return (EPKG_FATAL);

	return (EPKG_OK);
}

int
pkg_config_bool(pkg_config_key key, bool *val)
{
	*val = false;

	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_bool()");
		return (EPKG_FATAL);
	}

	if (c[key].type != BOOL) {
		pkg_emit_error("this config entry is not a bool");
		return (EPKG_FATAL);
	}

	if (c[key].val != NULL && (
	    strcasecmp(c[key].val, "yes") == 0 ||
	    strcasecmp(c[key].val, "true" ) == 0 ||
	    strcasecmp(c[key].val, "on" ) == 0))
		*val = true;

	return (EPKG_OK);
}

int
pkg_config_list(pkg_config_key key, struct pkg_config_kv **kv)
{
	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_list()");
		return (EPKG_FATAL);
	}

	if (c[key].type != LIST) {
		pkg_emit_error("this config entry is not a list");
		return (EPKG_FATAL);
	}

	if (*kv == NULL) {
		*kv = STAILQ_FIRST(&(c[key].list));
	} else {
		*kv = STAILQ_NEXT(*kv, next);
	}

	if (*kv == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

const char *
pkg_config_kv_get(struct pkg_config_kv *kv, pkg_config_kv_t type)
{
	assert(kv != NULL);

	switch (type) {
		case PKG_CONFIG_KV_KEY:
			return (kv->key);
			break;
		case PKG_CONFIG_KV_VALUE:
			return (kv->value);
			break;
	}
	return (NULL);
}

int
pkg_init(const char *path)
{
	FILE *fp;
	yaml_parser_t parser;
	yaml_document_t doc;
	yaml_node_t *node;
	size_t i;
	const char *val = NULL;

	if (parsed != false) {
		pkg_emit_error("pkg_init() must only be called once");
		return (EPKG_FATAL);
	}

	/* first fill with environment variables */
	for (i = 0; i < c_size; i++) {
		val = getenv(c[i].key);

		if (val != NULL)
			c[i].val = strdup(val);
	}

	if (path == NULL)
		path = "/etc/pkg.conf";

	if ((fp = fopen(path, "r")) == NULL) {
		if (errno != ENOENT) {
			pkg_emit_errno("fopen", path);
			return (EPKG_FATAL);
		}
		/* no configuration present */
		parsed = true;
		return (EPKG_OK);
	}

	yaml_parser_initialize(&parser);
	yaml_parser_set_input_file(&parser, fp);
	yaml_parser_load(&parser, &doc);

	node = yaml_document_get_root_node(&doc);
	if (node != NULL) {
		if (node->type != YAML_MAPPING_NODE) {
			pkg_emit_error("Invalid configuration format, ignoring the configuration file");
		} else {
			parse_configuration(&doc, node);
		}
	} else {
		pkg_emit_error("Invalid configuration format, ignoring the configuration file");
	}

	yaml_document_delete(&doc);
	yaml_parser_delete(&parser);

	parsed = true;
	return (EPKG_OK);
}

int
pkg_shutdown(void)
{
	size_t i;

	if (parsed == true) {
		for (i = 0; i < c_size; i++) {
			switch (c[i].type) {
			case STRING:
			case BOOL:
				free(c[i].val);
				break;
			case LIST:
				break;
			default:
				err(1, "unknown config entry type");
			}
		}
	} else {
		pkg_emit_error("pkg_shutdown() must be called after pkg_init()");
		return (EPKG_FATAL);
	}

	parsed = false;

	return (EPKG_OK);
}
