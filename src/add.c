/*-
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

#include <sys/param.h>
#include <sys/sbuf.h>

#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <getopt.h>

#include <pkg.h>

#include "pkgcli.h"

static int
is_url(const char * const pattern)
{
	if (strncmp(pattern, "http://", 7) == 0 ||
		strncmp(pattern, "https://", 8) == 0 ||
		strncmp(pattern, "ftp://", 6) == 0 ||
		strncmp(pattern, "file://", 7) == 0)
		return (EPKG_OK);

	return (EPKG_FATAL);
}

void
usage_add(void)
{
	fprintf(stderr, "Usage: pkg add [-IAfqM] <pkg-name> ...\n");
	fprintf(stderr, "       pkg add [-IAfqM] <protocol>://<path>/<pkg-name> ...\n\n");
	fprintf(stderr, "For more information see 'pkg help add'.\n");
}

int
exec_add(int argc, char **argv)
{
	struct pkgdb *db = NULL;
	struct sbuf *failedpkgs = NULL;
	char path[MAXPATHLEN];
	char *file;
	int retcode;
	int ch;
	int i;
	int failedpkgcount = 0;
	pkg_flags f = PKG_FLAG_NONE;
	struct pkg_manifest_key *keys = NULL;
	const char *location = NULL;

	/* options descriptor */
	struct option longopts[] = {
		{ "no-scripts",          no_argument,            NULL,           'I' },
		{ "automatic",           no_argument,            NULL,           'A' },
		{ "force",               no_argument,            NULL,           'f' },
		{ "accept-missing",      no_argument,            NULL,           'M' },
		{ "quiet",               no_argument,            NULL,           'q' },
		{ "relocate",            required_argument,      NULL,            1  },
		{ NULL,                  0,                      NULL,            0  }
	};

	while ((ch = getopt_long(argc, argv, "+IAfqM", longopts, NULL)) != -1) {
		switch (ch) {
		case 'I':
			f |= PKG_ADD_NOSCRIPT;
			break;
		case 'A':
			f |= PKG_ADD_AUTOMATIC;
			break;
		case 'f':
			f |= PKG_ADD_FORCE;
			force = true;
			break;
		case 'M':
			f |= PKG_ADD_FORCE_MISSING;
			break;
		case 'q':
			quiet = true;
			break;
		case 1:
			location = optarg;
			break;
		default:
			usage_add();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_add();
		return (EX_USAGE);
	}

	retcode = pkgdb_access(PKGDB_MODE_READ  |
			       PKGDB_MODE_WRITE |
			       PKGDB_MODE_CREATE,
			       PKGDB_DB_LOCAL);
	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to add packages");
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_EXCLUSIVE) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an exclusive lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	failedpkgs = sbuf_new_auto();
	pkg_manifest_keys_new(&keys);
	for (i = 0; i < argc; i++) {
		if (is_url(argv[i]) == EPKG_OK) {
			snprintf(path, sizeof(path), "%s/%s.XXXXX",
			    getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp", basename(argv[i]));
			if ((retcode = pkg_fetch_file(NULL, argv[i], path, 0)) != EPKG_OK)
				break;

			file = path;
		} else {
			file = argv[i];

			/* Special case: treat a filename of "-" as
			   meaning 'read from stdin.'  It doesn't make
			   sense to have a filename of "-" more than
			   once per command line, but we aren't
			   testing for that at the moment */

			if (strcmp(file, "-") != 0 && access(file, F_OK) != 0) {
				warn("%s", file);
				if (errno == ENOENT)
					warnx("Was 'pkg install %s' meant?", file);
				sbuf_cat(failedpkgs, argv[i]);
				if (i != argc - 1)
					sbuf_printf(failedpkgs, ", ");
				failedpkgcount++;
				continue;
			}

		}

		if ((retcode = pkg_add(db, file, f, keys, location)) != EPKG_OK) {
			sbuf_cat(failedpkgs, argv[i]);
			if (i != argc - 1)
				sbuf_printf(failedpkgs, ", ");
			failedpkgcount++;
		}

		if (is_url(argv[i]) == EPKG_OK)
			unlink(file);

	}
	pkg_manifest_keys_free(keys);
	pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);
	pkgdb_close(db);
	
	if(failedpkgcount > 0) {
		sbuf_finish(failedpkgs);
		printf("\nFailed to install the following %d package(s): %s\n", failedpkgcount, sbuf_data(failedpkgs));
		retcode = EPKG_FATAL;
	}
	sbuf_delete(failedpkgs);

	if (messages != NULL) {
		sbuf_finish(messages);
		printf("%s", sbuf_data(messages));
	}

	return (retcode == EPKG_OK ? EX_OK : EX_SOFTWARE);
}

