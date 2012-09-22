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
#include <sysexits.h>
#include <unistd.h>
#include <inttypes.h>
#include <libutil.h>

#include <pkg.h>

#define PLUGIN_STATS_LOCAL 	(1<<0)
#define PLUGIN_STATS_REMOTE 	(1<<1)

static char myname[] = "mystats";
static char version[] = "1.0.0";
static char mydesc[] = "Display package statistics";
static char plugdesc[] = "A plugin to display package statistics";

static int plugin_mystats_usage(void);

int
init(struct pkg_plugins *p)
{
	pkg_plugins_set(p, PKG_PLUGINS_NAME, myname);
	pkg_plugins_set(p, PKG_PLUGINS_DESC, plugdesc);
	pkg_plugins_set(p, PKG_PLUGINS_VERSION, version);

	return (EPKG_OK);
}

int
shutdown(void)
{
	/* nothing to be done here */

	return (EPKG_OK);
}

static int
plugin_mystats_usage(void)
{
	fprintf(stderr, "usage: pkg mystats [-lr]\n\n");
	fprintf(stderr, "A plugin for displaying package statistics\n");
	return (EPKG_OK);
}

static int
plugin_mystats_callback(int argc, char **argv)
{
	struct pkgdb *db = NULL;
        int64_t flatsize = 0;
        char size[7];
        unsigned int opt = 0;
        int ch;

        while ((ch = getopt(argc, argv, "lr")) != -1) {
                switch (ch) {
                case 'l':
                        opt |= PLUGIN_STATS_LOCAL;
                        break;
                case 'r':
                        opt |= PLUGIN_STATS_REMOTE;
                        break;
                default:
                        plugin_mystats_usage();
                        return (EX_USAGE);
                }
        }
        argc -= optind;
        argv += optind;

        /* default is to show everything we have */
        if (opt == 0)
                opt |= (PLUGIN_STATS_LOCAL | PLUGIN_STATS_REMOTE);

        if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
                return (EX_IOERR);
        }

        if (opt & PLUGIN_STATS_LOCAL) {
                printf("Local package database:\n");
                printf("\tInstalled packages: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_LOCAL_COUNT));

                flatsize = pkgdb_stats(db, PKG_STATS_LOCAL_SIZE);
                humanize_number(size, sizeof(flatsize), flatsize, "B", HN_AUTOSCALE, 0);
                printf("\tDisk space occupied: %s\n\n", size);
        }

        if (opt & PLUGIN_STATS_REMOTE) {
                printf("Remote package database(s):\n");
                printf("\tNumber of repositories: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_REMOTE_REPOS));
                printf("\tPackages available: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_REMOTE_COUNT));
                printf("\tUnique packages: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_REMOTE_UNIQUE));

                flatsize = pkgdb_stats(db, PKG_STATS_REMOTE_SIZE);
                humanize_number(size, sizeof(flatsize), flatsize, "B", HN_AUTOSCALE, 0);
                printf("\tTotal size of packages: %s\n", size);
        }

	pkgdb_close(db);

	return (EPKG_OK);
}

int
pkg_register_cmd(const char **name, const char **desc, int (**exec)(int argc, char **argv))
{
	*name = myname;
	*desc = mydesc;
	*exec = plugin_mystats_callback;

	return (EPKG_OK);
}

