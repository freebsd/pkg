#ifndef _PKGDB_H
#define _PKGDB_H

#include "pkg.h"

#include "sqlite3.h"

struct pkgdb {
	sqlite3 *sqlite;
};

typedef enum _pkgdb_it_t {
	IT_PKG,
	IT_CONFLICT,
	IT_FILE,
	IT_EXEC,
	IT_SCRIPT,
	IT_OPTION
} pkgdb_it_t;

struct pkgdb_it {
	struct pkgdb *db;
	sqlite3_stmt *stmt;
	pkgdb_it_t type;
};

#endif
