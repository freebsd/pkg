#ifndef _PKGDB_H
#define _PKGDB_H
#include <pkg.h>

#define PKG_DBDIR "/var/db/pkg"

struct pkg **pkgdb_list_packages(void);


#endif
