#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

#include <sqlite3.h>

#include "pkg_manifest.h"

struct pkg {
        const char *name;
        const char *version;
        const char *origin;
        const char *comment;
        const char *desc;
        struct pkgdb *pdb;
	sqlite3_stmt *deps_stmt;
	sqlite3_stmt *rdeps_stmt;
	sqlite3_stmt *which_stmt;
        struct pkg_manifest *m;
};

void pkg_from_manifest(struct pkg*, struct pkg_manifest *);

#endif
