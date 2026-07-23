/*
 * Copyright (c) 2014, Vsevolod Stakhov
 * Copyright (c) 2024-2026, Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2023, Serenity Cyber Security, LLC
 *                     Author: Gleb Popov <arrowd@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
#include <fcntl.h>
#include <fnmatch.h>
#include <inttypes.h>

#include <archive.h>
#include <archive_entry.h>

#include <sqlite3.h>

#include "pkg.h"
#include "private/event.h"
#include "private/pkg.h"
#include "private/pkgdb.h"
#include "private/utils.h"
#include "binary.h"

static struct pkg_repo_it* pkg_repo_binary_it_new(struct pkg_repo *repo,
	sqlite3_stmt *s, short flags);

struct pkg_repo_group {
	size_t index;
	ucl_object_t *groups;
};

/*
 * Iterator for file_which glob mode.
 * Stores (pkgid, realpath) pairs and returns one row per match.
 */
struct file_which_pair {
	int64_t pkgid;
	char  *path;
};
typedef vec_t(struct file_which_pair) fwpairv_t;

struct pkg_repo_file_which_glob {
	fwpairv_t  pairs;
	size_t     index;
	sqlite3    *sqlite;
	sqlite3_stmt *pkg_stmt;
	bool       pkg_cached;
	int64_t    last_pkgid;
	struct pkg *last_pkg;
};

static int pkg_repo_binary_it_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags);
static void pkg_repo_binary_it_free(struct pkg_repo_it *it);
static void pkg_repo_binary_it_reset(struct pkg_repo_it *it);

static int pkg_repo_binary_group_it_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags);
static void pkg_repo_binary_group_it_free(struct pkg_repo_it *it);
static void pkg_repo_binary_group_it_reset(struct pkg_repo_it *it);

static int pkg_repo_binary_file_which_glob_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags);
static void pkg_repo_binary_file_which_glob_free(struct pkg_repo_it *it);
static void pkg_repo_binary_file_which_glob_reset(struct pkg_repo_it *it);

static const struct pkg_repo_it_ops pkg_repo_binary_it_ops = {
	.next = pkg_repo_binary_it_next,
	.free = pkg_repo_binary_it_free,
	.reset = pkg_repo_binary_it_reset
};

static const struct pkg_repo_it_ops pkg_repo_binary_group_it_ops = {
	.next = pkg_repo_binary_group_it_next,
	.free = pkg_repo_binary_group_it_free,
	.reset = pkg_repo_binary_group_it_reset
};

static const struct pkg_repo_it_ops pkg_repo_binary_file_which_glob_it_ops = {
	.next = pkg_repo_binary_file_which_glob_next,
	.free = pkg_repo_binary_file_which_glob_free,
	.reset = pkg_repo_binary_file_which_glob_reset
};

static struct pkg_repo_it*
pkg_repo_binary_it_new(struct pkg_repo *repo, sqlite3_stmt *s, short flags)
{
	struct pkg_repo_it *it;
	struct pkgdb fakedb;

	it = xmalloc(sizeof(*it));

	it->ops = &pkg_repo_binary_it_ops;
	it->flags = flags;
	it->repo = repo;

	fakedb.sqlite = PRIV_GET(repo);
	it->data = pkgdb_it_new_sqlite(&fakedb, s, PKG_REMOTE, flags);

	if (it->data == NULL) {
		free(it);
		return (NULL);
	}

	return (it);
}

static struct pkg_repo_it *
pkg_repo_binary_group_it_new(struct pkg_repo *repo, ucl_object_t *matching)
{
	struct pkg_repo_group *prg;
	struct pkg_repo_it *it;

	it = xcalloc(1, sizeof(*it));
	prg = xcalloc(1, sizeof(*prg));
	prg->groups = matching;
	it->repo = repo;
	it->ops = &pkg_repo_binary_group_it_ops;
	it->data = prg;

	return (it);
}

static int
pkg_repo_binary_it_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags)
{
	return (pkgdb_it_next(it->data, pkg_p, flags));
}

static int
pkg_repo_binary_group_it_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags __unused)
{
	int ret;
	struct pkg_repo_group *prg;
	const ucl_object_t *o, *el, *ar;
	ucl_object_iter_t oit = NULL;

	prg = it->data;
	if (prg->index == ucl_array_size(prg->groups))
		return (EPKG_END);

	el = ucl_array_find_index(prg->groups, prg->index);
	prg->index++;
	pkg_free(*pkg_p);
	if ((ret = pkg_new(pkg_p, PKG_GROUP_REMOTE)) != EPKG_OK)
		return (ret);
	o = ucl_object_find_key(el, "name");
	xasprintf(&(*pkg_p)->name, ucl_object_tostring(o));
	xasprintf(&(*pkg_p)->uid, "@%s", (*pkg_p)->name);
	o = ucl_object_find_key(el, "comment");
	xasprintf(&(*pkg_p)->comment, ucl_object_tostring(o));
	ar = ucl_object_find_key(el, "depends");
	while ((o = ucl_iterate_object(ar, &oit, true))) {
		pkg_adddep(*pkg_p, ucl_object_tostring(o), NULL, NULL, false);
	}
	pkg_kv_add(&(*pkg_p)->annotations, "repository",   it->repo->name, "annotation");

	return (EPKG_OK);
}

static void
pkg_repo_binary_it_free(struct pkg_repo_it *it)
{
	pkgdb_it_free(it->data);
	free(it);
}

static void
pkg_repo_binary_group_it_free(struct pkg_repo_it *it)
{
	struct pkg_repo_group *prg = it->data;
	free(prg->groups);
	free(prg);
	free(it);
}

