/*-
 * Copyright (c) 2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2012 Baptiste Daroussin <bapt@FreeBSD.org>
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
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fts.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libutil.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <ucl.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

#define PLUGIN_NUMFIELDS 4

struct plugin_hook {
	pkg_plugin_hook_t hook;				/* plugin hook type */
	pkg_plugin_callback callback;			/* plugin callback function */
	UT_hash_handle hh;
};

struct pkg_plugin {
	struct sbuf *fields[PLUGIN_NUMFIELDS];
	void *lh;						/* library handle */
	bool parsed;
	struct plugin_hook *hooks;
	struct pkg_config *conf;
	struct pkg_config *conf_by_key;
	struct pkg_plugin *next;
};

static struct pkg_plugin *plugins = NULL;

static int pkg_plugin_free(void);
static int pkg_plugin_hook_free(struct pkg_plugin *p);
static int pkg_plugin_hook_exec(struct pkg_plugin *p, pkg_plugin_hook_t hook, void *data, struct pkgdb *db);

void *
pkg_plugin_func(struct pkg_plugin *p, const char *func)
{
	return (dlsym(p->lh, func));
}

static int
pkg_plugin_hook_free(struct pkg_plugin *p)
{
	assert(p != NULL);

	HASH_FREE(p->hooks, plugin_hook, free);

	return (EPKG_OK);
}

static void
plug_free(struct pkg_plugin *p)
{
	unsigned int i;

	for (i = 0; i < PLUGIN_NUMFIELDS; i++)
		sbuf_delete(p->fields[i]);

	pkg_plugin_hook_free(p);
	free(p);
}

static int
pkg_plugin_free(void)
{
	LL_FREE(plugins, pkg_plugin, plug_free);

	return (EPKG_OK);
}

int
pkg_plugin_hook_register(struct pkg_plugin *p, pkg_plugin_hook_t hook, pkg_plugin_callback callback)
{
	struct plugin_hook *new = NULL;
	
	assert(p != NULL);
	assert(callback != NULL);

	if ((new = calloc(1, sizeof(struct plugin_hook))) == NULL) {
		pkg_emit_error("Cannot allocate memory");
		return (EPKG_FATAL);
	}

	new->hook = hook;
	new->callback = callback;

	HASH_ADD_INT(p->hooks, hook, new);

	return (EPKG_OK);
}

static int
pkg_plugin_hook_exec(struct pkg_plugin *p, pkg_plugin_hook_t hook, void *data, struct pkgdb *db)
{
	struct plugin_hook *h = NULL;
	
	assert(p != NULL);

	HASH_FIND_INT(p->hooks, &hook, h);
	if (h != NULL)
		h->callback(data, db);

	return (EPKG_OK);
}

int
pkg_plugins_hook_run(pkg_plugin_hook_t hook, void *data, struct pkgdb *db)
{
	struct pkg_plugin *p = NULL;

	while (pkg_plugins(&p) != EPKG_END)
		pkg_plugin_hook_exec(p, hook, data, db);

	return (EPKG_OK);
}

int
pkg_plugin_set(struct pkg_plugin *p, pkg_plugin_key key, const char *str)
{
	assert(p != NULL);

	return (sbuf_set(&p->fields[key], str));
}

const char *
pkg_plugin_get(struct pkg_plugin *p, pkg_plugin_key key)
{
	assert(p != NULL);

	return (sbuf_get(p->fields[key]));
}

int
pkg_plugin_conf_add_string(struct pkg_plugin *p, int id, const char *key, const char *def)
{
	struct pkg_config *conf;
	char *val;

	HASH_FIND_INT(p->conf, &id, conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same id is already registred");
		return (EPKG_FATAL);
	}

	HASH_FIND(hhkey, p->conf_by_key, __DECONST(char *, key), strlen(key), conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same key(%s) is already registred", key);
		return (EPKG_FATAL);
	}

	conf = malloc(sizeof(struct pkg_config));
	conf->id = id;
	conf->key = key;
	conf->type = PKG_CONFIG_STRING;
	conf->fromenv = false;
	val = getenv(key);
	if (val != NULL) {
		conf->string = strdup(val);
		conf->fromenv = true;
	} else if (def != NULL) {
		conf->string = strdup(def);
	} else {
		conf->string = NULL;
	}

	HASH_ADD_INT(p->conf, id, conf);
	HASH_ADD_KEYPTR(hhkey, p->conf_by_key, __DECONST(char *, conf->key),
	    strlen(conf->key), conf);

	return (EPKG_OK);
}

