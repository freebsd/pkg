#ifndef _PKG_H
#define _PKG_H

#include <cdb.h>
#include <stdio.h> /* for size_t */

#define PKGERR_NOT_INSTALLED    (1<<0) /* dep not register (partial pkg with only name_version set */
#define PKGERR_VERSION_MISMATCH (1<<1) /* dep_version != require version */

struct pkg {
	const char *name_version;
	const char *name;
	const char *version;
	const char *origin;
	const char *comment;
	const char *desc;
	struct pkg **deps; /* null-terminated */
	struct pkg **rdeps; /* null-terminated */
	unsigned char errors; /* PKGERR_* */
	size_t idx; /* index on pkgdb */
};

#define PKGDB_INIT_DEPS (1<<0)
#define PKGDB_INIT_RDEPS (1<<1)

struct pkgdb {
	struct pkg **pkgs; /* null-terminated */
	size_t count;
	size_t i;
	struct cdb db;
	unsigned char flags;
	int lock_fd;
};

#define PKGDB_FOREACH(pkg, db) for ((db)->i = 0, (pkg) = (db)->pkgs[0]; \
		(pkg) != NULL; (pkg) = (db)->pkgs[++(db)->i])

typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(char *, pkg_formats, const char *, const char *);
#endif
