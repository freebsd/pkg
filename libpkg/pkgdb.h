#ifndef _PKGDB_H
#define _PKGDB_H

#include <regex.h>
#include <stdbool.h>

#include <sqlite3.h>

struct pkgdb {
        sqlite3 *sqlite;
	sqlite3_stmt *stmt;
        int lock_fd;
        size_t i; /* iterator */
        int errnum;
        char errstring[BUFSIZ]; /* not enough ? */
};

void pkgdb_set_error(struct pkgdb *, int, const char *, ...);

#endif
