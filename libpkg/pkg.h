#ifndef _PKG_H
#define _PKG_H

#include <stdint.h>
#include <stdio.h> /* for size_t */
#include <regex.h> /* regex_t */

/* Opaque type */
struct cdb;
struct pkg_manifest;

typedef enum _match_t {
	MATCH_ALL,
	MATCH_EXACT,
	MATCH_GLOB,
	MATCH_REGEX,
	MATCH_EREGEX
} match_t;

struct pkgdb {
	struct cdb *cdb;
	int lock_fd;
	size_t i; /* iterator */
	const char *pattern;
	match_t match;
	regex_t re;
	int errnum;
	char errstring[BUFSIZ]; /* not enough ? */
};

struct pkg {
	const char *name;
	const char *version;
	const char *origin;
	const char *comment;
	const char *desc;
	int32_t idx; /* index on pkgdb */
	size_t idep; /* iterator deps */
	size_t irdep; /* iterator rdeps */
	struct pkgdb *pdb;
	struct pkg_manifest *m;
};

void pkg_from_manifest(struct pkg *, struct pkg_manifest *);
void manifest_from_pkg(struct pkg *, struct pkg_manifest **);

void pkg_reset(struct pkg *);
const char *pkg_name(struct pkg *);
const char *pkg_version(struct pkg *);
const char *pkg_comment(struct pkg *);
const char *pkg_desc(struct pkg *);
const char *pkg_origin(struct pkg *);
int pkg_dep(struct pkg *, struct pkg *);
int pkg_rdep(struct pkg *, struct pkg *);

typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(const char *, pkg_formats, const char *, const char *, struct pkg *);

#endif
