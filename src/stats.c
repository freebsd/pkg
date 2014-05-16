/*-
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
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

#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>
#include <inttypes.h>
#include <libutil.h>
#include <err.h>

#include <pkg.h>

#include "pkgcli.h"

void
usage_stats(void)
{
	fprintf(stderr, "Usage: pkg stats [-qlrb]\n\n");
	fprintf(stderr, "For more information see 'pkg help stats'.\n");
}

int
exec_stats(__unused int argc, __unused char **argv)
{
	struct pkgdb *db = NULL;
	int64_t flatsize = 0;
	unsigned int opt = 0;
	char size[7];
	int ch;
	bool show_bytes = false;

	while ((ch = getopt(argc, argv, "qlrb")) != -1) {
                switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'l':
			opt |= STATS_LOCAL;
			break;
		case 'r':
			opt |= STATS_REMOTE;
			break;
		case 'b':
			show_bytes = true;
			break;
		default:
			usage_stats();
			return (EX_USAGE);
                }
        }
        argc -= optind;
        argv += optind;

	/* default is to show everything we have */
	if (opt == 0)
		opt |= (STATS_LOCAL | STATS_REMOTE);

	if (pkgdb_open(&db, PKGDB_REMOTE) != EPKG_OK) {
		return (EX_IOERR);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if (opt & STATS_LOCAL) {
		printf("Local package database:\n");
		printf("\tInstalled packages: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_LOCAL_COUNT));

		flatsize = pkgdb_stats(db, PKG_STATS_LOCAL_SIZE);

		if (show_bytes)
			printf("\tDisk space occupied: %" PRId64 "\n\n", flatsize);
		else {
			humanize_number(size, sizeof(size), flatsize, "B", HN_AUTOSCALE, 0);
			printf("\tDisk space occupied: %s\n\n", size);
		}
	}

	if (opt & STATS_REMOTE && pkg_repos_total_count() > 0) {
		printf("Remote package database(s):\n");
		printf("\tNumber of repositories: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_REMOTE_REPOS));
		printf("\tPackages available: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_REMOTE_COUNT));
		printf("\tUnique packages: %" PRId64 "\n", pkgdb_stats(db, PKG_STATS_REMOTE_UNIQUE));

		flatsize = pkgdb_stats(db, PKG_STATS_REMOTE_SIZE);

		if (show_bytes)
			printf("\tTotal size of packages: %" PRId64 "\n", flatsize);
		else {
			humanize_number(size, sizeof(size), flatsize, "B", HN_AUTOSCALE, 0);
			printf("\tTotal size of packages: %s\n", size);
		}
	}

	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (EX_OK);
}
