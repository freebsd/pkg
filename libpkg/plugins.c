/*
 * Copyright (c) 2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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
#include <sys/types.h>
#include <sys/stat.h>

#include <fts.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libutil.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "pkg.h"
#include "private/pkg.h"
#include "private/event.h"

#define PLUGIN_NUMFIELDS 4

struct plugin_hook {
	pkg_plugin_hook_t hook;				/* plugin hook type */
	pkg_plugin_callback callback;			/* plugin callback function */
	STAILQ_ENTRY(plugin_hook) next;
};

struct pkg_plugin {
	struct sbuf *fields[PLUGIN_NUMFIELDS];
	void *lh;						/* library handle */
	STAILQ_HEAD(phooks, plugin_hook) phooks;		/* plugin hooks */
	STAILQ_ENTRY(pkg_plugin) next;
};

static STAILQ_HEAD(, pkg_plugin) ph = STAILQ_HEAD_INITIALIZER(ph);

static int pkg_plugin_free(void);
static int pkg_plugin_hook_free(struct pkg_plugin *p);
static int pkg_plugin_hook_exec(struct pkg_plugin *p, pkg_plugin_hook_t hook, void *data, struct pkgdb *db);
static int pkg_plugin_hook_list(struct pkg_plugin *p, struct plugin_hook **h);

void *
pkg_plugin_func(struct pkg_plugin *p, const char *func)
{
	return (dlsym(p->lh, func));
}

static int
pkg_plugin_hook_free(struct pkg_plugin *p)
{
	struct plugin_hook *h = NULL;

	assert(p != NULL);

	while (!STAILQ_EMPTY(&p->phooks)) {
		h = STAILQ_FIRST(&p->phooks);
		STAILQ_REMOVE_HEAD(&p->phooks, next);
		free(h);
	}

	return (EPKG_OK);
}

static int
pkg_plugin_free(void)
{
	struct pkg_plugin *p = NULL;
	unsigned int i;

        while (!STAILQ_EMPTY(&ph)) {
                p = STAILQ_FIRST(&ph);
                STAILQ_REMOVE_HEAD(&ph, next);

		for (i = 0; i < PLUGIN_NUMFIELDS; i++)
			sbuf_delete(p->fields[i]);

		pkg_plugin_hook_free(p);
		free(p);
        }

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

	new->hook |= hook;
	new->callback = callback;

	STAILQ_INSERT_TAIL(&p->phooks, new, next);

	return (EPKG_OK);
}

static int
pkg_plugin_hook_exec(struct pkg_plugin *p, pkg_plugin_hook_t hook, void *data, struct pkgdb *db)
{
	struct plugin_hook *h = NULL;
	
	assert(p != NULL);

	while (pkg_plugin_hook_list(p, &h) != EPKG_END)
		if (h->hook == hook) {
			printf(">>> Triggering execution of plugin '%s'\n",
			       pkg_plugin_get(p, PKG_PLUGIN_NAME));
			h->callback(data, db);
		}

	return (EPKG_OK);
}

static int
pkg_plugin_hook_list(struct pkg_plugin *p, struct plugin_hook **h)
{
	assert(p != NULL);
	
	if ((*h) == NULL)
		(*h) = STAILQ_FIRST(&(p->phooks));
	else
		(*h) = STAILQ_NEXT((*h), next);

	if ((*h) == NULL)
		return (EPKG_END);
	else
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
pkg_plugins(struct pkg_plugin **plugin)
{
	assert(&ph != NULL);
	
	if ((*plugin) == NULL)
		(*plugin) = STAILQ_FIRST(&ph);
	else
		(*plugin) = STAILQ_NEXT((*plugin), next);

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
	int (*init_func)(struct pkg_plugin *);

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
		STAILQ_INIT(&p->phooks);
		if ((p->lh = dlopen(pluginfile, RTLD_LAZY)) == NULL) {
			pkg_emit_error("Loading of plugin '%s' failed: %s",
			    pkg_config_value(v), dlerror());
			return (EPKG_FATAL);
		}
		if ((init_func = dlsym(p->lh, "init")) == NULL) {
			pkg_emit_error("Cannot load init function for plugin '%s'",
			     pkg_config_value(v));
			pkg_emit_error("Plugin '%s' will not be loaded: %s",
			      pkg_config_value(v), dlerror());
		}
		pkg_plugin_set(p, PKG_PLUGIN_PLUGINFILE, pluginfile);
		if (init_func(p) == EPKG_OK) {
			STAILQ_INSERT_TAIL(&ph, p, next);
		} else {
			dlclose(p->lh);
			free(p);
		}
	}

	return (EPKG_OK);
}

int
pkg_plugins_shutdown(void)
{
	struct pkg_plugin *p = NULL;
	int (*shutdown_func)(struct pkg_plugin *p);

	/*
	 * Unload any previously loaded plugins
	 */
	while (pkg_plugins(&p) != EPKG_END) {
		if ((shutdown_func = dlsym(p->lh, "shutdown")) != NULL) {
			shutdown_func(p);
		}
		dlclose(p->lh);
	}

	/*
	 * Deallocate memory used by the plugins
	 */
	pkg_plugin_free();

	return (EPKG_OK);
}
