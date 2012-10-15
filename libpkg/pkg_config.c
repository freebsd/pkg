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

#include <sys/queue.h>

#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <yaml.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

#define ABI_VAR_STRING "${ABI}"

struct config_entry {
	uint8_t type;
	const char *key;
	const char *def;
};

static char myabi[BUFSIZ];
static struct pkg_config *config = NULL;
static struct pkg_config *config_by_key = NULL;

static struct config_entry c[] = {
	[PKG_CONFIG_REPO] = {
		CONF_STRING,
		"PACKAGESITE",
		NULL,
	},
	[PKG_CONFIG_DBDIR] = {
		CONF_STRING,
		"PKG_DBDIR",
		"/var/db/pkg",
	},
	[PKG_CONFIG_CACHEDIR] = {
		CONF_STRING,
		"PKG_CACHEDIR",
		"/var/cache/pkg",
	},
	[PKG_CONFIG_PORTSDIR] = {
		CONF_STRING,
		"PORTSDIR",
		"/usr/ports",
	},
	[PKG_CONFIG_REPOKEY] = {
		CONF_STRING,
		"PUBKEY",
		NULL,
	},
	[PKG_CONFIG_MULTIREPOS] = {
		CONF_BOOL,
		"PKG_MULTIREPOS",
		NULL,
	},
	[PKG_CONFIG_HANDLE_RC_SCRIPTS] = {
		CONF_BOOL,
		"HANDLE_RC_SCRIPTS",
		NULL,
	},
	[PKG_CONFIG_ASSUME_ALWAYS_YES] = {
		CONF_BOOL,
		"ASSUME_ALWAYS_YES",
		NULL,
	},
	[PKG_CONFIG_REPOS] = {
		CONF_KVLIST,
		"REPOS",
		"NULL",
	},
	[PKG_CONFIG_PLIST_KEYWORDS_DIR] = {
		CONF_STRING,
		"PLIST_KEYWORDS_DIR",
		NULL,
	},
	[PKG_CONFIG_SYSLOG] = {
		CONF_BOOL,
		"SYSLOG",
		"YES",
	},
	[PKG_CONFIG_SHLIBS] = {
		CONF_BOOL,
		"SHLIBS",
		"NO",
	},
	[PKG_CONFIG_AUTODEPS] = {
		CONF_BOOL,
		"AUTODEPS",
		"NO",
	},
	[PKG_CONFIG_ABI] = {
		CONF_STRING,
		"ABI",
		myabi,
	},
	[PKG_CONFIG_DEVELOPER_MODE] = {
		CONF_BOOL,
		"DEVELOPER_MODE",
		"NO",
	},
	[PKG_CONFIG_PORTAUDIT_SITE] = {
		CONF_STRING,
		"PORTAUDIT_SITE",
		"http://portaudit.FreeBSD.org/auditfile.tbz",
	},
	[PKG_CONFIG_SRV_MIRROR] = {
		CONF_BOOL,
		"SRV_MIRRORS",
		"YES",
	},
	[PKG_CONFIG_FETCH_RETRY] = {
		CONF_INTEGER,
		"FETCH_RETRY",
		"3",
	},
	[PKG_CONFIG_PLUGINS_DIR] = {
		CONF_STRING,
		"PKG_PLUGINS_DIR",
		PREFIX"/lib/pkg/",
	},
	[PKG_CONFIG_ENABLE_PLUGINS] = {
		CONF_BOOL,
		"PKG_ENABLE_PLUGINS",
		"YES",
	},
	[PKG_CONFIG_PLUGINS] = {
		CONF_LIST,
		"PLUGINS",
		"NULL",
	},
	[PKG_CONFIG_DEBUG_SCRIPTS] = {
		CONF_BOOL,
		"DEBUG_SCRIPTS",
		"NO",
	},
	[PKG_CONFIG_PLUGINS_CONF_DIR] = {
		CONF_STRING,
		"PLUGINS_CONF_DIR",
		PREFIX"/etc/pkg/",
	},
};

static bool parsed = false;
static size_t c_size = sizeof(c) / sizeof(struct config_entry);