int
pkg_plugin_conf_add_bool(struct pkg_plugin *p, int id, const char *key, bool boolean)
{
	struct pkg_config *conf;
	char *val;

	HASH_FIND_INT(p->conf, &id, conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same id is already registred");
		return (EPKG_FATAL);
	}

	HASH_FIND(hhkey, p->conf_by_key, __DECONST(char *, key), strlen(key), conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same key(%s) is already registred", key);
		return (EPKG_FATAL);
	}

	conf = malloc(sizeof(struct pkg_config));
	conf->id = id;
	conf->key = key;
	conf->type = PKG_CONFIG_BOOL;
	conf->fromenv = false;
	val = getenv(key);
	if (val != NULL) {
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
	} else {
		conf->boolean = boolean;
	}

	HASH_ADD_INT(p->conf, id, conf);
	HASH_ADD_KEYPTR(hhkey, p->conf_by_key, __DECONST(char *, conf->key),
	    strlen(conf->key), conf);

	return (EPKG_OK);
}

int
pkg_plugin_conf_add_integer(struct pkg_plugin *p, int id, const char *key, int64_t integer)
{
	struct pkg_config *conf;
	const char *errstr;
	char *val;

	HASH_FIND_INT(p->conf, &id, conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same id is already registred");
		return (EPKG_FATAL);
	}

	HASH_FIND(hhkey, p->conf_by_key, __DECONST(char *, key), strlen(key), conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same key(%s) is already registred", key);
		return (EPKG_FATAL);
	}

	conf = malloc(sizeof(struct pkg_config));
	conf->id = id;
	conf->key = key;
	conf->type = PKG_CONFIG_STRING;
	conf->fromenv = false;
	val = getenv(key);
	if (val != NULL) {
		conf->fromenv = true;
		conf->integer = strtonum(val, 0, INT64_MAX, &errstr);
		if (errstr != NULL) {
			pkg_emit_error("Unable to convert %s to int64: %s",
			    val, errstr);
			free(conf);
			return (EPKG_FATAL);
		}
	} else {
		conf->integer = integer;
	}

	HASH_ADD_INT(p->conf, id, conf);
	HASH_ADD_KEYPTR(hhkey, p->conf_by_key, __DECONST(char *, conf->key),
	    strlen(conf->key), conf);

	return (EPKG_OK);
}

int
pkg_plugin_conf_add_kvlist(struct pkg_plugin *p, int id, const char *key)
{
	struct pkg_config *conf;

	HASH_FIND_INT(p->conf, &id, conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same id is already registred");
		return (EPKG_FATAL);
	}

	HASH_FIND(hhkey, p->conf_by_key, __DECONST(char *, key), strlen(key), conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same key(%s) is already registred", key);
		return (EPKG_FATAL);
	}

	conf = malloc(sizeof(struct pkg_config));
	conf->id = id;
	conf->key = key;
	conf->type = PKG_CONFIG_KVLIST;
	conf->kvlist = NULL;

	HASH_ADD_INT(p->conf, id, conf);
	HASH_ADD_KEYPTR(hhkey, p->conf_by_key, __DECONST(char *, conf->key),
	    strlen(conf->key), conf);

	return (EPKG_OK);
}

int
pkg_plugin_conf_add_list(struct pkg_plugin *p, int id, const char *key)
{
	struct pkg_config *conf;

	HASH_FIND_INT(p->conf, &id, conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same id is already registred");
		return (EPKG_FATAL);
	}

	HASH_FIND(hhkey, p->conf_by_key, __DECONST(char *, key), strlen(key), conf);
	if (conf != NULL) {
		pkg_emit_error("A configuration with the same key(%s) is already registred", key);
		return (EPKG_FATAL);
	}

	conf = malloc(sizeof(struct pkg_config));
	conf->id = id;
	conf->key = key;
	conf->type = PKG_CONFIG_LIST;
	conf->list = NULL;

	HASH_ADD_INT(p->conf, id, conf);
	HASH_ADD_KEYPTR(hhkey, p->conf_by_key, __DECONST(char *, conf->key),
	    strlen(conf->key), conf);

	return (EPKG_OK);
}

int
pkg_plugins(struct pkg_plugin **plugin)
{
	if ((*plugin) == NULL)
		(*plugin) = plugins;
	else
		(*plugin) = (*plugin)->next;

	if ((*plugin) == NULL)
		return (EPKG_END);
	else
		return (EPKG_OK);
}

