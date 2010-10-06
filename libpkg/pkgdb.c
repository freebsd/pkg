#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <err.h>
#include <fnmatch.h>
#include <regex.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"

/* theses functions request on local database (cdb) */
static int pkg_db_open(struct pkgdb *);
static void pkg_reset(struct pkg *);
static const void *pkg_db_query(struct cdb *, const char *, ...);
static const char *pkg_db_getattr(struct pkg *, const char *);
static int pkg_db_dep(struct pkg *, struct pkg *);

const char *
pkg_namever(struct pkg *pkg)
{
	if (pkg->namever == NULL)
		pkg->namever = pkg_db_getattr(pkg, PKGDB_NAMEVER);
	return (pkg->namever);
}

const char *
pkg_name(struct pkg *pkg)
{
	if (pkg->name == NULL)
		pkg->name = pkg_db_getattr(pkg, PKGDB_NAME);
	return (pkg->name);
}

const char *
pkg_version(struct pkg *pkg)
{
	if (pkg->version == NULL)
		pkg->version = pkg_db_getattr(pkg, PKGDB_VERSION);
	return (pkg->version);
}

const char *
pkg_comment(struct pkg *pkg)
{
	if (pkg->comment == NULL)
		pkg->comment = pkg_db_getattr(pkg, PKGDB_COMMENT);
	return (pkg->comment);
}

const char *
pkg_desc(struct pkg *pkg)
{
	if (pkg->desc == NULL)
		pkg->desc = pkg_db_getattr(pkg, PKGDB_DESC);
	return (pkg->desc);
}

const char *
pkg_origin(struct pkg *pkg)
{
	if (pkg->origin == NULL)
		pkg->origin = pkg_db_getattr(pkg, PKGDB_ORIGIN);
	return (pkg->desc);
}

int
pkg_dep(struct pkg *pkg, struct pkg *dep)
{
	return (pkg_db_dep(pkg, dep));
}

static void
pkg_reset(struct pkg *pkg)
{
	pkg->namever = NULL;
	pkg->name = NULL;
	pkg->version = NULL;
	pkg->origin = NULL;
	pkg->comment = NULL;
	pkg->desc = NULL;
	pkg->idx = -1;
	pkg->idep = 0;
	pkg->irdep = 0;
	pkg->db = NULL;
}

static int
pkg_db_dep(struct pkg *pkg, struct pkg *dep)
{
	const size_t *idx;
	int ret = -1;

	pkg_reset(dep);

	if ((dep->namever = pkg_db_query(pkg->db, PKGDB_DEPS, pkg->idx, pkg->idep)) != NULL &&
			(idx = pkg_db_query(pkg->db, "%s", dep->namever)) != NULL) {
		dep->idx = *idx;
		dep->db = pkg->db;
		pkg->idep++;
		ret = 0;
	}
	return (ret);
}

/* query formated using string key */
static const void *
pkg_db_query(struct cdb *db, const char *fmt, ...)
{
	va_list args;
	char key[BUFSIZ];
	size_t len;
	const void *val;

	va_start(args, fmt);
	len = vsnprintf(key, sizeof(key), fmt, args);

	if (len != strlen(key)) {
		warnx("key too long:");
		vwarnx(fmt, args);
		va_end(args);
		return NULL;
	}

	va_end(args);

	if (cdb_find(db, key, len) <= 0)
		return NULL;

	db_get(val, db);
	return (val);
}

static const char *
pkg_db_getattr(struct pkg *pkg, const char *attr)
{
	return (pkg_db_query(pkg->db, attr, pkg->idx));
}

const char *
pkgdb_get_dir(void)
{
	const char *pkg_dbdir;

	if ((pkg_dbdir = getenv("PKG_DBDIR")) == NULL)
		pkg_dbdir = PKG_DBDIR;

	return pkg_dbdir;
}

static int
pkg_db_open(struct pkgdb *db)
{
	char path[MAXPATHLEN];
	int fd;

	snprintf(path, sizeof(path), "%s/pkgdb.cache", pkgdb_get_dir());

	if ((fd = open(path, O_RDONLY)) != -1)
		fd = cdb_init(&db->db, fd);
	else {
		/* TODO custom pkgdb error */
	}

	return (fd);
}

/*
 * Acquire a lock to access the database.
 * If `writer' is set to 1, an exclusive lock is requested so it wont mess up
 * with other writers or readers.
 */
void
pkgdb_lock(struct pkgdb *db, int writer)
{
	char fname[FILENAME_MAX];
	int flags;

	snprintf(fname, sizeof(fname), "%s/%s", pkgdb_get_dir(), PKGDB_LOCK);
	if ((db->lock_fd = open(fname, O_RDONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH)) < 0)
		err(EXIT_FAILURE, "open(%s)", fname);

	if (writer == 1)
		flags = LOCK_EX;
	else
		flags = LOCK_SH;

	if (flock(db->lock_fd, flags) < 0)
		errx(EXIT_FAILURE, "unable to acquire a lock to the database");
}

void
pkgdb_unlock(struct pkgdb *db)
{
	flock(db->lock_fd, LOCK_UN);
	close(db->lock_fd);
	db->lock_fd = -1;
}

int
pkgdb_init(struct pkgdb *db, const char *pattern, match_t match)
{
	pkgdb_cache_update(db);
	if (pkg_db_open(db) == -1)
		return (-1); /* TOTO pkgdb error */

	db->pattern = pattern;
	db->match = match;
	db->i = 0;

	if (match != MATCH_ALL && pattern == NULL)
		return (-1);

	/* Regex initialisation */
	if (match == MATCH_REGEX) {
		if (regcomp(&db->re, pattern, REG_BASIC | REG_NOSUB) != 0) {
			warnx("'%s' is not a valid regular expression", pattern);
			return (-1);
		}
	} else if (match == MATCH_EREGEX) {
		if (regcomp(&db->re, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
			warnx("'%s' is not a valid extended regular expression", pattern);
			return (-1);
		}
	}

	return (0);
}

void
pkgdb_free(struct pkgdb *db)
{
	close(cdb_fileno(&db->db));
	cdb_free(&db->db);

	if (db->match == MATCH_REGEX || db->match == MATCH_EREGEX)
		regfree(&db->re);

	return;
}

static int
pkgdb_match(struct pkgdb *db, const char *pattern)
{
	int matched = 1;
	switch (db->match) {
		case MATCH_ALL:
			matched = 0;
			break;
		case MATCH_EXACT:
			matched = strcmp(pattern, db->pattern);
			break;
		case MATCH_GLOB:
			matched = fnmatch(db->pattern, pattern, 0);
			break;
		case MATCH_REGEX:
		case MATCH_EREGEX:
			matched = regexec(&db->re, pattern, 0, NULL, 0);
			break;
	}
	return (matched);
}

int
pkgdb_query(struct pkgdb *db, struct pkg *pkg)
{
	pkg_reset(pkg);
	pkgdb_lock(db, 0);

	while ((pkg->namever = pkg_db_query(&db->db, PKGDB_NAMEVER, db->i)) != NULL) {
		if (pkg->namever != NULL && pkgdb_match(db, pkg->namever) == 0) {
			pkg->idx = db->i++;
			pkg->db = &db->db;
			return (0);
		}

		db->i++;
	}
	pkgdb_unlock(db);

	return (-1);
}

