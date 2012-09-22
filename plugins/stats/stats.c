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

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <libutil.h>

#include <pkg.h>

#define PLUGIN_NAME "stats"

static char name[] = "stats";
static char version[] = "1.0.0";
static char description[] = "Plugin for displaying package stats";
static int plugin_stats_callback(void *data, struct pkgdb *db);

int
init(struct pkg_plugin *p)
{
	/*
	 * Hook into the library and provide package stats for the following actions:
	 *
	 * - pre-install 
	 * - post-install
	 * - pre-deinstall
	 * - post-deinstall
	 */
	pkg_plugin_set(p, PKG_PLUGIN_NAME, name);
	pkg_plugin_set(p, PKG_PLUGIN_DESC, description);
	pkg_plugin_set(p, PKG_PLUGIN_VERSION, version);

	if (pkg_plugin_hook_register(p, PKG_PLUGIN_HOOK_PRE_INSTALL, &plugin_stats_callback) != EPKG_OK) {
		fprintf(stderr, "Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}

	if (pkg_plugin_hook_register(p, PKG_PLUGIN_HOOK_POST_INSTALL, &plugin_stats_callback) != EPKG_OK) {
		fprintf(stderr, "Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}
	if (pkg_plugin_hook_register(p, PKG_PLUGIN_HOOK_PRE_DEINSTALL, &plugin_stats_callback) != EPKG_OK) {
		fprintf(stderr, "Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}
	
	if (pkg_plugin_hook_register(p, PKG_PLUGIN_HOOK_POST_DEINSTALL, &plugin_stats_callback) != EPKG_OK) {
		fprintf(stderr, "Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}
	
	return (EPKG_OK);
}

int
shutdown(struct pkg_plugin *p __unused)
{
	/* nothing to be done here */

	return (EPKG_OK);
}

int
plugin_stats_callback(void *data, struct pkgdb *db)
{
        int64_t flatsize = 0;
        char size[7];

	assert(db != NULL);
	/* assert(data != NULL); */

	flatsize = pkgdb_stats(db, PKG_STATS_LOCAL_SIZE);
	humanize_number(size, sizeof(flatsize), flatsize, "B", HN_AUTOSCALE, 0);
	printf(">>> Installed packages : %" PRId64 " | Disk space: %s <<<\n",
	       pkgdb_stats(db, PKG_STATS_LOCAL_COUNT),
	       size);

	return (EPKG_OK);
}
