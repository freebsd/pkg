#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pkgdb.h"
#include "pkgdb_cache.h"

void
pkgdb_init(struct pkgdb *db, const char *pattern) {
	/* first check if the cache has to be rebuild */
	pkgdb_cache_update();
	return (pkgdb_cache_init(db, pattern));
}

void
pkgdb_free(struct pkgdb *db)
{
	int fd;
	struct pkg *pkg;

	fd = cdb_fileno(&db->db);
	cdb_free(&db->db);
	close(fd);

	PKGDB_FOREACH(pkg, db) {
		free(pkg->name_version);
		free(pkg);
	}
	free(db->pkgs);
}

size_t
pkgdb_count(struct pkgdb *db)
{
	return (db->count);
}

