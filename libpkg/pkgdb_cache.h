#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H
#include "pkgdb.h"

#define PKGDB_VERSION "%dv"
#define PKGDB_COMMENT "%dc"
#define PKGDB_DESC    "%dd"
#define PKGDB_ORIGIN  "%do"
#define PKGDB_DEPS    "%dD"
#define PKGDB_COUNT   "count"

/* quick db_get */
#define db_get(val, db) do { \
	(val) = cdb_get((db), cdb_datalen((db)), cdb_datapos((db))); \
	} while (0)


void pkgdb_cache_update(void);
void pkgdb_cache_init(struct pkgdb *db, const char *pattern);

#endif
