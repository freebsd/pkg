/*-
 * Copyright (c) 2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2012-2014 Baptiste Daroussin <bapt@FreeBSD.org>
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
	UT_string *fields[PLUGIN_NUMFIELDS];
	void *lh;						/* library handle */
	bool parsed;
	struct plugin_hook *hooks;
	pkg_object *conf;
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

	HASH_FREE(p->hooks, free);

	return (EPKG_OK);
}

static void
plug_free(struct pkg_plugin *p)
{
	unsigned int i;

	for (i = 0; i < PLUGIN_NUMFIELDS; i++)
		utstring_free(p->fields[i]);

	pkg_plugin_hook_free(p);
	free(p);
}

static int
pkg_plugin_free(void)
{
	LL_FREE(plugins, plug_free);

	return (EPKG_OK);
}

int
pkg_plugin_hook_register(struct pkg_plugin *p, pkg_plugin_hook_t hook, pkg_plugin_callback callback)
{
	struct plugin_hook *new = NULL;
	
	assert(p != NULL);
	assert(callback != NULL);

	new = xcalloc(1, sizeof(struct plugin_hook));
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

	utstring_renew(p->fields[key]);
	utstring_printf(p->fields[key], "%s", str);
	return (EPKG_OK);
}

const char *
pkg_plugin_get(struct pkg_plugin *p, pkg_plugin_key key)
{
	assert(p != NULL);

	if (p->fields[key] == NULL)
		return (NULL);

	return (utstring_body(p->fields[key]));
}

int
pkg_plugin_conf_add(struct pkg_plugin *p, pkg_object_t type, const char *key,
    const char *def)
{
	ucl_object_t *o = NULL;
	const char *walk, *buf, *value, *k;
	k = NULL;

	switch (type) {
	case PKG_STRING:
		o = ucl_object_fromstring_common(def, 0, UCL_STRING_TRIM);
		break;
	case PKG_BOOL:
		o = ucl_object_fromstring_common(def, 0, UCL_STRING_PARSE_BOOLEAN);
		if (o->type != UCL_BOOLEAN) {
			ucl_object_unref(o);
			return (EPKG_FATAL);
		}
		break;
	case PKG_INT:
		o = ucl_object_fromstring_common(def, 0, UCL_STRING_PARSE_INT);
		if (o->type != UCL_INT) {
			ucl_object_unref(o);
			return (EPKG_FATAL);
		}
		break;
	case PKG_OBJECT:
		walk = buf = def;
		while ((buf = strchr(buf, ',')) != NULL) {
			k = walk;
			value = walk;
			while (*value != ',') {
				if (*value == '=')
					break;
				value++;
			}
			if (o == NULL)
				o = ucl_object_typed_new(UCL_OBJECT);
			ucl_object_insert_key(o,
			    ucl_object_fromstring_common(value + 1, buf - value - 1, UCL_STRING_TRIM),
			    k, value - k, false);
			buf++;
			walk = buf;
		}
		key = walk;
		value = walk;
		while (*value != '\0') {
			if (*value == '=')
				break;
			value++;
		}
		if (o == NULL)
			o = ucl_object_typed_new(UCL_OBJECT);
		ucl_object_insert_key(o,
		    ucl_object_fromstring_common(value + 1, strlen(value + 1), UCL_STRING_TRIM),
		    k, value - k, false);
		break;
	case PKG_ARRAY:
		walk = buf = def;
		while ((buf = strchr(buf, ',')) != NULL) {
			if (o == NULL)
				o = ucl_object_typed_new(UCL_ARRAY);
			ucl_array_append(o,
					ucl_object_fromstring_common(walk, buf - walk, UCL_STRING_TRIM));
			buf++;
			walk = buf;
		}
		if (o == NULL)
			o = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(o,
				ucl_object_fromstring_common(walk, strlen(walk), UCL_STRING_TRIM));
		break;
	default:
		break;
	}

	if (o != NULL)
		ucl_object_replace_key(p->conf, o, key, strlen(key), false);

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
	char pluginfile[MAXPATHLEN];
	const ucl_object_t *obj, *cur;
	ucl_object_iter_t it = NULL;
	const char *plugdir;
	bool plug_enabled = false;
	int (*init_func)(struct pkg_plugin *);

	plug_enabled = pkg_object_bool(pkg_config_get("PKG_ENABLE_PLUGINS"));
	if (!plug_enabled)
		return (EPKG_OK);
	/*
	 * Discover available plugins
	 */
	plugdir = pkg_object_string(pkg_config_get("PKG_PLUGINS_DIR"));

	obj = pkg_config_get("PLUGINS");
	while ((cur = ucl_iterate_object(obj, &it, true))) {
		/*
		 * Load the plugin
		 */
		if (cur->type != UCL_STRING)
			continue;

		snprintf(pluginfile, sizeof(pluginfile), "%s/%s.so", plugdir,
		    pkg_object_string(cur));
		p = xcalloc(1, sizeof(struct pkg_plugin));
		if ((p->lh = dlopen(pluginfile, RTLD_LAZY)) == NULL) {
			pkg_emit_error("Loading of plugin '%s' failed: %s",
			    pkg_object_string(cur), dlerror());
			free(p);
			return (EPKG_FATAL);
		}
		if ((init_func = dlsym(p->lh, "pkg_plugin_init")) == NULL) {
			pkg_emit_error("Cannot load init function for plugin '%s'",
			     pkg_object_string(cur));
			pkg_emit_error("Plugin '%s' will not be loaded: %s",
			      pkg_object_string(cur), dlerror());
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
pkg_plugin_parse(struct pkg_plugin *p)
{
	char confpath[MAXPATHLEN];
	const char *path;
	const char *plugname;
	struct ucl_parser *pr;
	const ucl_object_t *cur, *o;
	ucl_object_t *obj;
	ucl_object_iter_t it = NULL;
	const char *key;

	pr = ucl_parser_new(0);

	path = pkg_object_string(pkg_config_get("PLUGINS_CONF_DIR"));
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

	while ((cur = ucl_iterate_object(obj, &it, true))) {
		key = ucl_object_key(cur);
		o = ucl_object_find_key(p->conf, key);
		if (o == NULL)
			continue;

		if (o->type != cur->type) {
			pkg_emit_error("Malformed key %s, ignoring", key);
			continue;
		}

		ucl_object_delete_key(p->conf, key);
		ucl_object_insert_key(p->conf, ucl_object_ref(cur), key, strlen(key), false);
	}

	p->parsed = true;
	ucl_object_unref(obj);
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

const pkg_object *
pkg_plugin_conf(struct pkg_plugin *p)
{
	return (p->conf);
}
