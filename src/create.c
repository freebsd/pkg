/*-
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
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

#ifdef HAVE_CONFIG_H
#include "pkg_config.h"
#endif

#include <sys/param.h>
#include <sys/queue.h>

#ifdef PKG_COMPAT
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#endif

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <pkg.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "pkgcli.h"

struct pkg_entry {
	struct pkg *pkg;
	STAILQ_ENTRY(pkg_entry) next;
};

STAILQ_HEAD(pkg_head, pkg_entry);

void
usage_create(void)
{
	fprintf(stderr, "Usage: pkg create [-On] [-f format] [-o outdir] "
		"[-p plist] [-r rootdir] -m metadatadir\n");
	fprintf(stderr, "Usage: pkg create [-On] [-f format] [-o outdir] "
		"[-r rootdir] -M manifest\n");
	fprintf(stderr, "       pkg create [-Ognx] [-f format] [-o outdir] "
		"[-r rootdir] pkg-name ...\n");
	fprintf(stderr, "       pkg create [-On] [-f format] [-o outdir] "
		"[-r rootdir] -a\n\n");
	fprintf(stderr, "For more information see 'pkg help create'.\n");
}

static int
pkg_create_matches(int argc, char **argv, match_t match, pkg_formats fmt,
    const char * const outdir, bool overwrite)
{
	int i, ret = EPKG_OK, retcode = EPKG_OK;
	struct pkg *pkg = NULL;
	struct pkgdb *db = NULL;
	struct pkgdb_it *it = NULL;
	int query_flags = PKG_LOAD_DEPS | PKG_LOAD_FILES |
	    PKG_LOAD_CATEGORIES | PKG_LOAD_DIRS | PKG_LOAD_SCRIPTS |
	    PKG_LOAD_OPTIONS | PKG_LOAD_LICENSES |
	    PKG_LOAD_USERS | PKG_LOAD_GROUPS | PKG_LOAD_SHLIBS_REQUIRED |
	    PKG_LOAD_SHLIBS_PROVIDED | PKG_LOAD_ANNOTATIONS;
	struct pkg_head head = STAILQ_HEAD_INITIALIZER(head);
	struct pkg_entry *e = NULL;
	char pkgpath[MAXPATHLEN];
	const char *format = NULL;
	bool foundone;

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK) {
		pkgdb_close(db);
		return (EX_IOERR);
	}
	/* XXX: get rid of hardcoded timeouts */
	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	switch (fmt) {
	case TXZ:
		format = "txz";
		break;
	case TBZ:
		format = "tbz";
		break;
	case TGZ:
		format = "tgz";
		break;
	case TAR:
		format = "tar";
		break;
	}

	for (i = 0; i < argc || match == MATCH_ALL; i++) {
		if (match == MATCH_ALL) {
			printf("Loading the package list...\n");
			if ((it = pkgdb_query(db, NULL, match)) == NULL)
				goto cleanup;
			match = !MATCH_ALL;
		} else
			if ((it = pkgdb_query(db, argv[i], match)) == NULL)
				goto cleanup;

		foundone = false;
		while ((ret = pkgdb_it_next(it, &pkg, query_flags)) == EPKG_OK) {
			if ((e = malloc(sizeof(struct pkg_entry))) == NULL)
				err(1, "malloc(pkg_entry)");
			e->pkg = pkg;
			pkg = NULL;
			STAILQ_INSERT_TAIL(&head, e, next);
			foundone = true;
		}
		if (!foundone) {
			warnx("No installed package matching \"%s\" found\n",
			    argv[i]);
			retcode++;
		}

		pkgdb_it_free(it);
		if (ret != EPKG_END)
			retcode++;
	}

	while (!STAILQ_EMPTY(&head)) {
		e = STAILQ_FIRST(&head);
		STAILQ_REMOVE_HEAD(&head, next);

		if (!overwrite) {
			pkg_snprintf(pkgpath, sizeof(pkgpath), "%S/%n-%v.%S",
			    outdir, e->pkg, e->pkg, format);
			if (access(pkgpath, F_OK) == 0) {
				pkg_printf("%n-%v already packaged, skipping...\n",
				    e->pkg, e->pkg);
				pkg_free(e->pkg);
				free(e);
				continue;
			}
		}
		pkg_printf("Creating package for %n-%v\n", e->pkg, e->pkg);
		if (pkg_create_installed(outdir, fmt, e->pkg) !=
		    EPKG_OK)
			retcode++;
		pkg_free(e->pkg);
		free(e);
	}

