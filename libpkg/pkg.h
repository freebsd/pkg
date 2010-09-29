#ifndef _PKG_H
#define _PKG_H

#include <cdb.h>
#include <stdio.h> /* for size_t */

struct pkg {
	char *name_version;
	const char *name;
	const char *version;
	const char *origin;
	const char *comment;
	const char *desc;
	struct pkg **deps; /* null-terminated */
};

struct pkgdb {
	struct pkg **pkgs; /* null-terminated */
	size_t count;
	size_t i;
	struct cdb db;
};

#define PKGDB_FOREACH(pkg, db) for ((db)->i = 0, (pkg) = (db)->pkgs[0]; \
		(pkg) != NULL; (pkg) = (db)->pkgs[++(db)->i])

typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(char *, pkg_formats, const char *, const char *);
#endif
