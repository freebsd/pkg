#ifndef _PKGDB_H
#define _PKGDB_H

#include <sqlite3.h>

struct pkgdb {
	sqlite3 *sqlite;
	sqlite3_stmt *stmt;
	int errnum;
	char errstring[1024];
};

int pkgdb_query_dep(struct pkg *, struct pkg *);
int pkgdb_query_rdep(struct pkg *, struct pkg *);
void pkgdb_set_error(struct pkgdb *, int, const char *, ...);

#endif
