#include <sys/types.h>

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

#include "pkg.h"
#include "pkg_event.h"

static struct _config {
	const char *key;
	const char *def;
	const char *val;
} c[] = {
	{ "PACKAGESITE", NULL, NULL},
	{ "PKG_DBDIR", "/var/db/pkg", NULL},
	{ "PKG_CACHEDIR", "/var/cache/pkg", NULL},
	{ "PORTSDIR", "/usr/ports", NULL },
	{ "PUBKEY", "/etc/ssl/pkg.pub", NULL },
	{ "PKG_MULTIREPOS", NULL, NULL },
	{ "HANDLE_RC_SCRIPTS", NULL, NULL },
	{ "ASSUME_ALWAYS_YES", NULL, NULL }, 
	{ NULL, NULL, NULL}
};

struct _pkg_config {
	yaml_parser_t parser;
	yaml_document_t doc;
	struct _config *c;
};

static struct _pkg_config *conf = NULL;

static void
parse_configuration(yaml_node_t *node)
{
	int i;
	yaml_node_pair_t *pair;
	yaml_node_t *key;
	yaml_node_t *val;

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		key = yaml_document_get_node(&conf->doc, pair->key);
		val = yaml_document_get_node(&conf->doc, pair->value);

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
		for (i = 0; conf->c[i].key != NULL; i++) {
			if (!strcasecmp(key->data.scalar.value, conf->c[i].key)) {
				if (conf->c[i].val != NULL) {
					/* skip env var already set */
					++pair;
					continue;
				}
				conf->c[i].val = val->data.scalar.value;
				break;
			}
		}
		/* unknown values are just silently skipped */
		++pair;
	}
}

const char *
pkg_config(const char *key)
{
	int i;

	assert(conf != NULL);

	for (i = 0; conf->c[i].key != NULL; i++) {
		if (strcmp(conf->c[i].key, key) == 0) {
			if (conf->c[i].val != NULL)
				return (conf->c[i].val);
			else
				return (conf->c[i].def);
		}
	}

	return (NULL);
}

int
pkg_init(const char *path)
{
	FILE *conffile;
	yaml_node_t *node;
	int i;

	assert(conf == NULL);

	conf = malloc(sizeof(struct _pkg_config));
	conf->c = c;

	/* first fill with environment variables */

	for (i = 0; conf->c[i].key != NULL; i++)
		conf->c[i].val = getenv(conf->c[i].key);

	if (path == NULL)
		path = "/etc/pkg.conf";

	conffile = fopen(path, "r");

	if (conffile == NULL)
		return (EPKG_OK);

	yaml_parser_initialize(&conf->parser);
	yaml_parser_set_input_file(&conf->parser, conffile);
	yaml_parser_load(&conf->parser, &conf->doc);

	node = yaml_document_get_root_node(&conf->doc);
	if (node != NULL) {
		if (node->type != YAML_MAPPING_NODE) {
			pkg_emit_error("Invalid configuration format, ignoring the configuration file");
		} else {
			parse_configuration(node);
		}
	} else {
		pkg_emit_error("Invalid configuration format, ignoring the configuration file");
	}

	return (EPKG_OK);
}

int
pkg_shutdown(void)
{
	assert(conf != NULL);

	yaml_document_delete(&conf->doc);
	yaml_parser_delete(&conf->parser);

	free(conf);

	return (EPKG_OK);
}
