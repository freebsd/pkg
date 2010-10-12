#ifndef _PKG_H
#define _PKG_H

#include <stdint.h>
#include <stdio.h> /* for size_t */
#include <regex.h> /* regex_t */

/* Opaque type */
struct cdb;

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
	const char *namever;
	const char *name;
	const char *version;
	const char *origin;
	const char *comment;
	const char *desc;
	int32_t idx; /* index on pkgdb */
	size_t idep; /* iterator deps */
	size_t irdep; /* iterator rdeps */
	struct pkgdb *pdb;
};


typedef enum pkg_formats { TAR, TGZ, TBZ, TXZ } pkg_formats;
int pkg_create(char *, pkg_formats, const char *, const char *);

#endif