static void
parse_config_sequence(yaml_document_t *doc, yaml_node_t *seq, struct pkg_config *conf)
{
	yaml_node_item_t *item = seq->data.sequence.items.start;
	yaml_node_t *val;
	struct pkg_config_value *v;

	while (item < seq->data.sequence.items.top) {
		val = yaml_document_get_node(doc, *item);
		if (val->type != YAML_SCALAR_NODE) {
			++item;
			continue;
		}
		v = malloc(sizeof(struct pkg_config_value));
		v->value = strdup(val->data.scalar.value);
		STAILQ_INSERT_TAIL(&conf->list, v, next);
		++item;
	}
}

static void
parse_config_mapping(yaml_document_t *doc, yaml_node_t *map, struct pkg_config *conf)
{
	yaml_node_pair_t *subpair = map->data.mapping.pairs.start;
	yaml_node_t *subkey, *subval;
	struct pkg_config_kv *kv;

	while (subpair < map->data.mapping.pairs.top) {
		subkey = yaml_document_get_node(doc, subpair->key);
		subval = yaml_document_get_node(doc, subpair->value);
		if (subkey->type != YAML_SCALAR_NODE ||
		    subval->type != YAML_SCALAR_NODE) {
			++subpair;
			continue;
		}
		kv = malloc(sizeof(struct pkg_config_kv));
		kv->key = strdup(subkey->data.scalar.value);
		kv->value = strdup(subval->data.scalar.value);
		STAILQ_INSERT_TAIL(&conf->kvlist, kv, next);
		++subpair;
	}
}

void
pkg_config_parse(yaml_document_t *doc, yaml_node_t *node, struct pkg_config *conf_by_key)
{
	struct pkg_config *conf;
	yaml_node_pair_t *pair;
	const char *errstr = NULL;
	int64_t newint;
	struct sbuf *b = sbuf_new_auto();

	pair = node->data.mapping.pairs.start;
	while (pair < node->data.mapping.pairs.top) {
		yaml_node_t *key = yaml_document_get_node(doc, pair->key);
		yaml_node_t *val = yaml_document_get_node(doc, pair->value);

		if (key->data.scalar.length <= 0) {
			/*
			 * ignoring silently empty keys can be empty lines or user mistakes
			 */
			++pair;
			continue;
		}

		if (val->type == YAML_NO_NODE ||
		    (val->type == YAML_SCALAR_NODE &&
		     val->data.scalar.length <= 0)) {
			/*
			 * silently skip on purpose to allow user to leave
			 * empty lines for examples without complaining
			 */
			++pair;
			continue;
		}
		sbuf_clear(b);
		for (size_t i = 0; i < strlen(key->data.scalar.value); ++i)
			sbuf_putc(b, toupper(key->data.scalar.value[i]));

		sbuf_finish(b);
		HASH_FIND(hhkey, conf_by_key, sbuf_data(b), sbuf_len(b), conf);
		if (conf != NULL) {
			switch (conf->type) {
			case CONF_STRING:
				if (val->type != YAML_SCALAR_NODE) {
					pkg_emit_error("Expecting a string for key %s,"
					    " ignoring...", key->data.scalar.value);
				}
				/* ignore if already set from env */
				if (!conf->fromenv) {
					free(conf->string);
					conf->string = strdup(val->data.scalar.value);
				}
				break;
			case CONF_INTEGER:
				if (val->type != YAML_SCALAR_NODE) {
					pkg_emit_error("Expecting an integer for key %s,"
					    " ignoring...", key->data.scalar.value);
				}
				/* ignore if already set from env */
				if (!conf->fromenv) {
					newint = strtonum(val->data.scalar.value, 0, INT64_MAX, &errstr);
					if (errstr != NULL) {
						pkg_emit_error("Expecting an integer for key %s"
						    " ignoring...", key->data.scalar.value);
					}
					conf->integer = newint;
				}
				break;
			case CONF_BOOL:
				if (val->type != YAML_SCALAR_NODE) {
					pkg_emit_error("Expecting an integer for key %s,"
					    " ignoring...", key->data.scalar.value);
				}
				/* ignore if already set from env */
				if (!conf->fromenv) {
					if (val->data.scalar.value != NULL && (
					    strcmp(val->data.scalar.value, "1") == 0 ||
					    strcasecmp(val->data.scalar.value, "yes") == 0 ||
					    strcasecmp(val->data.scalar.value, "true") == 0 ||
					    strcasecmp(val->data.scalar.value, "on") == 0)) {
						conf->boolean = true;
					} else {
						conf->boolean = false;
					}
				}
				break;
			case CONF_KVLIST:
				if (val->type != YAML_MAPPING_NODE) {
					pkg_emit_error("Expecting a key/value list for key %s,"
					    " ignoring...", key->data.scalar.value);
				}
				parse_config_mapping(doc, val, conf);
				break;
			case CONF_LIST:
				if (val->type != YAML_SEQUENCE_NODE) {
					pkg_emit_error("Expecting a string list for key %s,"
					    " ignoring...", key->data.scalar.value);
				}
				parse_config_sequence(doc, val, conf);
				break;
			}
		}
		/*
		 * unknown values are just silently ignored, because we don't
		 * care about them
		 */
		++pair;
	}
	sbuf_delete(b);
}

