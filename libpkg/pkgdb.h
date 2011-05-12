#ifndef _PKGDB_H
#define _PKGDB_H

#include "pkg.h"

#include "sqlite3.h"

struct pkgdb {
	sqlite3 *sqlite;
	pkgdb_t remote;
};

#define IT_LOCAL 0
#define IT_UPGRADE 1

struct pkgdb_it {
	struct pkgdb *db;
	sqlite3_stmt *stmt;
	int type;
};

#endif
