#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"

void
pkgdb_init(struct pkgdb *db, const char *pattern, match_t match, unsigned char flags) {
	/* first check if the cache has to be rebuild */
	pkgdb_cache_update();
	pkgdb_cache_init(db, pattern, match, flags);
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
	struct pkg *pkg;

	fd = cdb_fileno(&db->db);
	cdb_free(&db->db);
	close(fd);

	PKGDB_FOREACH(pkg, db)
		pkg_free(db, pkg);

	free(db->pkgs);
}

size_t
pkgdb_count(struct pkgdb *db)
{
	return (db->count);
}

