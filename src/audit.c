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

#include <pkg.h>
#include "pkgcli.h"

void
usage_audit(void)
{
	fprintf(stderr, "Usage: pkg audit [-Fq] <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help audit'.\n");
}

struct pkg_check_entry {
	struct pkg *pkg;
	struct pkg_check_entry *next;
};

static void
add_to_check(struct pkg_check_entry **head, struct pkg *pkg)
{
	struct pkg_check_entry *e;

	e = malloc(sizeof(*e));
	if (e == NULL) {
		warnx("malloc failed for pkg_check_entry");
		exit(EXIT_FAILURE);
	}
	e->pkg = pkg;
	LL_PREPEND(*head, e);
}

int
exec_audit(int argc, char **argv)
{
	struct pkg_audit	*audit;
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
	int				 ret = EX_OK;
	const char			*portaudit_site = NULL;
	struct sbuf			*sb;
	struct pkg_check_entry *check = NULL, *cur;

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

	audit = pkg_audit_new();

	if (fetch == true) {
		portaudit_site = pkg_object_string(pkg_config_get("VULNXML_SITE"));
		if (pkg_audit_fetch(portaudit_site, audit_file) != EPKG_OK) {
			return (EX_IOERR);
		}
	}

	if (pkg_audit_load(audit, audit_file) != EPKG_OK) {
		if (errno == ENOENT)
			warnx("vulnxml file %s does not exist. "
					"Try running 'pkg audit -F' first",
					audit_file);
		else
			warn("unable to open vulnxml file %s",
					audit_file);

		return (EX_DATAERR);
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
		add_to_check(&check, pkg);
	}
	else {

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

		if (pkgdb_obtain_lock(db, PKGDB_LOCK_READONLY) != EPKG_OK) {
			pkgdb_close(db);
			warnx("Cannot get a read lock on a database, it is locked by another process");
			return (EX_TEMPFAIL);
		}

		if ((it = pkgdb_query(db, NULL, MATCH_ALL)) == NULL) {
			warnx("Error accessing the package database");
			ret = EX_IOERR;
		}
		else {
			while ((ret = pkgdb_it_next(it, &pkg, PKG_LOAD_BASIC)) == EPKG_OK) {
				add_to_check(&check, pkg);
				pkg = NULL;
			}
			ret = EX_OK;
		}
		if (db != NULL) {
			if (it != NULL)
				pkgdb_it_free(it);
			pkgdb_release_lock(db, PKGDB_LOCK_READONLY);
			pkgdb_close(db);
		}
		if (ret != EX_OK)
			return (ret);
	}

	/* Now we have vulnxml loaded and check list formed */
#ifdef HAVE_CAPSICUM
	if (cap_enter() < 0 && errno != ENOSYS) {
		warn("cap_enter() failed");
		return (EPKG_FATAL);
	}
#endif

	if (pkg_audit_process(audit) == EPKG_OK) {
		LL_FOREACH(check, cur) {
			if (pkg_audit_is_vulnerable(audit, cur->pkg, quiet, &sb)) {
				vuln ++;
				printf("%s", sbuf_data(sb));
				sbuf_delete(sb);
			}
		}

		if (ret == EPKG_END && vuln == 0)
			ret = EX_OK;

		if (!quiet)
			printf("%u problem(s) in the installed packages found.\n", vuln);
	}
	else {
		warnx("cannot process vulnxml");
		ret = EX_SOFTWARE;
	}

	pkg_audit_free(audit);

	return (ret);
}
