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

static int
maybe_delete_conflicting(const char *origin, const char *version,
			 const char *pkg_path, bool forced)
{
	int ret = EPKG_FATAL;
	const char *oversion;

	if (run_prepared_statement(VERSION, origin) != SQLITE_ROW)
		return (EPKG_FATAL); /* sqlite error */
	oversion = sqlite3_column_text(STMT(VERSION), 0);
	if (!forced) {
		switch(pkg_version_cmp(oversion, version)) {
		case -1:
			pkg_emit_error("duplicate package origin: replacing older "
					"version %s in repo with package %s for "
					"origin %s", oversion, pkg_path, origin);

			if (run_prepared_statement(DELETE, origin, origin) != SQLITE_DONE)
				return (EPKG_FATAL); /* sqlite error */

			ret = EPKG_OK;	/* conflict cleared */
			break;
		case 0:
		case 1:
			pkg_emit_error("duplicate package origin: package %s is not "
					"newer than version %s already in repo for "
					"origin %s", pkg_path, oversion, origin);
			ret = EPKG_END;	/* keep what is already in the repo */
			break;
		}
	}
	else {
		if (run_prepared_statement(DELETE, origin, origin) != SQLITE_DONE)
			return (EPKG_FATAL); /* sqlite error */

		ret = EPKG_OK;
	}
	return (ret);
}

int
pkgdb_repo_cksum_exists(sqlite3 *sqlite, const char *cksum)
{
	if (run_prepared_statement(EXISTS, cksum) != SQLITE_ROW) {
		ERROR_SQLITE(sqlite, SQL(EXISTS));
		return (EPKG_FATAL);
	}
	if (sqlite3_column_int(STMT(EXISTS), 0) > 0) {
		return (EPKG_OK);
	}
	return (EPKG_END);
}

int
pkgdb_repo_add_package(struct pkg *pkg, const char *pkg_path,
		sqlite3 *sqlite, bool forced)
{
	const char *name, *version, *origin, *comment, *desc;
	const char *arch, *maintainer, *www, *prefix, *sum, *rpath;
	const char *olddigest, *manifestdigest;
	int64_t			 flatsize, pkgsize;
	int64_t			 licenselogic;
	int			 ret;
	struct pkg_dep		*dep      = NULL;
	struct pkg_option	*option   = NULL;
	struct pkg_shlib	*shlib    = NULL;
	const pkg_object	*obj, *licenses, *categories, *annotations;
	pkg_iter		 it;
	int64_t			 package_id;

	pkg_get(pkg, PKG_ORIGIN, &origin, PKG_NAME, &name,
			    PKG_VERSION, &version, PKG_COMMENT, &comment,
			    PKG_DESC, &desc, PKG_ARCH, &arch,
			    PKG_MAINTAINER, &maintainer, PKG_WWW, &www,
			    PKG_PREFIX, &prefix, PKG_FLATSIZE, &flatsize,
			    PKG_LICENSE_LOGIC, &licenselogic, PKG_CKSUM, &sum,
			    PKG_PKGSIZE, &pkgsize, PKG_REPOPATH, &rpath,
			    PKG_LICENSES, &licenses, PKG_CATEGORIES, &categories,
			    PKG_ANNOTATIONS, &annotations, PKG_OLD_DIGEST, &olddigest,
			    PKG_DIGEST, &manifestdigest);

try_again:
	if ((ret = run_prepared_statement(PKG, origin, name, version,
			comment, desc, arch, maintainer, www, prefix,
			pkgsize, flatsize, (int64_t)licenselogic, sum,
			rpath, manifestdigest, olddigest)) != SQLITE_DONE) {
		if (ret == SQLITE_CONSTRAINT) {
			switch(maybe_delete_conflicting(origin,
					version, pkg_path, forced)) {
			case EPKG_FATAL: /* sqlite error */
				ERROR_SQLITE(sqlite, SQL(PKG));
				return (EPKG_FATAL);
				break;
			case EPKG_END: /* repo already has newer */
				return (EPKG_END);
				break;
			default: /* conflict cleared, try again */
				goto try_again;
				break;
			}
		} else {
			ERROR_SQLITE(sqlite, SQL(PKG));
			return (EPKG_FATAL);
		}
	}
	package_id = sqlite3_last_insert_rowid(sqlite);

	if (run_prepared_statement (FTS_APPEND, package_id,
			name, version, origin) != SQLITE_DONE) {
		ERROR_SQLITE(sqlite, SQL(FTS_APPEND));
		return (EPKG_FATAL);
	}

	dep = NULL;
	while (pkg_deps(pkg, &dep) == EPKG_OK) {
		if (run_prepared_statement(DEPS,
				pkg_dep_origin(dep),
				pkg_dep_name(dep),
				pkg_dep_version(dep),
				package_id) != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(DEPS));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(categories, &it))) {
		ret = run_prepared_statement(CAT1, pkg_object_string(obj));
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(CAT2, package_id,
			    pkg_object_string(obj));
		if (ret != SQLITE_DONE)
		{
			ERROR_SQLITE(sqlite, SQL(CAT2));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(licenses, &it))) {
		ret = run_prepared_statement(LIC1, pkg_object_string(obj));
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(LIC2, package_id,
			    pkg_object_string(obj));
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(LIC2));
			return (EPKG_FATAL);
		}
	}
	option = NULL;
	while (pkg_options(pkg, &option) == EPKG_OK) {
		ret = run_prepared_statement(OPT1, pkg_option_opt(option));
		if (ret == SQLITE_DONE)
		    ret = run_prepared_statement(OPT2, pkg_option_opt(option),
				pkg_option_value(option), package_id);
		if(ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(OPT2));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_required(pkg, &shlib) == EPKG_OK) {
		const char *shlib_name = pkg_shlib_name(shlib);

		ret = run_prepared_statement(SHLIB1, shlib_name);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(SHLIB_REQD, package_id,
					shlib_name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(SHLIB_REQD));
			return (EPKG_FATAL);
		}
	}

	shlib = NULL;
	while (pkg_shlibs_provided(pkg, &shlib) == EPKG_OK) {
		const char *shlib_name = pkg_shlib_name(shlib);

		ret = run_prepared_statement(SHLIB1, shlib_name);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(SHLIB_PROV, package_id,
					shlib_name);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(SHLIB_PROV));
			return (EPKG_FATAL);
		}
	}

	it = NULL;
	while ((obj = pkg_object_iterate(annotations, &it))) {
		const char *note_tag = pkg_object_key(obj);
		const char *note_val = pkg_object_string(obj);

		ret = run_prepared_statement(ANNOTATE1, note_tag);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(ANNOTATE1, note_val);
		if (ret == SQLITE_DONE)
			ret = run_prepared_statement(ANNOTATE2, package_id,
				  note_tag, note_val);
		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, SQL(ANNOTATE2));
			return (EPKG_FATAL);
		}
	}

	return (EPKG_OK);
}

