#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H
#include "pkgdb.h"

#define PKGDB_VERSION "%dv"
#define PKGDB_COMMENT "%dc"
#define PKGDB_DESC    "%dd"
#define PKGDB_ORIGIN  "%do"

void pkgdb_cache_update(void);
struct pkg **pkgdb_cache_list_packages(const char*pattern);

#endif
