#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H
#include "pkgdb.h"

#define PKGDB_VERSION "%zuv"
#define PKGDB_COMMENT "%zuc"
#define PKGDB_DESC    "%zud"
#define PKGDB_ORIGIN  "%zuo"
#define PKGDB_DEPS    "%zuD"
#define PKGDB_COUNT   "count"

/* quick db_get */
#define db_get(val, db) do { \
	(val) = cdb_get((db), cdb_datalen((db)), cdb_datapos((db))); \
	} while (0)


void pkgdb_cache_update(struct pkgdb *db);
void pkgdb_cache_init(struct pkgdb *db, const char *pattern, match_t match, unsigned char flags);

#endif
