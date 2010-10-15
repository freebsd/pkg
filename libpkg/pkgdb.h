#ifndef _PKGDB_H
#define _PKGDB_H

#include <stdbool.h>

#include <pkg.h>

int pkgdb_open(struct pkgdb *);
int pkgdb_open2(struct pkgdb *, bool);
void pkgdb_close(struct pkgdb *);

/* query */
int pkgdb_query_init(struct pkgdb *, const char *, match_t);
int pkgdb_query(struct pkgdb *, struct pkg *);
void pkgdb_query_free(struct pkgdb *);

/* misc */
const char *pkgdb_get_dir(void);
int pkgdb_match(struct pkgdb *, const char *);
int pkgdb_lock(struct pkgdb *, int);
void pkgdb_set_error(struct pkgdb *, int, const char *, ...);
void pkgdb_warn(struct pkgdb *);
int pkgdb_errnum(struct pkgdb *);

#endif
