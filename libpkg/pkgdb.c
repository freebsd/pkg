#include <sys/file.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"

#define LOCK_FILE "lock"

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

	snprintf(fname, sizeof(fname), "%s/%s", pkgdb_get_dir(), LOCK_FILE);
	if ((db->lock_fd = open(fname, O_RDONLY | O_CREAT)) < 0)
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

void
pkgdb_init(struct pkgdb *db, const char *pattern, match_t match, unsigned char flags) {
	/* first check if the cache has to be rebuild */
	pkgdb_cache_update(db);

	pkgdb_lock(db, 0);
	pkgdb_cache_init(db, pattern, match, flags);
	pkgdb_unlock(db);
}

static void
pkg_free(struct pkgdb *db, struct pkg *pkg)
{
	struct pkg **deps;
	if (db->flags & PKGDB_INIT_DEPS) {
		if (!(pkg->errors & PKGERR_NOT_INSTALLED)) {
			for (deps = pkg->deps; *deps != NULL; deps++) {
				pkg_free(db, *deps);
			}
		}
	}
	free(pkg);
}

void
pkgdb_free(struct pkgdb *db)
{
	int fd;
	struct pkg *pkg, **deps;

	fd = cdb_fileno(&db->db);
	cdb_free(&db->db);
	close(fd);

	PKGDB_FOREACH(pkg, db) {
		if (db->flags & PKGDB_INIT_DEPS) {
			for (deps = pkg->deps; *deps != NULL; deps++)
				free(*deps);
			free(pkg->deps);
		}
		if (db->flags & PKGDB_INIT_RDEPS) {
			for (deps = pkg->rdeps; *deps != NULL; deps++)
				free(*deps);
			free(pkg->rdeps);
		}
		free(pkg);
	}

	free(db->pkgs);
}

size_t
pkgdb_count(struct pkgdb *db)
{
	return (db->count);
}

const char *
pkgdb_get_dir()
{
	const char *pkg_dbdir;

	if ((pkg_dbdir = getenv("PKG_DBDIR")) == NULL)
		pkg_dbdir = PKG_DBDIR;

	return pkg_dbdir;
}

