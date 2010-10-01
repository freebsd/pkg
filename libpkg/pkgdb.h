#ifndef _PKGDB_H
#define _PKGDB_H
#include <pkg.h>

#define PKG_DBDIR "/var/db/pkg"
#define PKGDB_LOCK "lock"
#define PKGDB_NAME    "%zun"
#define PKGDB_VERSION "%zuv"
#define PKGDB_COMMENT "%zuc"
#define PKGDB_DESC    "%zud"
#define PKGDB_ORIGIN  "%zuo"
#define PKGDB_DEPS    "%zuD"
#define PKGDB_MTIME   "%zum"
#define PKGDB_COUNT   "count"

/* quick cdb_get */
#define db_get(val, db) do { \
	(val) = cdb_get((db), cdb_datalen((db)), cdb_datapos((db))); \
	} while (0)

typedef enum _match_t {
	MATCH_ALL,
	MATCH_EXACT,
	MATCH_GLOB,
	MATCH_REGEX,
	MATCH_EREGEX
} match_t;

void pkgdb_lock(struct pkgdb *db, int write);
void pkgdb_unlock(struct pkgdb *db);
const char * pkgdb_get_dir(void);
void pkgdb_init(struct pkgdb *, const char *pattern, match_t match, unsigned char flags);
void pkgdb_free(struct pkgdb *db);
size_t pkgdb_count(struct pkgdb *db);

const void *pkgdb_query(struct pkgdb *db, const char *fmt, ...);
struct pkg *pkgdb_pkg_query(struct pkgdb *db, size_t idx);
void pkgdb_deps_query(struct pkgdb *db, struct pkg *pkg);
int pkgdb_open(struct pkgdb *db);

#endif
