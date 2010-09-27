#ifndef _PKGDB_H
#define _PKGDB_H
#include <pkg.h>

#define PKG_DBDIR "/var/db/pkg"

void pkgdb_init(struct pkgdb *db, const char *pattern);
void pkgdb_free(struct pkgdb *db);
size_t pkgdb_count(struct pkgdb *db);


#endif
