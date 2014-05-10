/*-
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
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
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define _WITH_GETLINE

#include <archive.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <utlist.h>

#include <expat.h>

#include <pkg.h>
#include "pkgcli.h"

void
usage_audit(void)
{
	fprintf(stderr, "Usage: pkg audit [-Fq] <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help audit'.\n");
}

int
exec_audit(int argc, char **argv)
{
	struct audit_entry		*h = NULL;
	struct audit_entry_sorted	*cooked_audit_entries = NULL;
	struct pkgdb			*db = NULL;
	struct pkgdb_it			*it = NULL;
	struct pkg			*pkg = NULL;
	const char			*db_dir;
	char				*name;
	char				*version;
	char				 audit_file_buf[MAXPATHLEN];
	char				*audit_file = audit_file_buf;
	unsigned int			 vuln = 0;
	bool				 fetch = false;
	int				 ch;
	int				 ret = EX_OK, res;
	const char			*portaudit_site = NULL;

	db_dir = pkg_object_string(pkg_config_get("PKG_DBDIR"));
	snprintf(audit_file_buf, sizeof(audit_file_buf), "%s/vuln.xml", db_dir);

	while ((ch = getopt(argc, argv, "qFf:")) != -1) {
		switch (ch) {
		case 'q':
			quiet = true;
			break;
		case 'F':
			fetch = true;
			break;
		case 'f':
			audit_file = optarg;
			break;
		default:
			usage_audit();
			return(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (fetch == true) {
		portaudit_site = pkg_object_string(pkg_config_get("VULNXML_SITE"));
		if (fetch_and_extract(portaudit_site, audit_file) != EPKG_OK) {
			return (EX_IOERR);
		}
	}

	if (argc > 2) {
		usage_audit();
		return (EX_USAGE);
	}

	if (argc == 1) {
		name = argv[0];
		version = strrchr(name, '-');
		if (version == NULL)
			err(EX_USAGE, "bad package name format: %s", name);
		version[0] = '\0';
		version++;
		if (pkg_new(&pkg, PKG_FILE) != EPKG_OK)
			err(EX_OSERR, "malloc");
		pkg_set(pkg,
		    PKG_NAME, name,
		    PKG_VERSION, version);
		res = parse_db_vulnxml(audit_file, &h);
		if (res != EPKG_OK) {
			if (errno == ENOENT)
				warnx("vulnxml file %s does not exist. "
				      "Try running 'pkg audit -F' first",
				      audit_file);
			else
				warn("unable to open vulnxml file %s",
				     audit_file);
			ret = EX_DATAERR;
			goto cleanup;
		}
		cooked_audit_entries = preprocess_db(h);
		is_vulnerable(cooked_audit_entries, pkg);
		goto cleanup;
	}

	/*
	 * if the database doesn't exist it just means there are no
	 * packages to audit.
	 */

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL);
	if (ret == EPKG_ENODB) 
		return (EX_OK);
	else if (ret == EPKG_ENOACCESS) {
		warnx("Insufficient privileges to read the package database");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK) {
		warnx("Error accessing the package database");
		return (EX_IOERR);
	}

	if (pkgdb_open(&db, PKGDB_DEFAULT) != EPKG_OK)
		return (EX_IOERR);

	if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY, 0, 0) != EPKG_OK) {
		pkgdb_close(db);
		warnx("Cannot get a read lock on a database, it is locked by another process");
		return (EX_TEMPFAIL);
	}

	if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL)
	{
		warnx("Error accessing the package database");
		ret = EX_IOERR;
		goto cleanup;
	}

	res = parse_db_vulnxml(audit_file, &h);
	if (res != EPKG_OK) {
		if (errno == ENOENT)
			warnx("unable to open vulnxml file, try running 'pkg audit -F' first");
		else
			warn("unable to open vulnxml file %s", audit_file);
		ret = EX_DATAERR;
		goto cleanup;
	}
	cooked_audit_entries = preprocess_db(h);

	while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK)
		if (is_vulnerable(cooked_audit_entries, pkg))
			vuln++;

	if (ret == EPKG_END && vuln == 0)
		ret = EX_OK;

	if (!quiet)
		printf("%u problem(s) in the installed packages found.\n", vuln);

cleanup:
	pkgdb_it_free(it);
	if (db != NULL)
		pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
	pkgdb_close(db);
	pkg_free(pkg);
	free_audit_list(h);

	return (ret);
}
