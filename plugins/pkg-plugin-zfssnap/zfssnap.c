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

#include <sys/types.h>
#include <sys/param.h>

#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <libutil.h>
#include <unistd.h>

#include <pkg.h>

#include "zfssnap.h"

#define PLUGIN_NAME "zfssnap"
#define PLUGIN_CONF "/usr/local/etc/pkg/plugins/zfssnap.conf"

static struct _zfssnap_config {
        const char *key;
        const char *val;
} c[] = {
        { "zfs_fs", NULL },
	{ "zfs_prefix", NULL },
        { "zfs_args", NULL },
        { NULL, NULL }
};

static int plugins_zfssnap_load_conf(const char *file);
static const char *plugins_zfssnap_get_conf(const char *key);
static int plugins_zfssnap_fd = -1;
static properties plugins_zfssnap_p = NULL;

int
pkg_plugins_init_zfssnap(void)
{
	if (plugins_zfssnap_load_conf(PLUGIN_CONF) != EPKG_OK) {
		fprintf(stderr, ">>> Cannot parse configuration file %s\n", PLUGIN_CONF);
		return (EPKG_FATAL);
	}
	
	if (pkg_plugins_hook(PLUGIN_NAME, PKG_PLUGINS_HOOK_PRE_INSTALL, &plugins_zfssnap_callback) != EPKG_OK) {
		fprintf(stderr, ">>> Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}
	
	if (pkg_plugins_hook(PLUGIN_NAME, PKG_PLUGINS_HOOK_PRE_DEINSTALL, &plugins_zfssnap_callback) != EPKG_OK) {
		fprintf(stderr, ">>> Plugin '%s' failed to hook into the library\n", PLUGIN_NAME);
		return (EPKG_FATAL);
	}
	
	return (EPKG_OK);
}

int
pkg_plugins_shutdown_zfssnap(void)
{
	properties_free(plugins_zfssnap_p);
	close(plugins_zfssnap_fd);
	
	return (EPKG_OK);
}

static int
plugins_zfssnap_load_conf(const char *file)
{
        int i;
	bool wrong_conf = false;

	assert(file != NULL);

	if ((plugins_zfssnap_fd = open(file, O_RDONLY)) < 0) {
                fprintf(stderr, ">>> Cannot open configuration file %s", file);
                return (EPKG_FATAL);
        }

	plugins_zfssnap_p = properties_read(plugins_zfssnap_fd);
	
        for (i = 0; c[i].key != NULL; i++)
		c[i].val = property_find(plugins_zfssnap_p, c[i].key);

	return (EPKG_OK);
}

static const char *
plugins_zfssnap_get_conf(const char *key)
{
	unsigned int i;
	
	assert (key != NULL);

        for (i = 0; c[i].key != NULL; i++)
		if (strcmp(c[i].key, key) == 0)
			return (c[i].val);

	return (NULL);
}

int
plugins_zfssnap_callback(void *data, struct pkgdb *db)
{
	char cmd_buf[MAXPATHLEN + 1];
	struct tm *tm = NULL;
	const char *zfs_fs = NULL;
	const char *zfs_args = NULL;
	const char *zfs_prefix = NULL;
	time_t t = 0;

	t = time(NULL);
	tm = localtime(&t);
	
	/* we don't care about data and db, so nothing to assert() here */
	/* assert(db != NULL); */ 
	/* assert(data != NULL); */

	zfs_fs = plugins_zfssnap_get_conf("zfs_fs");
	zfs_args = plugins_zfssnap_get_conf("zfs_args");
	zfs_prefix = plugins_zfssnap_get_conf("zfs_prefix");

	if ((zfs_fs == NULL) || (zfs_prefix == NULL)) {
		fprintf(stderr, ">>> Configuration options missing, plugin '%s' will not be loaded\n",
			       PLUGIN_NAME);
		return (EPKG_FATAL);
	}

	snprintf(cmd_buf, sizeof(cmd_buf), "%s %s %s@%s-%d-%d-%d_%d.%d.%d",
		 "/sbin/zfs snapshot", zfs_args,
		 zfs_fs, zfs_prefix,
		 1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		 tm->tm_hour, tm->tm_min, tm->tm_sec);

	printf(">>> Creating ZFS snapshot\n");
	system(cmd_buf);
	
	return (EPKG_OK);
}

