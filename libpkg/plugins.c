/*
 * Copyright (c) 2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#define N(a) (sizeof(a) / sizeof(a[0]))

struct _pkg_plugins_kv {
	 char *key;
	 char *val;
} pkg_plugins_kv[] = {
	[PKG_PLUGINS_NAME] 		= { __DECONST(char *, "name"), NULL },
	[PKG_PLUGINS_DESC] 		= { __DECONST(char *, "description"), NULL },
	[PKG_PLUGINS_VERSION] 		= { __DECONST(char *, "version"), NULL },
	[PKG_PLUGINS_PLUGINFILE]	= { __DECONST(char *, "plugin"), NULL },
	[PKG_PLUGINS_ENABLED]		= { __DECONST(char *, "enabled"), NULL },
};

struct pkg_plugins {
	struct _pkg_plugins_kv fields[N(pkg_plugins_kv)];	/* plugin configuration fields */
	void *lh;						/* library handle */
	STAILQ_ENTRY(pkg_plugins) next;
};

STAILQ_HEAD(plugins_head, pkg_plugins);
static struct plugins_head ph = STAILQ_HEAD_INITIALIZER(ph);

static int pkg_plugins_discover(void);
static int pkg_plugins_parse_conf(const char *file);
static int pkg_plugins_free(void);
static int pkg_plugins_load(struct pkg_plugins *p);
static int pkg_plugins_unload(struct pkg_plugins *p);

static int
pkg_plugins_discover(void)
{
	FTS  	*fts = NULL;
	FTSENT	*ftsent = NULL;
	char	*paths[2];
	const char *plugins_dir = NULL;

	if (pkg_config_string(PKG_CONFIG_PLUGINS_DIR, &plugins_dir) != EPKG_OK) {
		pkg_emit_error("Cannot get PKG_PLUGINS_DIR config entry");
		return (EPKG_FATAL);
	}
	
        paths[0] = __DECONST(char *, plugins_dir);
        paths[1] = NULL;

	if ((fts = fts_open(paths, FTS_LOGICAL, NULL)) == NULL) {
		pkg_emit_error("fts_open(%s)", plugins_dir);
		return (EPKG_FATAL);
	}

	while ((ftsent = fts_read(fts)) != NULL) {

		/* skip anything that is not a file */
		if (ftsent->fts_info != FTS_F)
			continue;

		/* parse only .conf files */
		if (strstr(ftsent->fts_name, ".conf") != NULL)
			pkg_plugins_parse_conf(ftsent->fts_path);
	}

	fts_close(fts);

	return (EPKG_OK);
}

static int
pkg_plugins_parse_conf(const char *file)
{
	struct pkg_plugins *new = NULL;
	properties p = NULL;
	unsigned int i = 0;
	char *temp;
	int fd = -1;
	bool wrong_conf = false;

	assert(file != NULL);

	if ((fd = open(file, O_RDONLY)) < 0) {
		pkg_emit_error("open(%s)", file);
		return (EPKG_FATAL);
	}

	if ((new = calloc(1, sizeof(struct pkg_plugins))) == NULL) {
		pkg_emit_error("Cannot allocate memory");
		return (EPKG_FATAL);
	}

	p = properties_read(fd);

	for (i = 0; i < N(pkg_plugins_kv); i++) {
		new->fields[i].key = strdup(pkg_plugins_kv[i].key);
		if ((temp = property_find(p, new->fields[i].key)) == NULL) {
			pkg_emit_error("required option '%s' is not specified in '%s'",
				       new->fields[i].key, file);
			wrong_conf = true;
		} else
			new->fields[i].val = strdup(temp);
	}

	properties_free(p);
	close(fd);

	if (wrong_conf == true) {
		pkg_emit_error("required options were missing in '%s', plugin will not be loaded", file);
		for (i = 0; i < N(pkg_plugins_kv); i++) {
			if (new->fields[i].key != NULL)
				free(new->fields[i].key);
			if (new->fields[i].val != NULL)
				free (new->fields[i].val);
		}
		free(new);
		return (EPKG_FATAL);
	}

	STAILQ_INSERT_TAIL(&ph, new, next);

	return (EPKG_OK);
}

static int
pkg_plugins_free(void)
{
	struct pkg_plugins *p = NULL;
	unsigned int i;

        while (!STAILQ_EMPTY(&ph)) {
                p = STAILQ_FIRST(&ph);
                STAILQ_REMOVE_HEAD(&ph, next);

		for (i = 0; i < N(pkg_plugins_kv); i++) {
			free(p->fields[i].key);
			free(p->fields[i].val);
		}

		free(p);
        }

	return (EPKG_OK);
}

