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

#ifndef _PKGDB_H
#define _PKGDB_H

#include "pkg.h"

#include "sqlite3.h"

struct pkgdb {
	sqlite3		*sqlite;
	pkgdb_t		 type;
	int		 lock_count;
	bool		 prstmt_initialized;
};

struct pkgdb_it {
	struct pkgdb	*db;
	sqlite3_stmt	*stmt;
	int		 type;
};


/**
 * Transaction/savepoint handling.
 * @param savepoint -- if NULL or an empty string, use BEGIN, ROLLBACK, COMMIT
 * otherwise use SAVEPOINT, ROLLBACK TO, RELEASE.
 * @return an error code.
 */
int pkgdb_transaction_begin(sqlite3 *sqlite, const char *savepoint);
int pkgdb_transaction_commit(sqlite3 *sqlite, const char *savepoint);
int pkgdb_transaction_rollback(sqlite3 *sqlite, const char *savepoint);

struct pkgdb_it *pkgdb_query_delete(struct pkgdb *db, match_t type, int nbpkgs, char **pkgs, int recursive);
struct pkgdb_it *pkgdb_query_autoremove(struct pkgdb *db);
struct pkgdb_it *pkgdb_query_upgrades(struct pkgdb *db, const char *reponame, bool all, bool pkgversiontest);
struct pkgdb_it *pkgdb_query_installs(struct pkgdb *db, match_t type, int nbpkgs, char **pkgs, const char *reponame, bool force, bool recursive, bool pkgversiontest, int *retcode);
struct pkgdb_it *pkgdb_query_fetch(struct pkgdb *db, match_t type, int nbpkgs, char **pkgs, const char *reponame, unsigned flags);

int pkgdb_obtain_lock(struct pkgdb *db);
int pkgdb_release_lock(struct pkgdb *db);

void pkgshell_open(const char **r);
#endif
