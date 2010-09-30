#ifndef _PKGDB_H
#define _PKGDB_H
#include <pkg.h>

#define PKG_DBDIR "/var/db/pkg"

typedef enum _match_t {
	MATCH_ALL,
	MATCH_EXACT,
	MATCH_GLOB,
	MATCH_REGEX,
	MATCH_EREGEX
} match_t;

void pkgdb_lock(struct pkgdb *db, int write);
void pkgdb_unlock(struct pkgdb *db);

const char * pkgdb_get_dir(void);

void pkgdb_init(struct pkgdb *db, const char *pattern, match_t match, unsigned char flags);
void pkgdb_free(struct pkgdb *db);
size_t pkgdb_count(struct pkgdb *db);

#endif