static void
pkg_repo_binary_it_reset(struct pkg_repo_it *it)
{
	pkgdb_it_reset(it->data);
}

static void
pkg_repo_binary_group_it_reset(struct pkg_repo_it *it)
{
	struct pkg_repo_group *prg = it->data;

	prg->index = 0;
}

/*
 * file_which glob iterator implementation.
 * Iterates over (pkgid, realpath) pairs, returning one row per pair.
 * The realpath is stored in pkg->rwhich_path so the caller can display it.
 */

static void
populate_pkg_from_stmt(sqlite3_stmt *stmt, struct pkg *pkg)
{
	int icol;
	const unsigned char *val;

	for (icol = 0; icol < sqlite3_column_count(stmt); icol++) {
		const char *colname = sqlite3_column_name(stmt, icol);

		if (strcmp(colname, "id") == 0)
			pkg->id = sqlite3_column_int64(stmt, icol);
		else if (strcmp(colname, "origin") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->origin);
			pkg->origin = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "name") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->name);
			pkg->name = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "version") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->version);
			pkg->version = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "comment") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->comment);
			pkg->comment = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "uniqueid") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->uid);
			pkg->uid = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "prefix") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->prefix);
			pkg->prefix = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "desc") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->desc);
			pkg->desc = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "arch") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->abi);
			pkg->abi = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "maintainer") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->maintainer);
			pkg->maintainer = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "www") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->www);
			pkg->www = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "flatsize") == 0)
			pkg->flatsize = sqlite3_column_int64(stmt, icol);
		else if (strcmp(colname, "pkgsize") == 0)
			pkg->pkgsize = sqlite3_column_int64(stmt, icol);
		else if (strcmp(colname, "cksum") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->sum);
			pkg->sum = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "manifestdigest") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->digest);
			pkg->digest = val ? xstrdup((const char *)val) : NULL;
		} else if (strcmp(colname, "repopath") == 0) {
			val = sqlite3_column_text(stmt, icol);
			free(pkg->repopath);
			pkg->repopath = val ? xstrdup((const char *)val) : NULL;
		}
	}
}

static int
pkg_repo_binary_file_which_glob_next(struct pkg_repo_it *it, struct pkg **pkg_p, unsigned flags __unused)
{
	struct pkg_repo_file_which_glob *fglob = it->data;
	sqlite3_stmt *stmt = fglob->pkg_stmt;

	while (fglob->index < fglob->pairs.len) {
		struct file_which_pair *pair = &fglob->pairs.d[fglob->index];
		fglob->index++;

		/* If same package as last time, just update the path */
		if (pair->pkgid == fglob->last_pkgid && fglob->last_pkg != NULL) {
			free(fglob->last_pkg->rwhich_path);
			fglob->last_pkg->rwhich_path = xstrdup(pair->path);
			*pkg_p = fglob->last_pkg;
			fglob->last_pkg = NULL;
			return (EPKG_OK);
		}

		/* Load package from database */
		pkg_free(fglob->last_pkg);
		fglob->last_pkg = NULL;
		fglob->last_pkgid = pair->pkgid;

		sqlite3_reset(stmt);
		sqlite3_bind_int64(stmt, 2, pair->pkgid);

		if (sqlite3_step(stmt) != SQLITE_ROW)
			continue;

		pkg_free(*pkg_p);
		int ret = pkg_new(pkg_p, PKG_REMOTE);
		if (ret != EPKG_OK)
			return (ret);

		/* Manually populate from the statement */
		populate_pkg_from_stmt(stmt, *pkg_p);

		free((*pkg_p)->rwhich_path);
		(*pkg_p)->rwhich_path = xstrdup(pair->path);

		(*pkg_p)->repo = it->repo;

		/* Check if the next pair is the same package - if so, cache */
		if (fglob->index < fglob->pairs.len &&
		    fglob->pairs.d[fglob->index].pkgid == pair->pkgid) {
			fglob->last_pkg = *pkg_p;
			fglob->last_pkgid = pair->pkgid;
		}

		return (EPKG_OK);
	}

	return (EPKG_END);
}

static void
pkg_repo_binary_file_which_glob_free(struct pkg_repo_it *it)
{
	struct pkg_repo_file_which_glob *fglob = it->data;

	if (fglob != NULL) {
		for (size_t i = 0; i < fglob->pairs.len; i++)
			free(fglob->pairs.d[i].path);
		vec_free(&fglob->pairs);
		pkg_free(fglob->last_pkg);
		sqlite3_finalize(fglob->pkg_stmt);
		free(fglob);
	}
	free(it);
}

static void
pkg_repo_binary_file_which_glob_reset(struct pkg_repo_it *it)
{
	struct pkg_repo_file_which_glob *fglob = it->data;

	fglob->index = 0;
	pkg_free(fglob->last_pkg);
	fglob->last_pkg = NULL;
	fglob->last_pkgid = -1;
}

struct pkg_repo_it *
pkg_repo_binary_groupquery(struct pkg_repo *repo, const char *pattern, match_t match)
{
	return (pkg_repo_binary_groupsearch(repo, pattern, match, FIELD_NAME));
}

