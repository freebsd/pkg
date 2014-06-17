/*-
 * Copyright (c) 2013 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2011-2012 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011 Philippe Pepiot <phil@philpep.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2012-2013 Matthew Seaman <matthew@FreeBSD.org>
 * Copyright (c) 2012 Bryan Drewery <bryan@shatow.net>
 * Copyright (c) 2013 Gerald Pfeifer <gerald@pfeifer.com>
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

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <grp.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <sqlite3.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/repodb.h"


struct pkgdb_it *
pkgdb_rquery(struct pkgdb *db, const char *pattern, match_t match,
    const char *repo)
{
	sqlite3_stmt	*stmt = NULL;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	const char	*comp = NULL;
	int		 ret;
	char		 basesql[BUFSIZ] = ""
		"SELECT id, origin, name, name || '~' || origin as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, manifestdigest, path AS repopath, '%1$s' AS dbname "
		"FROM '%1$s'.packages p";

	assert(db != NULL);
	assert(match == MATCH_ALL || (pattern != NULL && pattern[0] != '\0'));

	/*
	 * If we have no remote repos loaded, we just return nothing instead of failing
	 * an assert deep inside pkgdb_get_reponame
	 */
	if (db->type != PKGDB_REMOTE)
		return (NULL);

	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	comp = pkgdb_get_pattern_query(pattern, match);
	if (comp && comp[0])
		strlcat(basesql, comp, sizeof(basesql));

	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
		    basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame, reponame);

	sbuf_cat(sql, " ORDER BY name;");
	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s' query for %s", sbuf_get(sql), pattern);
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), sbuf_size(sql), &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	if (match != MATCH_ALL && match != MATCH_CONDITION)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_rquery_provide(struct pkgdb *db, const char *provide, const char *repo)
{
	sqlite3_stmt	*stmt;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	int		 ret;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name || '~' || p.origin as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%1$s' AS dbname "
			"FROM '%1$s'.packages AS p, '%1$s'.pkg_provides AS pp, "
			"'%1$s'.provides AS pr "
			"WHERE p.id = pp.package_id "
			"AND pp.provide_id = pr.id "
			"AND pr.name = ?1;";

	assert(db != NULL);
	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
				basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame);

	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s'", sbuf_get(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_find_shlib_provide(struct pkgdb *db, const char *require, const char *repo)
{
	sqlite3_stmt	*stmt;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	int		 ret;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name || '~' || p.origin as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%1$s' AS dbname "
			"FROM '%1$s'.packages AS p INNER JOIN '%1$s'.pkg_shlibs_provided AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id IN (SELECT id FROM '%1$s'.shlibs WHERE "
			"name BETWEEN ?1 AND ?1 || '.9');";

	assert(db != NULL);
	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
				basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame);

	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s'", sbuf_get(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, require, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}

struct pkgdb_it *
pkgdb_find_shlib_require(struct pkgdb *db, const char *provide, const char *repo)
{
	sqlite3_stmt	*stmt;
	struct sbuf	*sql = NULL;
	const char	*reponame = NULL;
	int		 ret;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name || '~' || p.origin as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%1$s' AS dbname "
			"FROM '%1$s'.packages AS p INNER JOIN '%1$s'.pkg_shlibs_required AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id = (SELECT id FROM '%1$s'.shlibs WHERE name=?1);";

	assert(db != NULL);
	reponame = pkgdb_get_reponame(db, repo);

	sql = sbuf_new_auto();
	/*
	 * Working on multiple remote repositories
	 */
	if (reponame == NULL) {
		/* duplicate the query via UNION for all the attached
		 * databases */

		ret = pkgdb_sql_all_attached(db->sqlite, sql,
				basesql, " UNION ALL ");
		if (ret != EPKG_OK) {
			sbuf_delete(sql);
			return (NULL);
		}
	} else
		sbuf_printf(sql, basesql, reponame);

	sbuf_finish(sql);

	pkg_debug(4, "Pkgdb: running '%s'", sbuf_get(sql));
	ret = sqlite3_prepare_v2(db->sqlite, sbuf_get(sql), -1, &stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(db->sqlite, sbuf_get(sql));
		sbuf_delete(sql);
		return (NULL);
	}

	sbuf_delete(sql);

	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);

	return (pkgdb_it_new_sqlite(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}
