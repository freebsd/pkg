#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H
#include "pkgdb.h"

void pkgdb_cache_update(void);
struct pkg **pkgdb_cache_list_packages(void);

#endif
