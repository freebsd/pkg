#ifndef _PKGDB_CACHE_H
#define _PKGDB_CACHE_H

#include <cdb.h>

#include "pkgdb.h"

#define PKGDB_LOCK		"lock"
#define PKGDB_NAMEVER	"%zunv"
#define PKGDB_NAME		"%zun"
#define PKGDB_VERSION	"%zuv"
#define PKGDB_COMMENT	"%zuc"
#define PKGDB_DESC		"%zud"
#define PKGDB_ORIGIN	"%zuo"
#define PKGDB_DEPS		"%zuD%zu"
#define PKGDB_COUNT		"count"

/* quick cdb_get */
#define db_get(val, db) \
	do { \
		(val) = cdb_get((db), cdb_datalen((db)), cdb_datapos((db))); \
	} while (0)

int pkgdb_cache_open(struct pkgdb *);
void pkgdb_cache_close(struct cdb *);

const void *pkgdb_cache_vget(struct cdb *, const char *, ...);
const char *pkgdb_cache_getattr(struct pkg *, const char *);
int pkgdb_cache_dep(struct pkg *, struct pkg *);

void pkgdb_cache_update(struct pkgdb *db);

#endif