struct pkg_repo_it *
pkg_repo_binary_query(struct pkg_repo *repo, const char *cond, const char *pattern, match_t match)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	sqlite3_stmt	*stmt = NULL;
	char *sql = NULL;
	const char	*comp = NULL;
	const char basesql_quick[] = ""
		"SELECT DISTINCT(p.id), origin, p.name, p.name as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, manifestdigest, path AS repopath, '%s' AS dbname "
		"FROM packages  as p "
		" %s "
		"%s%s%s "
		"ORDER BY p.name;";
	const char basesql[] = ""
		"SELECT DISTINCT(p.id), origin, p.name, p.name as uniqueid, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, manifestdigest, path AS repopath, '%s' AS dbname "
		"FROM packages  as p "
		"LEFT JOIN pkg_categories ON p.id = pkg_categories.package_id "
		"LEFT JOIN categories ON categories.id = pkg_categories.category_id "
		"LEFT JOIN flavors ON flavors.package_id = p.id "
		" %s "
		"%s%s%s "
		"ORDER BY p.name;";

	const char *bsql = (match == MATCH_INTERNAL) ? basesql_quick : basesql;

	if (match != MATCH_ALL && (pattern == NULL || pattern[0] == '\0'))
		return (NULL);

	comp = pkgdb_get_pattern_query(pattern, match);
	if (comp == NULL)
		comp = "";
	if (cond == NULL)
		xasprintf(&sql, bsql, repo->name, comp, "", "", "");
	else
		xasprintf(&sql, bsql, repo->name, comp,
		    comp[0] != '\0' ? "AND (" : "WHERE ( ", cond + 7, " )");

	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	if (match != MATCH_ALL)
		sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_shlib_provide(struct pkg_repo *repo, const char *require)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_shlibs_provided AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id IN (SELECT id FROM shlibs WHERE "
			"name BETWEEN ?1 AND ?1 || '.9');";

	xasprintf(&sql, basesql, repo->name);

	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, require, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_provide(struct pkg_repo *repo, const char *require)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_provides AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.provide_id IN (SELECT id from provides WHERE "
			"provide = ?1 );";

	xasprintf(&sql, basesql, repo->name);

	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, require, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_shlib_require(struct pkg_repo *repo, const char *provide)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_shlibs_required AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.shlib_id = (SELECT id FROM shlibs WHERE name=?1);";

	xasprintf(&sql, basesql, repo->name);

	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	pkg_debug(1, "> loading provides");
	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_require(struct pkg_repo *repo, const char *provide)
{
	sqlite3_stmt	*stmt;
	sqlite3 *sqlite = PRIV_GET(repo);
	char *sql = NULL;
	const char	 basesql[] = ""
			"SELECT p.id, p.origin, p.name, p.version, p.comment, "
			"p.name as uniqueid, "
			"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
			"p.licenselogic, p.flatsize, p.pkgsize, "
			"p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
			"FROM packages AS p INNER JOIN pkg_requires AS ps ON "
			"p.id = ps.package_id "
			"WHERE ps.require_id = (SELECT id FROM requires WHERE require=?1);";

	xasprintf(&sql, basesql, repo->name);

	stmt = prepare_sql(sqlite, sql);
	free(sql);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, provide, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

/*
 * Streaming filesite parser for rwhich.
 *
 * The compressed 'files' archive contains a text file with two sections
 * separated by a blank line:
 *  1) Directory dictionary: front-compressed lines "N suffix"
 *     (N = common prefix bytes with previous directory)
 *  2) Package data: "<name> <version>" header followed by
 *     ">N" directory index changes and filename lines.
 *
 * We stream through the decompressed file, keeping only the directory
 * dictionary in memory (thousands of entries, not millions of files).
 * For each file entry we reconstruct the full path and match it
 * against the requested path or glob pattern, collecting only the
 * matching package IDs.
 */

typedef vec_t(int64_t) idvec_t;
typedef vec_t(char *) dirv_t;

/* Helper to build full path from directory dictionary and filename */
static char *
build_fullpath(const char *dir, const char *file)
{
	char *fullpath;
	if (dir[0] == '\0')
		fullpath = xstrdup(file);
	else
		xasprintf(&fullpath, "%s/%s", dir, file);
	return (fullpath);
}

typedef vec_t(int) intv_t;

/*
 * Optimized glob matcher for rwhich -g.
 *
 * Splits the glob pattern at the last '/' into:
 *   - dir_pattern: everything before the last '/'
 *   - fn_pattern:  the last component (filename pattern)
 *
 * Without FNM_PATHNAME, '*' in fnmatch traverses '/' so we cannot
 * safely split arbitrary patterns by components.  However, we can
 * optimize two common cases:
 *
 *   1. Pattern has no '/' -> apply fnmatch only on the filename,
 *      skip directory filtering entirely (all dirs match).
 *
 *   2. Pattern has '/' and the directory part contains no glob
 *      metacharacters -> exact string compare on the directory,
 *      fnmatch only on the filename.
 *
 *   3. Otherwise -> fallback to fnmatch on the full path (same as
 *      before, but only for complex patterns with wildcards in dir).
 *
 * This avoids millions of fnmatch+alloc calls for the common cases
 * where the user searches a filename pattern or a fixed directory.
 */
struct glob_matcher {
	char    *pattern;      /* original pattern (for fallback) */
	char    *fn_pattern;   /* filename part of the pattern */
	char    *dir_pattern;  /* directory part (may be NULL) */
	bool    fn_is_glob;   /* fn_pattern has glob metacharacters */
	bool    dir_is_exact; /* dir_pattern has no glob metacharacters */
	int     exact_dir_idx;/* directory index if dir_is_exact, -1 otherwise */
};

static bool
glob_has_metachars(const char *p)
{
	const char *s;

	for (s = p; *s; s++) {
		if (*s == '*' || *s == '?' || *s == '[' || *s == ']')
			return (true);
	}
	return (false);
}

static struct glob_matcher *
glob_matcher_new(const char *pattern)
{
	struct glob_matcher *gm;
	const char *last_slash;

	gm = xcalloc(1, sizeof(*gm));
	gm->pattern = xstrdup(pattern);
	gm->exact_dir_idx = -1;

	last_slash = strrchr(pattern, '/');
	if (last_slash == NULL) {
		/* No '/' → filename-only pattern, all directories match */
		gm->fn_pattern = xstrdup(pattern);
		gm->dir_pattern = NULL;
		gm->dir_is_exact = false;
	} else {
		/* Split at last '/' */
		size_t dir_len = (size_t)(last_slash - pattern);

		gm->dir_pattern = xstrndup(pattern, dir_len);
		gm->fn_pattern = xstrdup(last_slash + 1);
		gm->dir_is_exact = !glob_has_metachars(gm->dir_pattern);
	}

	gm->fn_is_glob = glob_has_metachars(gm->fn_pattern);

	return (gm);
}

static void
glob_matcher_free(struct glob_matcher *gm)
{
	if (gm == NULL)
		return;
	free(gm->pattern);
	free(gm->fn_pattern);
	free(gm->dir_pattern);
	free(gm);
}

/*
 * After the directory dictionary is fully parsed, find the matching
 * directory index if we have an exact directory pattern.
 */
static void
glob_matcher_resolve_dirs(struct glob_matcher *gm, const dirv_t *dirs)
{
	int i;

	if (!gm->dir_is_exact || gm->dir_pattern == NULL)
		return;

	for (i = 0; i < (int)dirs->len; i++) {
		if (STREQ(dirs->d[i], gm->dir_pattern)) {
			gm->exact_dir_idx = i;
			return;
		}
	}
}

/*
 * Check if a file in directory cur_dir with name 'filename' matches
 * the glob pattern. Returns true if it matches.
 */
static bool
glob_matcher_matches(const struct glob_matcher *gm, int cur_dir,
    const char *filename, const char *dirpath)
{
	bool fn_match;

	/* Case 1: no directory constraint — all dirs match */
	if (gm->dir_pattern == NULL)
		goto check_fn;

	/* Case 2: exact directory match — O(1) index check */
	if (gm->dir_is_exact) {
		if (cur_dir != gm->exact_dir_idx)
			return (false);
		goto check_fn;
	}

	/* Case 3: directory has wildcards — fallback to full fnmatch */
	{
		char *fullpath;
		bool res;

		if (dirpath[0] == '\0')
			fullpath = xstrdup(filename);
		else
			xasprintf(&fullpath, "%s/%s", dirpath, filename);
		res = (fnmatch(gm->pattern, fullpath, 0) == 0);
		free(fullpath);
		return (res);
	}

check_fn:
	if (gm->fn_is_glob)
		fn_match = (fnmatch(gm->fn_pattern, filename, 0) == 0);
	else
		fn_match = STREQ(filename, gm->fn_pattern);

	return (fn_match);
}

static void
idvec_free(idvec_t *v)
{
	if (v != NULL) {
		free(v->d);
		free(v);
	}
}

static idvec_t *
idvec_new(void)
{
	idvec_t *v = xcalloc(1, sizeof(*v));
	return (v);
}

static int
pkg_repo_binary_file_which_parse(FILE *fp, struct pkg_repo *repo,
    const char *path, sqlite3_stmt **out_stmt)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	dirv_t dirs = vec_init();
	idvec_t *matching_ids = idvec_new();
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int phase = 0;			/* 0=dirs, 1=packages */
	int64_t cur_pkgid = -1;
	char cur_pkgname[256] = { 0 };
	int cur_dir = -1;
	sqlite3_stmt *id_stmt = NULL;
	const char *id_sql = "SELECT id FROM packages WHERE name = ?1 "
	    "AND version = ?2 LIMIT 1;";
	int ret = EPKG_FATAL;

	/*
	 * For exact (non-glob) lookup, split the requested path into
	 * directory and filename components so we can match efficiently
	 * without reconstructing every full path.
	 */
	char *match_dir = NULL;
	const char *match_file = NULL;
	int match_dir_idx = -1;

	const char *last_slash = strrchr(path, '/');
	if (last_slash == NULL) {
		/* No slash: file is at root, dir is empty */
		match_dir = xstrdup("");
		match_file = path;
	} else {
		match_file = last_slash + 1;
		size_t dir_len = last_slash - path;
		match_dir = xstrndup(path, dir_len);
	}

	id_stmt = prepare_sql(sqlite, id_sql);
	if (id_stmt == NULL)
		goto cleanup;

	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		/* Strip trailing newline */
		if (linelen > 0 && line[linelen - 1] == '\n')
			line[--linelen] = '\0';

		/* Empty line = section separator or package delimiter */
		if (linelen == 0) {
			if (phase == 1)
				cur_pkgid = -1;
			phase = 1;
			/*
			 * For exact match, find the directory index now that
			 * the dictionary is fully parsed.
			 */
			if (match_dir_idx == -1) {
				for (size_t i = 0; i < dirs.len; i++) {
					if (STREQ(dirs.d[i], match_dir)) {
						match_dir_idx = (int)i;
						break;
					}
				}
			}
			continue;
		}

		if (phase == 0) {
			/* Directory dictionary: "N suffix" */
			long prefix_len;
			char *space;

			space = strchr(line, ' ');
			if (space == NULL) {
				/* First line: no prefix, whole line is the dir */
				vec_push(&dirs, xstrdup(line));
			} else {
				prefix_len = strtol(line, &space, 10);
				while (*space == ' ')
					space++;
				const char *prev = dirs.len > 0 ?
				    dirs.d[dirs.len - 1] : "";
				char *dir = xmalloc(strlen(prev) + strlen(space) + 1);
				memcpy(dir, prev, prefix_len);
				strcpy(dir + prefix_len, space);
				vec_push(&dirs, dir);
			}
		} else {
			/* Package data */
			if (line[0] == '>') {
				/* Directory index change: ">N" */
				cur_dir = strtol(line + 1, NULL, 10);
			} else if (strchr(line, ' ') != NULL && cur_pkgid == -1) {
				/* Package header: "<name> <version>" */
				char *sp = strchr(line, ' ');
				strncpy(cur_pkgname, line, sp - line);
				cur_pkgname[sp - line] = '\0';
				const char *ver = sp + 1;

				sqlite3_reset(id_stmt);
				sqlite3_bind_text(id_stmt, 1, cur_pkgname, -1,
				    SQLITE_TRANSIENT);
				sqlite3_bind_text(id_stmt, 2, ver, -1,
				    SQLITE_TRANSIENT);
				if (sqlite3_step(id_stmt) == SQLITE_ROW)
					cur_pkgid = sqlite3_column_int64(id_stmt, 0);
				else
					cur_pkgid = -1;
			} else if (cur_pkgid >= 0 && cur_dir >= 0) {
				if (cur_dir == match_dir_idx &&
					    STREQ(line, match_file))
					vec_push(matching_ids, cur_pkgid);
			}
		}
	}

	sqlite3_finalize(id_stmt);

	/* Clean up directory dictionary */
	vec_free_and_free(&dirs, free);

	if (matching_ids->len == 0) {
		ret = EPKG_OK;
		goto cleanup;
	}

	/* Build SQL to fetch package details for matching IDs */
	xstring *sqlstr = xstring_new();
	xstring_printf(sqlstr,
	    "SELECT p.id, p.origin, p.name, p.version, p.comment, "
	    "p.name as uniqueid, "
	    "p.prefix, p.desc, p.arch, p.maintainer, p.www, "
	    "p.licenselogic, p.flatsize, p.pkgsize, "
	    "p.cksum, p.manifestdigest, p.path AS repopath, '%s' AS dbname "
	    "FROM packages AS p WHERE p.id IN (", repo->name);

	for (size_t i = 0; i < matching_ids->len; i++) {
		if (i > 0)
			xstring_printf(sqlstr, ",");
		xstring_printf(sqlstr, "%" PRId64, matching_ids->d[i]);
	}
	xstring_printf(sqlstr, ") ORDER BY p.name;");

	*out_stmt = prepare_sql(sqlite, xstring_get(sqlstr));

	if (*out_stmt == NULL)
		ret = EPKG_FATAL;
	else
		ret = EPKG_OK;

cleanup:
	idvec_free(matching_ids);
	free(line);
	free(match_dir);
	return (ret);
}

/*
 * Parse the filesite for glob mode, collecting (pkgid, realpath) pairs.
 * Uses optimized matching: splits the pattern into directory + filename
 * parts, pre-filters directories when possible, and avoids fnmatch on
 * full paths for the common cases.
 * Returns a fwpairv_t with the matching pairs.
 */
static int
pkg_repo_binary_file_which_parse_glob(FILE *fp, struct pkg_repo *repo,
    const char *pattern, fwpairv_t *out_pairs)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	dirv_t dirs = vec_init();
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int phase = 0;
	int64_t cur_pkgid = -1;
	char cur_pkgname[256] = { 0 };
	int cur_dir = -1;
	sqlite3_stmt *id_stmt = NULL;
	const char *id_sql = "SELECT id FROM packages WHERE name = ?1 "
	    "AND version = ?2 LIMIT 1;";
	struct glob_matcher *gm = NULL;
	int ret = EPKG_FATAL;

	gm = glob_matcher_new(pattern);

	id_stmt = prepare_sql(sqlite, id_sql);
	if (id_stmt == NULL)
		goto cleanup;

	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		if (linelen > 0 && line[linelen - 1] == '\n')
			line[--linelen] = '\0';

		if (linelen == 0) {
			if (phase == 1)
				cur_pkgid = -1;
			phase = 1;
			/*
			 * Directory dictionary is fully parsed — resolve
			 * exact directory index if applicable.
			 */
			glob_matcher_resolve_dirs(gm, &dirs);
			continue;
		}

		if (phase == 0) {
			long prefix_len;
			char *space;

			space = strchr(line, ' ');
			if (space == NULL)
				vec_push(&dirs, xstrdup(line));
			else {
				prefix_len = strtol(line, &space, 10);
				while (*space == ' ')
					space++;
				const char *prev = dirs.len > 0 ?
				    dirs.d[dirs.len - 1] : "";
				char *dir = xmalloc(strlen(prev) + strlen(space) + 1);
				memcpy(dir, prev, prefix_len);
				strcpy(dir + prefix_len, space);
				vec_push(&dirs, dir);
			}
		} else {
			if (line[0] == '>') {
				cur_dir = strtol(line + 1, NULL, 10);
			} else if (strchr(line, ' ') != NULL && cur_pkgid == -1) {
				char *sp = strchr(line, ' ');
				strncpy(cur_pkgname, line, sp - line);
				cur_pkgname[sp - line] = '\0';
				const char *ver = sp + 1;

				sqlite3_reset(id_stmt);
				sqlite3_bind_text(id_stmt, 1, cur_pkgname, -1,
				    SQLITE_TRANSIENT);
				sqlite3_bind_text(id_stmt, 2, ver, -1,
				    SQLITE_TRANSIENT);
				if (sqlite3_step(id_stmt) == SQLITE_ROW)
					cur_pkgid = sqlite3_column_int64(id_stmt, 0);
				else
					cur_pkgid = -1;
			} else if (cur_pkgid >= 0 && cur_dir >= 0) {
				if (cur_dir >= 0 && cur_dir < (int)dirs.len) {
					if (glob_matcher_matches(gm, cur_dir,
					    line, dirs.d[cur_dir])) {
						struct file_which_pair pair;
						pair.pkgid = cur_pkgid;
						pair.path = build_fullpath(
						    dirs.d[cur_dir], line);
						vec_push(out_pairs, pair);
					}
				}
			}
		}
	}

	sqlite3_finalize(id_stmt);
	vec_free_and_free(&dirs, free);

	ret = EPKG_OK;

