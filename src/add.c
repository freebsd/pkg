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

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <xstring.h>

#include <pkg.h>

#include "pkgcli.h"

static int
is_url(const char * const pattern)
{
	if (strncmp(pattern, "http://", 7) == 0 ||
		strncmp(pattern, "https://", 8) == 0 ||
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
	xstring *failedpkgs = NULL;
	char path[MAXPATHLEN];
	char *env, *file;
	int retcode;
	int ch;
	int i;
	int failedpkgcount = 0;
	int scriptnoexec = 0;
	pkg_flags f = PKG_FLAG_NONE;
	const char *location = NULL;

	/* options descriptor */
	struct option longopts[] = {
		{ "no-scripts",          no_argument,            NULL,           'I' },
		{ "script-no-exec",      no_argument,            &scriptnoexec,  1 },
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
		case 0:
			if (scriptnoexec == 1)
				f |= PKG_ADD_NOEXEC;
			break;
		default:
			usage_add();
			return (EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage_add();
		return (EXIT_FAILURE);
	}

	retcode = pkgdb_access(PKGDB_MODE_READ  |
			       PKGDB_MODE_WRITE |
			       PKGDB_MODE_CREATE,
			       PKGDB_DB_LOCAL);
	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to add packages");
		return (EXIT_FAILURE);
	} else if (retcode != EPKG_OK)
		return (EXIT_FAILURE);

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EXIT_FAILURE);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_EXCLUSIVE) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get an exclusive lock on a database, it is locked by another process");
		return (EXIT_FAILURE);
	}

	failedpkgs = xstring_new();
	for (i = 0; i < argc; i++) {
		if (is_url(argv[i]) == EPKG_OK) {
			const char *name = strrchr(argv[i], '/');
			if (name == NULL)
				name = argv[i];
			else
				name++;

			if ((env = getenv("TMPDIR")) == NULL)
				env = "/tmp";
			snprintf(path, sizeof(path), "%s/%s.XXXXX", env, name);
			if ((retcode = pkg_fetch_file(NULL, argv[i], path, 0, 0, 0)) != EPKG_OK)
				break;

			file = path;
		} else {
			file = argv[i];

			/* Special case: treat a filename of "-" as
			   meaning 'read from stdin.'  It doesn't make
			   sense to have a filename of "-" more than
			   once per command line, but we aren't
			   testing for that at the moment */

			if (!STREQ(file, "-") && access(file, F_OK) != 0) {
				warn("%s", file);
				if (errno == ENOENT)
					warnx("Was 'pkg install %s' meant?", file);
				fprintf(failedpkgs->fp, "%s", argv[i]);
				if (i != argc - 1)
					fprintf(failedpkgs->fp, ", ");
				failedpkgcount++;
				continue;
			}

		}

		if ((retcode = pkg_add(db, file, f, location)) != EPKG_OK) {
			fprintf(failedpkgs->fp, "%s", argv[i]);
			if (i != argc - 1)
				fprintf(failedpkgs->fp, ", ");
			failedpkgcount++;
		}

		if (is_url(argv[i]) == EPKG_OK)
			unlink(file);

	}
	pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);
	pkgdb_close(db);

	if(failedpkgcount > 0) {
		fflush(failedpkgs->fp);
		printf("\nFailed to install the following %d package(s): %s\n", failedpkgcount, failedpkgs->buf);
		retcode = EPKG_FATAL;
	}
	xstring_free(failedpkgs);

	pkg_add_triggers();
	if (messages != NULL) {
		fflush(messages->fp);
		printf("%s", messages->buf);
	}

	return (retcode == EPKG_OK ? EXIT_SUCCESS : EXIT_FAILURE);
}

