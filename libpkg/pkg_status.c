/*-
 * Copyright (c) 2012 Matthew Seaman <matthew@FreeBSD.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include <bsd_compat.h>

#include "pkg.h"


#define _LOCALBASE	"/usr/local"

static bool is_exec_at_localbase(const char *progname);

pkg_status_t
pkg_status(int *count)
{
	const pkg_object	*o;
	const char		*progname;
	char			 dbpath[MAXPATHLEN];
	int			 numpkgs = 0;
	sqlite3			*db = NULL;
	sqlite3_stmt		*stmt = NULL;
	const char		*sql = "SELECT COUNT(*) FROM packages";
	bool			 dbsuccess;

	/* Is this executable called pkg, or does pkg exist at
	   $LOCALBASE/sbin/pkg.  Ditto: pkg-static. Portability:
	   assumes setprogname() has been called */

	progname = getprogname();
	if (progname == NULL)
		return (PKG_STATUS_UNINSTALLED);

	if (strcmp(progname, PKG_EXEC_NAME) != 0   &&
	    strcmp(progname, PKG_STATIC_NAME) != 0 &&
	    !is_exec_at_localbase(PKG_EXEC_NAME)   &&
	    !is_exec_at_localbase(PKG_STATIC_NAME))
		return (PKG_STATUS_UNINSTALLED);

	/* Does the local.sqlite pkg database exist, and can we open
	   it for reading? */

	o = pkg_config_get("PKG_DBDIR");
	snprintf(dbpath, sizeof(dbpath), "%s/local.sqlite", pkg_object_string(o));

	if (eaccess(dbpath, R_OK) == -1)
		return (PKG_STATUS_NODB);
	
	/* Try opening the DB and preparing and running a simple query. */

	dbsuccess = (sqlite3_initialize() == SQLITE_OK);
	if (dbsuccess) {
		dbsuccess = (sqlite3_open_v2(dbpath, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK);
		if (dbsuccess) {
			dbsuccess = (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
			if (dbsuccess) {
				dbsuccess = (sqlite3_step(stmt) == SQLITE_ROW);
				if (dbsuccess) {
					numpkgs = sqlite3_column_int(stmt, 0);
				}
				sqlite3_finalize(stmt);
			}
			sqlite3_close(db);
		}
		sqlite3_shutdown();
	}

	if (!dbsuccess)
		return (PKG_STATUS_NODB);

	/* Save result, if requested */
	if (count != NULL)
		*count = numpkgs;

	return (numpkgs == 0 ? PKG_STATUS_NOPACKAGES : PKG_STATUS_ACTIVE);
}

static bool
is_exec_at_localbase(const char *progname)
{
	char	pkgpath[MAXPATHLEN];
	bool	result = true;

	snprintf(pkgpath, sizeof(pkgpath), "%s/sbin/%s",
		 getenv("LOCALBASE") ? getenv("LOCALBASE") : _LOCALBASE,
		 progname);
	if (access(pkgpath, X_OK) == -1)
		result = false;

	return (result);
}