cleanup:
	pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);

	return (retcode);
}

/*
 * options:
 * -x: regex
 * -g: globbing
 * -r: rootdir for the package
 * -m: path to dir where to find the metadata
 * -M: manifest file
 * -f <format>: format could be txz, tgz, tbz or tar
 * -o: output directory where to create packages by default ./ is used
 */

int
exec_create(int argc, char **argv)
{
	match_t		 match = MATCH_EXACT;
	const char	*outdir = NULL;
	const char	*format = NULL;
	const char	*rootdir = NULL;
	const char	*metadatadir = NULL;
	const char	*manifest = NULL;
	char		*plist = NULL;
	pkg_formats	 fmt;
	int		 ch;
	bool		 overwrite = true;

	struct option longopts[] = {
		{ "all",	no_argument,		NULL,	'a' },
		{ "glob",	no_argument,		NULL,	'g' },
		{ "regex",	no_argument,		NULL,	'x' },
		{ "format",	required_argument,	NULL,	'f' },
		{ "root-dir",	required_argument,	NULL,	'r' },
		{ "metadata",	required_argument,	NULL,	'm' },
		{ "manifest",	required_argument,	NULL,	'M' },
		{ "out-dir",	required_argument,	NULL,	'o' },
		{ "no-clobber", no_argument,		NULL,	'n' },
		{ "plist",	required_argument,	NULL,	'p' },
		{ NULL,		0,			NULL,	0   },
	};

	while ((ch = getopt_long(argc, argv, "+agxf:r:m:M:o:np:", longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		case 'f':
			format = optarg;
			break;
		case 'o':
			outdir = optarg;
			break;
		case 'r':
			rootdir = optarg;
			break;
		case 'm':
			metadatadir = optarg;
			break;
		case 'M':
			manifest = optarg;
			break;
		case 'n':
			overwrite = false;
			break;
		case 'p':
			plist = optarg;
			break;
		default:
			usage_create();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (match != MATCH_ALL && metadatadir == NULL && manifest == NULL &&
	    argc == 0) {
		usage_create();
		return (EX_USAGE);
	}

	if (metadatadir == NULL && manifest == NULL && rootdir != NULL) {
		warnx("Do not specify a rootdir when creating a package from an installed package");
		usage_create();
		return (EX_USAGE);
	}

	if (outdir == NULL)
		outdir = "./";

	if (format == NULL) {
		fmt = TXZ;
	} else {
		if (format[0] == '.')
			++format;
		if (strcmp(format, "txz") == 0)
			fmt = TXZ;
		else if (strcmp(format, "tbz") == 0)
			fmt = TBZ;
		else if (strcmp(format, "tgz") == 0)
			fmt = TGZ;
		else if (strcmp(format, "tar") == 0)
			fmt = TAR;
		else {
			warnx("unknown format %s, using txz", format);
			fmt = TXZ;
		}
	}

	if (metadatadir == NULL && manifest == NULL) {
		return (pkg_create_matches(argc, argv, match, fmt, outdir,
		    overwrite) == EPKG_OK ? EX_OK : EX_SOFTWARE);
	} else if (metadatadir != NULL) {
		return (pkg_create_staged(outdir, fmt, rootdir, metadatadir,
		    plist) == EPKG_OK ? EX_OK : EX_SOFTWARE);
	} else  { /* (manifest != NULL) */
		return (pkg_create_from_manifest(outdir, fmt, rootdir,
		    manifest) == EPKG_OK ? EX_OK : EX_SOFTWARE);
	}
}

