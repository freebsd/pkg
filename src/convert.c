/*-
 * Copyright (c) 2012-2013 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2013 Bryan Drewery <bdrewery@FreeBSD.org>
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sbuf.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <sysexits.h>
#include <dirent.h>
#include <unistd.h>

#include <pkg.h>

#include <bsd_compat.h>

#include "pkgcli.h"

void
usage_convert(void)
{
	fprintf(stderr, "Usage: pkg convert [-d dir] [-n]\n\n");
	fprintf(stderr, "For more information see 'pkg help convert'.\n");
}

static int
convert_from_old(const char *pkg_add_dbdir, bool dry_run)
{
	DIR		*d;
	struct dirent	*dp;
	struct pkg	*p = NULL;
	char		 path[MAXPATHLEN];
	struct pkgdb	*db = NULL;
	struct stat	 sb;
	int		lock_type = PKGDB_LOCK_EXCLUSIVE;
	int		ret;

	if (dry_run)
		ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	else
		ret = pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE|
		    PKGDB_MODE_CREATE, PKGDB_DB_LOCAL);

	if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to convert packages");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK && ret != EPKG_ENODB) {
		warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	if ((d = opendir(pkg_add_dbdir)) == NULL)
		err(EX_NOINPUT, "%s", pkg_add_dbdir);

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		return (EX_IOERR);
	}
	if (dry_run)
		lock_type = PKGDB_LOCK_READONLY;
	if (pkgdb_obtain_lock(db, lock_type) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an advisory lock on a database, it is locked"
		    " by another process");
		return (EX_TEMPFAIL);
	}
	while ((dp = readdir(d)) != NULL) {
		if (fstatat(dirfd(d), dp->d_name, &sb, 0) == 0 &&
		    S_ISDIR(sb.st_mode)) {
			if (strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;
			if (p != NULL)
				pkg_free(p);
			if (pkg_new(&p, PKG_OLD_FILE) != EPKG_OK)
				err(EX_OSERR, "malloc");
			printf("Converting %s...\n", dp->d_name);
			snprintf(path, sizeof(path), "%s/%s", pkg_add_dbdir, dp->d_name);
			if (pkg_old_load_from_path(p, path) != EPKG_OK) {
				fprintf(stderr, "Skipping invalid package: %s\n", path);
				continue;
			}
			pkg_from_old(p);
			if (!dry_run)
				pkgdb_register_ports(db, p);
		}
	}

	pkg_free(p);
	pkgdb_release_lock(db, lock_type);
	pkgdb_close(db);
	return (EX_OK);
}

int
exec_convert(__unused int argc, __unused char **argv)
{
	int		 ch;
	bool		 dry_run = false;
	const char	*pkg_add_dbdir = "/var/db/pkg";

	struct option longopts[] = {
		{ "pkg-dbdir",	required_argument,	NULL,	'd' },
		{ "dry-run",	no_argument,		NULL,	'n' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+d:n", longopts, NULL)) != -1) {
		switch (ch) {
		case 'd':
			pkg_add_dbdir = optarg;
			break;
		case 'n':
			dry_run = true;
			break;
		default:
			usage_convert();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1) {
		usage_convert();
		return (EX_USAGE);
	}

	printf("Converting packages from %s\n", pkg_add_dbdir);

	return (convert_from_old(pkg_add_dbdir, dry_run));
}
