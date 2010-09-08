#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H
#include "pkgdb.h"

#define ADD_CACHE(db, key, val) \
	do { \
		cdb_make_add((db), key, strlen(key), val, strlen(val)); \
	} while (0)

void pkgdb_cache_update(void);
struct pkg **pkgdb_cache_list_packages(void);

#endif
