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

	if (db->match == MATCH_GLOB)
		matched = fnmatch(db->pattern, pattern, 0);
	else if (db->match == MATCH_REGEX || db->match == MATCH_EREGEX)
		matched = regexec(&db->re, pattern, 0, NULL, 0);

	return (matched);
}

int
pkgdb_open(struct pkgdb *db)
{
	return (pkgdb_open2(db, true));
}

int
pkgdb_open2(struct pkgdb *db, bool rebuild)
{
	db->cdb = NULL;
	db->lock_fd = -1;
	db->errnum = -1;
	db->errstring[0] = '\0';

	return (pkgdb_cache_open(db, rebuild));
}

void
pkgdb_close(struct pkgdb *db)
{
	if (db->lock_fd != -1) {
		pkgdb_lock(db, LOCK_UN);
		close(db->lock_fd);
	}

	/* call backend close */
	pkgdb_cache_close(db);
}

int
pkgdb_query_init(struct pkgdb *db, const char *pattern, match_t match)
{
	db->pattern = pattern;
	db->match = match;
	db->i = 0;

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

	return (0);
}

void
pkgdb_query_free(struct pkgdb *db)
{
	if (db->match == MATCH_REGEX || db->match == MATCH_EREGEX)
		regfree(&db->re);
	db->match = -1;
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
}

void
pkgdb_warn(struct pkgdb *db)
{
	warnx("%s %s", db->errstring, (db->errnum > 0) ? strerror(db->errnum) : "");
}

int
pkgdb_errnum(struct pkgdb *db)
{
	return (db->errnum);
}
