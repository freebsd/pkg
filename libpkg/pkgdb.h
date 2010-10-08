#ifndef _PKGDB_H
#define _PKGDB_H

#include <pkg.h>

#define PKG_DBDIR "/var/db/pkg"

void pkgdb_lock(struct pkgdb *db, int write);
void pkgdb_unlock(struct pkgdb *db);
const char * pkgdb_get_dir(void);

/* getter */
const char *pkg_namever(struct pkg *);
const char *pkg_name(struct pkg *);
const char *pkg_version(struct pkg *);
const char *pkg_comment(struct pkg *);
const char *pkg_desc(struct pkg *);
const char *pkg_origin(struct pkg *);
int pkg_dep(struct pkg *, struct pkg *);

/* query */
int pkgdb_init(struct pkgdb *, const char *, match_t);
int pkgdb_query(struct pkgdb *, struct pkg *);
void pkgdb_free(struct pkgdb *);

#endif