/**
 * @brief Substitute PACKAGESITE variables
 */
static void
subst_packagesite(void)
{
	const char *variable_string;
	const char *oldval;
	const char *myarch;
	struct sbuf *newval;
	struct pkg_config *conf;
	pkg_config_key k = PKG_CONFIG_REPO;

	HASH_FIND_INT(config, &k, conf);
	oldval = conf->string;

	if (oldval == NULL || (variable_string = strstr(oldval, ABI_VAR_STRING)) == NULL)
		return;

	newval = sbuf_new_auto();
	sbuf_bcat(newval, oldval, variable_string - oldval);
	pkg_config_string(PKG_CONFIG_ABI, &myarch);
	sbuf_cat(newval, myarch);
	sbuf_cat(newval, variable_string + strlen(ABI_VAR_STRING));
	sbuf_finish(newval);

	free(conf->string);
	conf->string = strdup(sbuf_data(newval));
	sbuf_free(newval);
}

int
pkg_initialized(void)
{
	return (parsed);
}

int
pkg_config_string(pkg_config_key key, const char **val)
{
	struct pkg_config *conf;

	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_int64()");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(config, &key, conf);
	if (conf == NULL)
		*val = NULL;
	else
		*val = conf->string;

	return (EPKG_OK);
}

int
pkg_config_int64(pkg_config_key key, int64_t *val)
{
	struct pkg_config *conf;

	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_int64()");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(config, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	*val = conf->integer;

	return (EPKG_OK);
}

int
pkg_config_bool(pkg_config_key key, bool *val)
{
	struct pkg_config *conf;

	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_int64()");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(config, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	*val = conf->boolean;

	return (EPKG_OK);
}