int
pkg_plugins_init(void)
{
	struct pkg_plugin *p = NULL;
	struct pkg_config_value *v = NULL;
	char pluginfile[MAXPATHLEN];
	const char *plugdir;
	bool plug_enabled = false;
	int (*init_func)(struct pkg_plugin *);

	pkg_config_bool(PKG_CONFIG_ENABLE_PLUGINS, &plug_enabled);
	if (!plug_enabled)
		return (EPKG_OK);
	/*
	 * Discover available plugins
	 */
	pkg_config_string(PKG_CONFIG_PLUGINS_DIR, &plugdir);

	while (pkg_config_list(PKG_CONFIG_PLUGINS, &v) == EPKG_OK) {
		/*
		 * Load the plugin
		 */
		snprintf(pluginfile, MAXPATHLEN, "%s/%s.so", plugdir,
		    pkg_config_value(v));
		p = calloc(1, sizeof(struct pkg_plugin));
		if ((p->lh = dlopen(pluginfile, RTLD_LAZY)) == NULL) {
			pkg_emit_error("Loading of plugin '%s' failed: %s",
			    pkg_config_value(v), dlerror());
			free(p);
			return (EPKG_FATAL);
		}
		if ((init_func = dlsym(p->lh, "pkg_plugin_init")) == NULL) {
			pkg_emit_error("Cannot load init function for plugin '%s'",
			     pkg_config_value(v));
			pkg_emit_error("Plugin '%s' will not be loaded: %s",
			      pkg_config_value(v), dlerror());
			dlclose(p->lh);
			free(p);
			return (EPKG_FATAL);
		}
		pkg_plugin_set(p, PKG_PLUGIN_PLUGINFILE, pluginfile);
		if (init_func(p) == EPKG_OK) {
			LL_APPEND(plugins, p);
		} else {
			dlclose(p->lh);
			free(p);
		}
	}

	return (EPKG_OK);
}

int
pkg_plugin_conf_string(struct pkg_plugin *p, int key, const char **val)
{
	struct pkg_config *conf;

	if (p->parsed != true) {
		pkg_emit_error("configuration file not parsed");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(p->conf, &key, conf);
	if (conf == NULL)
		*val = NULL;
	else
		*val = conf->string;

	return (EPKG_OK);
}

int
pkg_plugin_conf_integer(struct pkg_plugin *p, int key, int64_t *val)
{
	struct pkg_config *conf;

	if (p->parsed != true) {
		pkg_emit_error("configuration file not parsed");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(p->conf, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	*val = conf->integer;

	return (EPKG_OK);
}

int
pkg_plugin_conf_bool(struct pkg_plugin *p, int key, bool *val)
{
	struct pkg_config *conf;

	if (p->parsed != true) {
		pkg_emit_error("configuration file not parsed");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(p->conf, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	*val = conf->boolean;

	return (EPKG_OK);
}

int
pkg_plugin_conf_kvlist(struct pkg_plugin *p, int key, struct pkg_config_kv **kv)
{
	struct pkg_config *conf;

	if (p->parsed != true) {
		pkg_emit_error("configuration file not parsed");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(p->conf, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	if (conf->type != PKG_CONFIG_KVLIST) {
		pkg_emit_error("this config entry is not a \"key: value\" list");
		return (EPKG_FATAL);
	}

	HASH_NEXT(conf->kvlist, (*kv));
}

int
pkg_plugin_conf_list(struct pkg_plugin *p, int key, struct pkg_config_value **v)
{
	struct pkg_config *conf;

	if (p->parsed != true) {
		pkg_emit_error("configuration file not parsed");
		return (EPKG_FATAL);
	}

	HASH_FIND_INT(p->conf, &key, conf);
	if (conf == NULL)
		return (EPKG_FATAL);

	if (conf->type != PKG_CONFIG_LIST) {
		pkg_emit_error("this config entry is not a list");
		return (EPKG_FATAL);
	}

	HASH_NEXT(conf->list, (*v));
}

int
pkg_plugin_confs(struct pkg_plugin *p, struct pkg_config **conf)
{
	HASH_NEXT(p->conf, (*conf));
}

int
pkg_plugin_parse(struct pkg_plugin *p)
{
	char confpath[MAXPATHLEN];
	const char *path;
	const char *plugname;
	struct ucl_parser *pr;
	ucl_object_t *obj;

	pr = ucl_parser_new(0);

	pkg_config_string(PKG_CONFIG_PLUGINS_CONF_DIR, &path);
	plugname = pkg_plugin_get(p, PKG_PLUGIN_NAME);

	snprintf(confpath, sizeof(confpath), "%s/%s.conf", path, plugname);

	if (!ucl_parser_add_file(pr, confpath)) {
		if (errno == ENOENT) {
			ucl_parser_free(pr);
			p->parsed = true;
			return (EPKG_OK);
		}
		pkg_emit_error("%s\n", ucl_parser_get_error(pr));
		ucl_parser_free(pr);

		return (EPKG_FATAL);
	}

	obj = ucl_parser_get_object(pr);
	if (obj->type == UCL_OBJECT)
		pkg_object_walk(obj->value.ov, p->conf_by_key);

	p->parsed = true;
	ucl_object_free(obj);
	ucl_parser_free(pr);

	return (EPKG_OK);
}

void
pkg_plugins_shutdown(void)
{
	struct pkg_plugin *p = NULL;
	int (*shutdown_func)(struct pkg_plugin *p);

	/*
	 * Unload any previously loaded plugins
	 */
	while (pkg_plugins(&p) != EPKG_END) {
		if ((shutdown_func = dlsym(p->lh, "pkg_plugin_shutdown")) != NULL) {
			shutdown_func(p);
		}
		dlclose(p->lh);
	}

	/*
	 * Deallocate memory used by the plugins
	 */
	pkg_plugin_free();

	return;
}