static int
pkg_plugins_load(struct pkg_plugins *p)
{
	struct sbuf *init_name = NULL;
	int (*init_func)(void);
	int rc = EPKG_OK;
	
	const char *pluginfile = NULL;
	const char *pluginname = NULL;
	
	assert(p != NULL);

	pluginfile = pkg_plugins_get(p, PKG_PLUGINS_PLUGINFILE);
	pluginname = pkg_plugins_get(p, PKG_PLUGINS_NAME);

	if ((eaccess(pluginfile, F_OK)) != 0) {
		pkg_emit_error("Plugin file '%s' does not exists, ignoring plugin '%s'",
			       pluginfile, pluginname);
		return (EPKG_FATAL);
	}

	/*
	 * Load the plugin
	 */
	if ((p->lh = dlopen(pluginfile, RTLD_LAZY)) == NULL) {
		pkg_emit_error("Loading of plugin '%s' failed: %s",
			       pluginname, dlerror());
		return (EPKG_FATAL);
	}

	/*
	 * The plugin *must* provide an init function that is called by the library.
	 *
	 * The plugin's init function takes care of registering a hook in the library,
	 * which is handled by the pkg_plugins_register_hook() function.
	 *
	 * Every plugin *must* provide a 'pkg_plugin_init_<plugin>' function, which is
	 * called upon plugin loading for registering a hook in the library.
	 *
	 * The plugin's init function prototype should be in the following form:
	 *
	 * int pkg_plugins_init_<plugin> (void);
	 *
	 * No arguments are passed to the plugin's init function.
	 *
	 * Upon successful initialization of the plugin EPKG_OK (0) is returned and
	 * upon failure EPKG_FATAL ( > 0 ) is returned to the caller.
	 */
	init_name = sbuf_new_auto();
	sbuf_printf(init_name, "pkg_plugins_init_%s", pluginname);

	if ((init_func = dlsym(p->lh, sbuf_get(init_name))) == NULL) {
		pkg_emit_error("Cannot load init function for plugin '%s': %s",
			       pluginname, dlerror());
		sbuf_delete(init_name);
		dlclose(p->lh);
		return (EPKG_FATAL);
	}

	sbuf_delete(init_name);

	/*
	 * Initialize the plugin and let it register itself a hook in the library
	 */
	if ((rc = (*init_func)()) != EPKG_OK) {
		pkg_emit_error("Plugin '%s' failed to initialize, return code was %d",
			       pluginname, rc);
		dlclose(p->lh);
	}

	return (rc);
}

static int
pkg_plugins_unload(struct pkg_plugins *p)
{
	struct sbuf *shutdown_name = NULL;
	int (*shutdown_func)(void);
	int rc = EPKG_OK;
	
	const char *pluginfile = NULL;
	const char *pluginname = NULL;
	
	assert(p != NULL);

	/*
	 * Plugin could be enabled, but failed to be loaded
	 */
	if (p->lh == NULL)
		return (EPKG_OK);

	pluginfile = pkg_plugins_get(p, PKG_PLUGINS_PLUGINFILE);
	pluginname = pkg_plugins_get(p, PKG_PLUGINS_NAME);

	/*
	 * Plugins may optionally provide a shutdown function.
	 *
	 * When a plugin provides a shutdown function, it is called
	 * before a plugin is being unloaded. This is useful in cases
	 * where a plugin needs to perform a cleanup for example, or
	 * perform any post-actions like reporting for example.
	 *
	 * The plugin's shutdown function prototype should be in the following form:
	 *
	 * int pkg_plugins_shutdown_<plugin> (void);
	 *
	 * Upon successful shutdown of the plugin EPKG_OK (0) is returned and
	 * upon failure EPKG_FATAL ( > 0 ) is returned to the caller.
	 */
	
	shutdown_name = sbuf_new_auto();
	sbuf_printf(shutdown_name, "pkg_plugins_shutdown_%s", pluginname);

	/* Execute the shutdown function provided by the plugin */
	if ((shutdown_func = dlsym(p->lh, sbuf_get(shutdown_name))) != NULL) {
		if ((rc = (*shutdown_func)()) != EPKG_OK) {
			pkg_emit_error("Plugin '%s' failed to shutdown properly, return code was %d",
				       pluginname, rc);
		}
	}

	sbuf_delete(shutdown_name);
	dlclose(p->lh);

	return (rc);
}

const char *
pkg_plugins_get(struct pkg_plugins *p, pkg_plugins_key key)
{
	assert(p != NULL);

	return (p->fields[key].val);
}

bool
pkg_plugins_is_enabled(struct pkg_plugins *p)
{
	const char *enabled = NULL;
	bool is_enabled = false;

	assert(p != NULL);

	enabled = pkg_plugins_get(p, PKG_PLUGINS_ENABLED);

	if ((strcasecmp(enabled, "on") == 0) ||
	    (strcasecmp(enabled, "yes") == 0) ||
	    (strcasecmp(enabled, "true") == 0))
		is_enabled = true;

	return (is_enabled);
}

bool
pkg_plugins_is_loaded(struct pkg_plugins *p)
{
	bool is_loaded = false;

	assert(p != NULL);

	is_loaded = ((pkg_plugins_is_enabled(p)) && (p->lh != NULL));
	
	return (is_loaded);
}

int
pkg_plugins_display_loaded(void)
{
	struct pkg_plugins *p = NULL;

	if (!STAILQ_EMPTY(&ph))
		return (EPKG_OK);

	printf("Plugins loaded: ");
	
	while (pkg_plugins_list(&p) != EPKG_END)
		if (pkg_plugins_is_loaded(p))
			printf("%s ", pkg_plugins_get(p, PKG_PLUGINS_NAME));

	return (EPKG_OK);
}

int
pkg_plugins_list(struct pkg_plugins **plugin)
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
	struct pkg_plugins *p = NULL;

	/*
	 * Discover available plugins
	 */
	pkg_plugins_discover();

	/*
	 * Load any enabled plugins
	 */
	while (pkg_plugins_list(&p) != EPKG_END)
		if (pkg_plugins_is_enabled(p))
			pkg_plugins_load(p);

	return (EPKG_OK);
}

int
pkg_plugins_shutdown(void)
{
	struct pkg_plugins *p = NULL;

	/*
	 * Unload any previously loaded plugins
	 */
	while (pkg_plugins_list(&p) != EPKG_END)
		if (pkg_plugins_is_enabled(p))
			pkg_plugins_unload(p);

	/*
	 * Deallocate memory used by the plugins
	 */
	pkg_plugins_free();

	return (EPKG_OK);
}
