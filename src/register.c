/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2013-2014 Matthew Seaman <matthew@FreeBSD.org>
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
#include <stdio.h>
#include <pkg.h>
#include <sysexits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>

#include "pkgcli.h"

void
usage_register(void)
{
	fprintf(stderr, "Usage: pkg register [-ldt] [-i <input-path>]"
	                " [-f <plist-file>] -m <metadatadir>\n");
	fprintf(stderr, "       pkg register [-ldt] [-i <input_path>]"
		        " -M <manifest>\n\n");
	fprintf(stderr, "For more information see 'pkg help register'.\n");
}

int
exec_register(int argc, char **argv)
{
	struct pkg	*pkg = NULL;
	struct pkgdb	*db  = NULL;

	const char	*plist      = NULL;
	const char	*mdir       = NULL;
	const char	*mfile      = NULL;
	const char	*input_path = NULL;
	const char	*location   = NULL;

	bool		 legacy        = false;
	bool		 testing_mode  = false;

	int		 ch;
	int		 ret     = EPKG_OK;
	int		 retcode = EX_OK;

	/* options descriptor */
	struct option longopts[] = {
		{ "automatic",	no_argument,		NULL,	'A' },
		{ "debug",      no_argument,		NULL,	'd' },
		{ "legacy",	no_argument,		NULL,	'l' },
		{ "manifest",	required_argument,	NULL,	'M' },
		{ "metadata",	required_argument,	NULL,	'm' },
		{ "plist",	required_argument,	NULL,	'f' },
		{ "relocate",	required_argument,	NULL, 	1 },
		{ "root",	required_argument,	NULL,	'i' },
		{ "test",	no_argument,		NULL,	't' },
		{ NULL,		0,			NULL,	0},
	};

	if (pkg_new(&pkg, PKG_INSTALLED) != EPKG_OK)
		err(EX_OSERR, "malloc");

	while ((ch = getopt_long(argc, argv, "+Adf:i:lM:m:t", longopts, NULL)) != -1) {
		switch (ch) {
		case 'A':
		case 'd':
			pkg_set(pkg, PKG_AUTOMATIC, (bool)true);
			break;
		case 'f':
			plist = optarg;
			break;
		case 'i':
			input_path = optarg;
			break;
		case 'l':
			legacy = true;
			break;
		case 'M':
			mfile = optarg;
			break;
		case 'm':
			mdir = optarg;
			break;
		case 't':
			testing_mode = true;
			break;
		case 1:
			location = optarg;
			break;
		default:
			warnx("Unrecognised option -%c\n", ch);
			usage_register();
			pkg_free(pkg);
			return (EX_USAGE);
		}
	}

	retcode = pkgdb_access(PKGDB_MODE_READ  |
			       PKGDB_MODE_WRITE |
			       PKGDB_MODE_CREATE,
			       PKGDB_DB_LOCAL);
	if (retcode == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to register packages");
		pkg_free(pkg);
		return (EX_NOPERM);
	} else if (retcode != EPKG_OK) {
		pkg_free(pkg);
		return (EX_IOERR);
	}

	/*
	 * Ideally, the +MANIFEST should be all that is necessary,
	 * since it can contain all of the meta-data supplied by the
	 * other files mentioned below.  These are here for backwards
	 * compatibility with the way the ports tree works with
	 * pkg_tools.
	 * 
	 * The -M option specifies one manifest file to read the
	 * meta-data from, and overrides the use of legacy meta-data
	 * inputs.
	 *
	 * Dependencies, shlibs, files etc. may be derived by
	 * analysing the package files (maybe discovered as the
	 * content of the staging directory) unless -t (testing_mode)
	 * is used.
	 */

	if (mfile != NULL && mdir != NULL) {
		warnx("Cannot use both -m and -M together");
		usage_register();
		pkg_free(pkg);
		return (EX_USAGE);
	}


	if (mfile == NULL && mdir == NULL) {
		warnx("One of either -m or -M flags is required");
		usage_register();
		pkg_free(pkg);
		return (EX_USAGE);
	}

	if (testing_mode && input_path != NULL) {
		warnx("-i incompatible with -t option");
		usage_register();
		pkg_free(pkg);
		return (EX_USAGE);
	}

	ret = pkg_load_metadata(pkg, mfile, mdir, plist, input_path, testing_mode);
	if (ret != EPKG_OK) {
		pkg_free(pkg);
		return (EX_IOERR);
	}


	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkg_free(pkg);
		return (EX_IOERR);
	}

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_EXCLUSIVE) != EPKG_OK) {
		pkgdb_close(db);
		pkg_free(pkg);
		warnx("Cannot get an exclusive lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	retcode = pkg_add_port(db, pkg, input_path, location, testing_mode);

	if (!legacy && retcode == EPKG_OK && messages != NULL) {
		printf("%s\n", utstring_body(messages));
	}

	pkg_free(pkg);
	pkgdb_release_lock(db, PKGDB_LOCK_EXCLUSIVE);

	return (retcode != EPKG_OK ? EX_SOFTWARE : EX_OK);
}
