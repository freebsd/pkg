#ifndef _PKG_H
#define _PKG_H

struct pkg;
struct pkgdb;

typedef enum _match_t {
	MATCH_ALL,
	MATCH_EXACT,
	MATCH_GLOB,
	MATCH_REGEX,
	MATCH_EREGEX
} match_t;

int pkg_new(struct pkg **);
void pkg_reset(struct pkg *);
void pkg_free(struct pkg *);
const char *pkg_name(struct pkg *);
const char *pkg_version(struct pkg *);
const char *pkg_comment(struct pkg *);
const char *pkg_desc(struct pkg *);
const char *pkg_origin(struct pkg *);
int pkg_dep(struct pkg *, struct pkg *);
int pkg_rdep(struct pkg *, struct pkg *);
int pkg_files(struct pkg *, const char **);

/* pkgdb */
int pkgdb_open(struct pkgdb **);
void pkgdb_close(struct pkgdb *);

int pkgdb_query_init(struct pkgdb *, const char *, match_t);
int pkgdb_query(struct pkgdb *, struct pkg *);
void pkgdb_query_free(struct pkgdb *);
int pkgdb_query_which(struct pkgdb *, const char *, struct pkg *);

const char *pkgdb_get_dir(void);
void pkgdb_warn(struct pkgdb *);
int pkgdb_errnum(struct pkgdb *);

/* create */
typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(const char *, pkg_formats, const char *, const char *, struct pkg *);

#endif
