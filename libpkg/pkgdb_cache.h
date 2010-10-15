#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H

#include <stdbool.h>

#include <cdb.h>

#include "pkgdb.h"

/* query */
int pkgdb_cache_open(struct pkgdb *, bool);
void pkgdb_cache_close(struct pkgdb *);
int pkgdb_cache_query(struct pkgdb *, struct pkg *);

/* getter */
const char *pkgdb_cache_getattr(struct pkg *, const char *);
int pkgdb_cache_dep(struct pkg *, struct pkg *);

#endif