int
pkg_config_kvlist(pkg_config_key key, struct pkg_config_kv **kv)
{
	struct pkg_config *conf;

	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_kvlist()");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(config, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	if (conf->type != CONF_KVLIST) {
		pkg_emit_error("this config entry is not a \"key: value\" list");
		return (EPKG_FATAL);
	}

	if (*kv == NULL)
		*kv = STAILQ_FIRST(&(conf->kvlist));
	else
		*kv = STAILQ_NEXT(*kv, next);

	if (*kv == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_config_list(pkg_config_key key, struct pkg_config_value **v)
{
	struct pkg_config *conf;

	if (parsed != true) {
		pkg_emit_error("pkg_init() must be called before pkg_config_list()");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(config, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	if (conf->type != CONF_LIST) {
		pkg_emit_error("this config entry is not a list");
		return (EPKG_FATAL);
	}

	if (*v == NULL)
		*v = STAILQ_FIRST(&conf->list);
	else
		*v = STAILQ_NEXT(*v, next);
	if (*v == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

const char *
pkg_config_value(struct pkg_config_value *v)
{
	assert(v != NULL);

	return (v->value);
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

static void
disable_plugins_if_static(void)
{
	void *dlh;
	struct pkg_config *conf;
	pkg_config_key k = PKG_CONFIG_ENABLE_PLUGINS;

	HASH_FIND_INT(config, &k, conf);

	if (!conf->boolean)
		return;

	dlh = dlopen(0, 0);
	dlclose(dlh);

	/* if dlh is 0 then we are in static binary */
	if (dlh == 0)
		conf->boolean = false;

	return;
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
	const char *errstr = NULL;
	struct pkg_config *conf;

	pkg_get_myarch(myabi, BUFSIZ);
	if (parsed != false) {
		pkg_emit_error("pkg_init() must only be called once");
		return (EPKG_FATAL);
	}

	for (i = 0; i < c_size; i++) {
		conf = malloc(sizeof(struct pkg_config));
		conf->id = i;
		conf->key = c[i].key;
		conf->type = c[i].type;
		conf->fromenv = false;
		val = getenv(c[i].key);

		switch (c[i].type) {
		case CONF_STRING:
			if (val != NULL) {
				conf->string = strdup(val);
				conf->fromenv = true;
			}
			else if (c[i].def != NULL)
				conf->string = strdup(c[i].def);
			else
				conf->string = NULL;
			break;
		case CONF_INTEGER:
			if (val == NULL)
				val = c[i].def;
			else
				conf->fromenv = true;
			conf->integer = strtonum(val, 0, INT64_MAX, &errstr);
			if (errstr != NULL) {
				pkg_emit_error("Unable to convert %s to int64: %s",
				    val, errstr);
				return (EPKG_FATAL);
			}
			break;
		case CONF_BOOL:
			if (val == NULL)
				val = c[i].def;
			else
				conf->fromenv = true;
			if (val != NULL && (
			    strcmp(val, "1") == 0 ||
			    strcasecmp(val, "yes") == 0 ||
			    strcasecmp(val, "true") == 0 ||
			    strcasecmp(val, "on") == 0)) {
				conf->boolean = true;
			} else {
				conf->boolean = false;
			}
			break;
		case CONF_KVLIST:
			STAILQ_INIT(&conf->kvlist);
			break;
		case CONF_LIST:
			STAILQ_INIT(&conf->list);
			break;
		}

		HASH_ADD_INT(config, id, conf);
		HASH_ADD_KEYPTR(hhkey, config_by_key, __DECONST(char *, conf->key),
		    strlen(conf->key), conf);
	}

	if (path == NULL)
		path = PREFIX"/etc/pkg.conf";

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
			pkg_config_parse(&doc, node, config_by_key);
		}
	} else {
		pkg_emit_error("Invalid configuration format, ignoring the configuration file");
	}

	yaml_document_delete(&doc);
	yaml_parser_delete(&parser);

	subst_packagesite();
	disable_plugins_if_static();

	parsed = true;
	return (EPKG_OK);
}

static void
pkg_config_free(struct pkg_config *conf)
{
	struct pkg_config_kv *k;
	struct pkg_config_value *v;
	if (conf == NULL)
		return;

	if (conf->type == CONF_STRING)
		free(conf->string);
	else if (conf->type == CONF_KVLIST) {
		while (!STAILQ_EMPTY(&conf->kvlist)) {
			k = STAILQ_FIRST(&conf->kvlist);
			free(k->key);
			free(k->value);
			STAILQ_REMOVE_HEAD(&conf->kvlist, next);
			free(k);
		}
	} else if (conf->type == CONF_LIST) {
		while (!STAILQ_EMPTY(&conf->kvlist)) {
			v = STAILQ_FIRST(&conf->list);
			free(v->value);
			STAILQ_REMOVE_HEAD(&conf->list, next);
			free(v);
		}
	}

	free(conf);
}

int
pkg_shutdown(void)
{
	if (parsed == true) {
		HASH_FREE(config, pkg_config, pkg_config_free);
	} else {
		pkg_emit_error("pkg_shutdown() must be called after pkg_init()");
		return (EPKG_FATAL);
	}

	parsed = false;

	return (EPKG_OK);
}
