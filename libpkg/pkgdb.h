#ifndef _PKGDB_H
#define _PKGDB_H

#include "pkg.h"

#include "sqlite3.h"

struct pkgdb {
	sqlite3 *sqlite;
	pkgdb_t remote;
};

struct pkgdb_it {
	struct pkgdb *db;
	sqlite3_stmt *stmt;
};

#endif