cleanup:
	glob_matcher_free(gm);
	free(line);
	return (ret);
}

/*
 * Read "files" (compressed, raw format) and stream decompress
 * into a FILE* via fopencookie, then parse without writing to disk.
 */
struct archive_read_data {
	struct archive *a;
	char  *buf;     /* owned buffer for archive_read_data */
	size_t len;
	off_t off;
};

static ssize_t
archive_read_read_fn(void *p, char *buf, size_t n)
{
	struct archive_read_data *ad = p;
	ssize_t total = 0;

	while ((size_t)total < n) {
		if ((off_t)ad->off >= (off_t)ad->len) {
			if (ad->buf == NULL)
				ad->buf = xmalloc(65536);
			la_ssize_t rd = archive_read_data(ad->a,
			    ad->buf, 65536);
			if (rd <= 0)
				break;
			ad->len = (size_t)rd;
			ad->off = 0;
		}
		size_t avail = ad->len - (size_t)ad->off;
		size_t tocopy = avail < (n - (size_t)total) ? avail :
		    n - (size_t)total;
		memcpy(buf + total, (const char *)ad->buf + ad->off, tocopy);
		ad->off += tocopy;
		total += (ssize_t)tocopy;
	}
	return total;
}

static int
pkg_repo_binary_file_which_read(struct pkg_repo *repo, const char *path,
    bool glob, sqlite3_stmt **out_stmt, fwpairv_t *out_pairs)
{
	struct archive *a = archive_read_new();
	struct archive_entry *ae;
	FILE *unfp = NULL;
	int fd = -1;
	int rc = EPKG_FATAL;

	fd = openat(repo->dfd, "files", O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return (EPKG_OK); /* no filesite at all */

	archive_read_support_filter_all(a);
	archive_read_support_format_raw(a);

	if (archive_read_open_fd(a, fd, 4096) != ARCHIVE_OK) {
		archive_read_free(a);
		close(fd);
		return (EPKG_FATAL);
	}
	archive_read_next_header(a, &ae);

	/* The entire stream is the zstd-compressed filesite data (raw format, no header) */
	struct archive_read_data ad = { a, NULL, 0, 0 };
	cookie_io_functions_t fi = { archive_read_read_fn, NULL, NULL, NULL };
	unfp = fopencookie(&ad, "r", fi);
	if (unfp != NULL) {
		if (glob)
			rc = pkg_repo_binary_file_which_parse_glob(unfp, repo, path, out_pairs);
		else
			rc = pkg_repo_binary_file_which_parse(unfp, repo, path, out_stmt);
		fclose(unfp);
	}

	archive_read_close(a);
	archive_read_free(a);
	close(fd);
	return (rc);
}

static struct pkg_repo_it *
pkg_repo_binary_file_which_glob_new(struct pkg_repo *repo)
{
	struct pkg_repo_it *it;
	struct pkg_repo_file_which_glob *fglob;

	it = xcalloc(1, sizeof(*it));
	fglob = xcalloc(1, sizeof(*fglob));

	it->ops = &pkg_repo_binary_file_which_glob_it_ops;
	it->flags = PKGDB_IT_FLAG_ONCE;
	it->repo = repo;
	it->data = fglob;

	fglob->sqlite = PRIV_GET(repo);
	fglob->last_pkgid = -1;

	const char *sql =
		"SELECT p.id, p.origin, p.name, p.version, p.comment, "
		"p.name as uniqueid, "
		"p.prefix, p.desc, p.arch, p.maintainer, p.www, "
		"p.licenselogic, p.flatsize, p.pkgsize, "
		"p.cksum, p.manifestdigest, p.path AS repopath, ? AS dbname "
		"FROM packages AS p WHERE p.id = ?;";

	fglob->pkg_stmt = prepare_sql(fglob->sqlite, sql);
	if (fglob->pkg_stmt == NULL)
		return (NULL);

	sqlite3_bind_text(fglob->pkg_stmt, 1, repo->name, -1, SQLITE_STATIC);

	return (it);
}

struct pkg_repo_it *
pkg_repo_binary_file_which(struct pkg_repo *repo, const char *path, bool glob)
{
	int rc;

	if (repo->dfd == -1 && pkg_repo_open(repo) == EPKG_FATAL)
		return (NULL);

	if (glob) {
		pkg_debug(1, "rwhich glob");
		fwpairv_t pairs = vec_init();
		struct pkg_repo_it *it;

		rc = pkg_repo_binary_file_which_read(repo, path, glob, NULL, &pairs);

		if (rc != EPKG_OK || pairs.len == 0) {
			for (size_t i = 0; i < pairs.len; i++)
				free(pairs.d[i].path);
			vec_free(&pairs);
			return (NULL);
		}

		it = pkg_repo_binary_file_which_glob_new(repo);
		((struct pkg_repo_file_which_glob *)it->data)->pairs = pairs;
		return (it);
	} else {
		pkg_debug(1, "rwhich no glob");
		sqlite3_stmt *stmt = NULL;

		rc = pkg_repo_binary_file_which_read(repo, path, glob, &stmt, NULL);

		if (rc != EPKG_OK || stmt == NULL)
			return (NULL);

		return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
	}
}

static const char *
pkg_repo_binary_search_how(match_t match)
{
	const char	*how = NULL;

	switch (match) {
	case MATCH_ALL:
		how = "TRUE";
		break;
	case MATCH_INTERNAL:
		how = "%s = ?1";
		break;
	case MATCH_EXACT:
		if (pkgdb_case_sensitive())
			how = "%s = ?1";
		else
			how = "%s = ?1 COLLATE NOCASE";
		break;
	case MATCH_GLOB:
		if (pkgdb_case_sensitive())
			how = "%s GLOB ?1";
		else
			how = "lower(%s) GLOB lower(?1)";
		break;
	case MATCH_REGEX:
		how = "%s REGEXP ?1";
		break;
	}

	return (how);
}

static int
pkg_repo_binary_build_search_query(xstring *sql, match_t match,
    pkgdb_field field, pkgdb_field sort)
{
	const char	*how;
	const char	*what = NULL;
	const char	*orderby = NULL;

	how = pkg_repo_binary_search_how(match);

	switch (field) {
	case FIELD_NONE:
		what = NULL;
		break;
	case FIELD_ORIGIN:
		what = "categories.name || substr(origin, instr(origin, '/'))";
		break;
	case FIELD_FLAVOR:
		what = "categories.name || substr(origin, instr(origin, '/')) || '@' || flavor";
		break;
	case FIELD_NAME:
		what = "p.name";
		break;
	case FIELD_NAMEVER:
		what = "p.name || '-' || version";
		break;
	case FIELD_COMMENT:
		what = "comment";
		break;
	case FIELD_DESC:
		what = "desc";
		break;
	case FIELD_COMMENT_DESC:
		break;
	}

	if (field == FIELD_COMMENT_DESC && how != NULL) {
		xstring_printf(sql, "(");
		xstring_printf(sql, how, "comment");
		xstring_printf(sql, " OR ");
		xstring_printf(sql, how, "desc");
		xstring_printf(sql, ")");
	} else if (what != NULL && how != NULL)
		xstring_printf(sql, how, what);

	switch (sort) {
	case FIELD_NONE:
		orderby = NULL;
		break;
	case FIELD_ORIGIN:
		orderby = " ORDER BY origin";
		break;
	case FIELD_FLAVOR:
		orderby = " ORDER BY p.name";
	case FIELD_NAME:
		orderby = " ORDER BY p.name";
		break;
	case FIELD_NAMEVER:
		orderby = " ORDER BY p.name, version";
		break;
	case FIELD_COMMENT:
		orderby = " ORDER BY comment";
		break;
	case FIELD_DESC:
		orderby = " ORDER BY desc";
		break;
	case FIELD_COMMENT_DESC:
		orderby = " ORDER BY comment";
		break;
	}

	if (orderby != NULL)
		xstring_printf(sql, "%s", orderby);

	return (EPKG_OK);
}

struct pkg_repo_it *
pkg_repo_binary_search(struct pkg_repo *repo, const char *pattern, match_t match,
    pkgdb_field field, pkgdb_field sort)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	sqlite3_stmt	*stmt = NULL;
	xstring	*sql = NULL;
	char *sqlcmd = NULL;
	const char	*multireposql = ""
		"SELECT DISTINCT p.id, origin, p.name, version, comment, "
		"prefix, desc, arch, maintainer, www, "
		"licenselogic, flatsize, pkgsize, "
		"cksum, path AS repopath, '%1$s' AS dbname, '%2$s' AS repourl "
		"FROM packages  as p "
		"LEFT JOIN pkg_categories ON p.id = pkg_categories.package_id "
		"LEFT JOIN categories ON categories.id = pkg_categories.category_id "
		"LEFT JOIN flavors ON flavors.package_id = p.id ";

	if (match != MATCH_ALL && (pattern == NULL || pattern[0] == '\0'))
		return (NULL);

	sql = xstring_new();
	xstring_printf(sql, multireposql, repo->name, repo->url);

	/* close the UNIONs and build the search query */
	xstring_printf(sql, "%s", "WHERE ");

	pkg_repo_binary_build_search_query(sql, match, field, sort);
	xstring_printf(sql, "%s", ";");
	sqlcmd = xstring_get(sql);

	stmt = prepare_sql(sqlite, sqlcmd);
	free(sqlcmd);
	if (stmt == NULL)
		return (NULL);

	sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
	pkgdb_debug(4, stmt);

	return (pkg_repo_binary_it_new(repo, stmt, PKGDB_IT_FLAG_ONCE));
}