int
pkgdb_repo_remove_package(const char *origin)
{
	if (run_prepared_statement(DELETE, origin, origin) != SQLITE_DONE)
		return (EPKG_FATAL); /* sqlite error */

	return (EPKG_OK);
}

/* We want to replace some arbitrary number of instances of the placeholder
   %Q in the SQL with the name of the database. */
static int
substitute_into_sql(char *sqlbuf, size_t buflen, const char *fmt,
		    const char *replacement)
{
	char	*f;
	char	*f0;
	char	*tofree;
	char	*quoted;
	size_t	 len;
	int	 ret = EPKG_OK;

	tofree = f = strdup(fmt);
	if (tofree == NULL)
		return (EPKG_FATAL); /* out of memory */

	quoted = sqlite3_mprintf("%Q", replacement);
	if (quoted == NULL) {
		free(tofree);
		return (EPKG_FATAL); /* out of memory */
	}

	sqlbuf[0] = '\0';

	while ((f0 = strsep(&f, "%")) != NULL) {
		len = strlcat(sqlbuf, f0, buflen);
		if (len >= buflen) {
			/* Overflowed the buffer */
			ret = EPKG_FATAL;
			break;
		}

		if (f == NULL)
			break;	/* done */

		if (f[0] == 'Q') {
			len = strlcat(sqlbuf, quoted, buflen);
			f++;	/* Jump the Q */
		} else {
			len = strlcat(sqlbuf, "%", buflen);
		}

		if (len >= buflen) {
			/* Overflowed the buffer */
			ret = EPKG_FATAL;
			break;
		}
	}

	free(tofree);
	sqlite3_free(quoted);

	return (ret);
}


