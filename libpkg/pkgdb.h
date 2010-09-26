#ifndef _PKGDB_H
#define _PKGDB_H
#include <pkg.h>

#define PKG_DBDIR "/var/db/pkg"

struct pkg **pkgdb_list_packages(const char*pattern);
void pkgdb_free(struct pkg **pkgs);
size_t pkgdb_count(struct pkg **pkgs);


#endif