struct pkg_repo_it *
pkg_repo_binary_groupsearch(struct pkg_repo *repo, const char *pattern, match_t match,
    pkgdb_field field)
{
	ucl_object_t *groups, *ar, *el;
	const ucl_object_t *o;
	const char *cmp;
	int fd;
	regex_t *re = NULL;
	int flag = 0;
	bool in_comment = false;
	bool start_with = false;

	switch (field) {
		case FIELD_NAME:
		case FIELD_NAMEVER:
			break;
		case FIELD_COMMENT:
			in_comment = true;
			break;
		default:
			/* we cannot search in other fields */
			return (NULL);
	}

	if (repo->dfd == -1 && pkg_repo_open(repo) == EPKG_FATAL)
		return (NULL);
	fd = openat(repo->dfd, "groups.ucl", O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return (NULL);
	groups = ucl_parse_fd(fd, repo->name);
	close(fd);
	if (groups == NULL)
		return (NULL);

	if (ucl_object_type(groups) != UCL_ARRAY) {
		ucl_object_unref(groups);
		return (NULL);
	}
	if (*pattern == '@') {
		pattern++;
		start_with = true;
	}

	ar = NULL;
	while (ucl_array_size(groups) > 0) {
		el = ucl_array_pop_first(groups);
		if (in_comment) {
			o = ucl_object_find_key(el, "comment");
		} else {
			o = ucl_object_find_key(el, "name");
		}
		if (o == NULL) {
			ucl_object_unref(el);
			continue;
		}
		cmp = ucl_object_tostring(o);
		switch (match) {
		case MATCH_ALL:
			break;
		case MATCH_INTERNAL:
			if (!STREQ(cmp, pattern))
				continue;
			break;
		case MATCH_EXACT:
			if (pkgdb_case_sensitive()) {
				if (!STREQ(cmp, pattern))
					continue;
			} else {
				if (!STRIEQ(cmp, pattern))
					continue;
			}
			break;
		case MATCH_GLOB:
			if (pkgdb_case_sensitive() != 0)
				flag = FNM_CASEFOLD;
			if (fnmatch(cmp, pattern, flag) == FNM_NOMATCH)
				continue;
			break;
		case MATCH_REGEX:
			if (re == NULL) {
				char *newpattern = NULL;
				const char *pat = pattern;
				flag = REG_EXTENDED | REG_NOSUB;
				if (pkgdb_case_sensitive() != 0)
					flag |= REG_ICASE;
				re = xmalloc(sizeof(regex_t));
				if (start_with) {
					xasprintf(&newpattern, "^%s", pattern);
					pat = newpattern;
				}
				if (regcomp(re, pat, flag) != 0) {
					pkg_emit_error("Invalid regex: 'pattern'");
					ucl_object_unref(groups);
					if (ar != NULL)
						ucl_object_unref(ar);
					free(newpattern);
					return (NULL);
				}
				free(newpattern);
			}
			if (regexec(re, cmp, 0, NULL, 0) == REG_NOMATCH)
				continue;
			break;
		}
		if (ar == NULL)
			ar = ucl_object_typed_new(UCL_ARRAY);
		ucl_array_append(ar, el);
	}

	if (re != NULL)
		regfree(re);
	ucl_object_unref(groups);

	if (ar == NULL)
		return (NULL);

	return (pkg_repo_binary_group_it_new(repo, ar));
}

int
pkg_repo_binary_ensure_loaded(struct pkg_repo *repo,
	struct pkg *pkg, unsigned flags)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	struct pkg *cached = NULL;
	char path[MAXPATHLEN];
	int rc;

	if (pkg->type == PKG_GROUP_REMOTE)
		return (EPKG_OK);
	flags &= PKG_LOAD_FILES|PKG_LOAD_DIRS;
	/*
	 * If info is already present, done.
	 */
	if ((pkg->flags & flags) == flags) {
		return EPKG_OK;
	}
	if (pkg->type == PKG_INSTALLED) {
		pkg_emit_error("cached package %s-%s: "
			       "attempting to load info from an installed package",
			       pkg->name, pkg->version);
		return EPKG_FATAL;

		/* XXX If package is installed, get info from SQLite ???  */
		rc = pkgdb_ensure_loaded_sqlite(sqlite, pkg, flags);
		if (rc != EPKG_OK) {
			return rc;
		}
		/* probably unnecessary */
		if ((pkg->flags & flags) != flags) {
			return EPKG_FATAL;
		}
		return rc;
	}
	/*
	 * Try to get that information from fetched package in cache
	 */

	if (pkg_repo_cached_name(pkg, path, sizeof(path)) != EPKG_OK)
		return (EPKG_FATAL);

	pkg_debug(1, "Binary> loading %s", path);
	if (pkg_open(&cached, path, PKG_OPEN_TRY) != EPKG_OK) {
		pkg_free(cached);
		return EPKG_FATAL;
	}

	/* Now move required elements to the provided package */
	pkg_list_free(pkg, PKG_FILES);
	pkg_list_free(pkg, PKG_CONFIG_FILES);
	pkg_list_free(pkg, PKG_DIRS);
	pkg->files = cached->files;
	memset(&cached->files, 0, sizeof(cached->files));
	pkg->config_files = cached->config_files;
	memset(&cached->config_files, 0, sizeof(cached->config_files));
	pkg->dirs = cached->dirs;
	memset(&cached->dirs, 0, sizeof(cached->dirs));

	pkg_free(cached);
	pkg->flags |= flags;

	return EPKG_OK;
}

int64_t
pkg_repo_binary_stat(struct pkg_repo *repo, pkg_stats_t type)
{
	sqlite3 *sqlite = PRIV_GET(repo);
	sqlite3_stmt	*stmt = NULL;
	int64_t		 stats = 0;
	const char *sql = NULL;

	switch(type) {
	case PKG_STATS_LOCAL_COUNT:
	case PKG_STATS_REMOTE_REPOS:
	case PKG_STATS_LOCAL_SIZE:
		return (stats);
	case PKG_STATS_REMOTE_UNIQUE:
		sql = "SELECT COUNT(id) FROM main.packages;";
		break;
	case PKG_STATS_REMOTE_COUNT:
		sql = "SELECT COUNT(id) FROM main.packages;";
		break;
	case PKG_STATS_REMOTE_SIZE:
		sql = "SELECT SUM(pkgsize) FROM main.packages;";
		break;
	}

	pkg_debug(4, "binary_repo: running '%s'", sql);
	stmt = prepare_sql(sqlite, sql);

	if (stmt == NULL)
		return (stats);

	while (sqlite3_step(stmt) != SQLITE_DONE) {
		stats = sqlite3_column_int64(stmt, 0);
	}

	sqlite3_finalize(stmt);

	return (stats);
}