struct pkgdb_it *
pkgdb_repo_origins(sqlite3 *sqlite)
{
	sqlite3_stmt *stmt = NULL;
	int ret;
	static struct pkgdb repodb;
	const char query_sql[] = ""
		"SELECT id, origin, name, name || '~' || origin as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, path AS repopath, manifestdigest "
		"FROM packages "
		"ORDER BY origin;";

	ret = sqlite3_prepare_v2(sqlite, query_sql, -1,
			&stmt, NULL);
	if (ret != SQLITE_OK) {
		ERROR_SQLITE(sqlite, query_sql);
		return (NULL);
	}
	repodb.sqlite = sqlite;
	repodb.type = PKGDB_REMOTE;

	return pkgdb_it_new(&repodb, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE);
}

int
pkgdb_repo_register_conflicts(const char *origin, char **conflicts,
		int conflicts_num, sqlite3 *sqlite)
{
	const char clean_conflicts_sql[] = ""
			"DELETE FROM pkg_conflicts "
			"WHERE package_id = ?1;";
	const char select_id_sql[] = ""
			"SELECT id FROM packages "
			"WHERE origin = ?1;";
	const char insert_conflict_sql[] = ""
			"INSERT INTO pkg_conflicts "
			"(package_id, conflict_id) "
			"VALUES (?1, ?2);";
	sqlite3_stmt *stmt = NULL;
	int ret, i;
	int64_t origin_id, conflict_id;

	pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", select_id_sql);
	if (sqlite3_prepare_v2(sqlite, select_id_sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, select_id_sql);
		return (EPKG_FATAL);
	}

	sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);
	ret = sqlite3_step(stmt);

	if (ret == SQLITE_ROW) {
		origin_id = sqlite3_column_int64(stmt, 0);
	}
	else {
		ERROR_SQLITE(sqlite, select_id_sql);
		return (EPKG_FATAL);
	}
	sqlite3_finalize(stmt);

	pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", clean_conflicts_sql);
	if (sqlite3_prepare_v2(sqlite, clean_conflicts_sql, -1, &stmt, NULL) != SQLITE_OK) {
		ERROR_SQLITE(sqlite, clean_conflicts_sql);
		return (EPKG_FATAL);
	}

	sqlite3_bind_int64(stmt, 1, origin_id);
	/* Ignore cleanup result */
	(void)sqlite3_step(stmt);

	sqlite3_finalize(stmt);

	for (i = 0; i < conflicts_num; i ++) {
		/* Select a conflict */
		pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", select_id_sql);
		if (sqlite3_prepare_v2(sqlite, select_id_sql, -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(sqlite, select_id_sql);
			return (EPKG_FATAL);
		}

		sqlite3_bind_text(stmt, 1, conflicts[i], -1, SQLITE_TRANSIENT);
		ret = sqlite3_step(stmt);

		if (ret == SQLITE_ROW) {
			conflict_id = sqlite3_column_int64(stmt, 0);
		}
		else {
			ERROR_SQLITE(sqlite, select_id_sql);
			return (EPKG_FATAL);
		}

		sqlite3_finalize(stmt);

		/* Insert a pair */
		pkg_debug(4, "pkgdb_repo_register_conflicts: running '%s'", insert_conflict_sql);
		if (sqlite3_prepare_v2(sqlite, insert_conflict_sql, -1, &stmt, NULL) != SQLITE_OK) {
			ERROR_SQLITE(sqlite, insert_conflict_sql);
			return (EPKG_FATAL);
		}

		sqlite3_bind_int64(stmt, 1, origin_id);
		sqlite3_bind_int64(stmt, 2, conflict_id);
		ret = sqlite3_step(stmt);

		if (ret != SQLITE_DONE) {
			ERROR_SQLITE(sqlite, insert_conflict_sql);
			return (EPKG_FATAL);
		}

		sqlite3_finalize(stmt);
	}

	return (EPKG_OK);
}

const char *
pkgdb_get_reponame(struct pkgdb *db, const char *repo)
{
	const char	*reponame = NULL;
	struct pkg_repo	*r;

	assert(db->type == PKGDB_REMOTE);

	if (repo != NULL) {
		if ((r = pkg_repo_find_ident(repo)) == NULL) {
			pkg_emit_error("repository '%s' does not exist", repo);
			return (NULL);
		}
		reponame = pkg_repo_name(r);

		if (!pkgdb_is_attached(db->sqlite, reponame)) {
			pkg_emit_error("repository '%s' does not exist", repo);
			return (NULL);
		}
	}

	return (reponame);
}

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

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
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

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
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

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
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

	return (pkgdb_it_new(db, stmt, PKG_REMOTE, PKGDB_IT_FLAG_ONCE));
}
