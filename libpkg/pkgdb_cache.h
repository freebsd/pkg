#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H

#include <cdb.h>

#include "pkgdb.h"

/* getter */
const char *pkgdb_cache_getattr(struct pkg *, const char *);

/* query */
int pkgdb_cache_init(struct pkgdb *);
int pkgdb_cache_dep(struct pkg *, struct pkg *);
int pkgdb_cache_rdep(struct pkg *, struct pkg *);
int pkgdb_cache_query(struct pkgdb *, struct pkg *);
void pkgdb_cache_free(struct pkgdb *);

#endif
