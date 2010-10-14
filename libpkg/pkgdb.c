#include <sys/param.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <err.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"


#define PKGDB_LOCK "lock"
#define PKG_DBDIR "/var/db/pkg"

static const char *
pkg_getattr(struct pkg *pkg, const char **val, const char *attr)
{
	if (*val == NULL)
		*val = pkgdb_cache_getattr(pkg, attr);

	return (*val);
}

const char *
pkg_namever(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->namever, "namever"));
}

const char *
pkg_name(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->name, "name"));
}

const char *
pkg_version(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->version, "version"));
}

const char *
pkg_comment(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->comment, "comment"));
}

const char *
pkg_desc(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->comment, "comment"));
}

const char *
pkg_origin(struct pkg *pkg)
{
	return (pkg_getattr(pkg, &pkg->origin, "origin"));
}

int
pkg_dep(struct pkg *pkg, struct pkg *dep)
{
	/* call backend dep query */
	return (pkgdb_cache_dep(pkg, dep));
}

void
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
	pkg->pdb = NULL;
}

const char *
pkgdb_get_dir(void)
{
	const char *pkg_dbdir;

	if ((pkg_dbdir = getenv("PKG_DBDIR")) == NULL)
		pkg_dbdir = PKG_DBDIR;

	return pkg_dbdir;
}

/* Acquire/Release a lock to access the database. */
int
pkgdb_lock(struct pkgdb *db, int flags)
{
	char fname[MAXPATHLEN];

	if (db->lock_fd == -1) {
		snprintf(fname, sizeof(fname), "%s/%s", pkgdb_get_dir(), PKGDB_LOCK);

		if ((db->lock_fd = open(fname, O_RDONLY | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH)) < 0) {
			pkgdb_set_error(db, errno, "open(%s)", fname);
			return (-1);
		}
	}

	if (flock(db->lock_fd, flags) < 0) {
		pkgdb_set_error(db, errno, "unable to acquire lock on %s", fname);
		return (-1);
	}

	return (0);
}

int
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
pkgdb_init(struct pkgdb *db, const char *pattern, match_t match)
{
	db->pattern = pattern;
	db->match = match;
	db->i = 0;
	db->lock_fd = -1;
	db->errnum = -1;

	if (match != MATCH_ALL && pattern == NULL) {
		pkgdb_set_error(db, 0, "missing pattern");
		return (-1);
	}

	/* Regex initialisation */
	if (match == MATCH_REGEX) {
		if (regcomp(&db->re, pattern, REG_BASIC | REG_NOSUB) != 0) {
			pkgdb_set_error(db, 0, "'%s' is not a valid regular expression", pattern);
			return (-1);
		}
	} else if (match == MATCH_EREGEX) {
		if (regcomp(&db->re, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
			pkgdb_set_error(db, 0, "'%s' is not a valid extended regular expression", pattern);
			return (-1);
		}
	}

	/* call backend init */
	return (pkgdb_cache_init(db));
}

void
pkgdb_free(struct pkgdb *db)
{
	if (db->match == MATCH_REGEX || db->match == MATCH_EREGEX)
		regfree(&db->re);

	if (db->lock_fd != -1)
		close(db->lock_fd);

	/* call backend free */
	pkgdb_cache_free(db);

	return;
}

int
pkgdb_query(struct pkgdb *db, struct pkg *pkg)
{
	pkg_reset(pkg);
	/* call backend query */
	return (pkgdb_cache_query(db, pkg));
}

void
pkgdb_set_error(struct pkgdb *db, int errnum, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(db->errstring, sizeof(db->errstring), fmt, args);
	va_end(args);

	db->errnum = errnum;
	return;
}

void
pkgdb_warn(struct pkgdb *db)
{
	warnx("%s %s", db->errstring, (db->errnum > 0) ? strerror(db->errnum) : "");
	return;
}

int
pkgdb_errnum(struct pkgdb *db)
{
	return (db->errnum);
}
