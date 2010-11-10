#ifndef _PKG_PRIVATE_H
#define _PKG_PRIVATE_H

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
	sqlite3_stmt *conflicts_stmt;
	sqlite3_stmt *which_stmt;
	sqlite3_stmt *files_stmt;
	struct pkg_manifest *m;
};

#endif
